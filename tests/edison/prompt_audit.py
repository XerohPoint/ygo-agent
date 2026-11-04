#!/usr/bin/env python3
"""Read the test-only prompt differential's output, and show the differential changes nothing.

  summarize DIR [DIR ...]      Merge the audit-*.jsonl files of soak stages into one report.
  no-effect                    Play the same duels with the audit off and on, and require whole
                               observation traces to match byte for byte.
  compare-soak OLD NEW         Compare two soak runs of one seed plan, duel by duel. Soak policies
                               never read a label, so rule outcomes must match.
  compare-fields OLD_SO NEW_SO Replay duels under a uniform policy on two module files and show
                               which observation fields differ, decision by decision.

The differential is ygoenv/ygoenv/ygopro/prompt_audit.h. It compares each option's card id and
row with the card code in the engine's own prompt. See docs/edison-deck-labels.md.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIELDS = ("steps", "options", "hidden", "label_mismatch", "row_mismatch", "zone_mismatch", "shared_row",
          "lone_options", "lone_mismatch", "unknown_code", "private_shown", "selected_checked", "selected_mismatch",
          "face_mismatch", "hidden_pointer")
MISMATCH_FIELDS = ("label_mismatch", "row_mismatch", "zone_mismatch", "shared_row", "lone_mismatch", "private_shown",
                   "selected_mismatch", "face_mismatch", "hidden_pointer")


def read_records(directory):
    for path in sorted(Path(directory).glob("audit-*.jsonl")):
        for line in path.read_text(errors="replace").splitlines():
            if line:
                record = json.loads(line)
                if record.get("type") == "duel":
                    yield record


def summarize(directories):
    """Totals by prompt kind, plus every recorded mismatch grouped by what went wrong."""
    totals, examples = {}, {}
    duels = decisions = card_decisions = extra = extra_prompts = mismatches = 0
    order = {"pairs_distinct": 0, "top_first_distinct": 0, "pairs_same": 0, "top_first_same": 0}
    for directory in directories:
        for record in read_records(directory):
            duels += 1
            decisions += record["decisions"]
            card_decisions += record["card_decisions"]
            extra += record["extra_visible_rows"]
            extra_prompts += record["prompts_with_extra_rows"]
            mismatches += record["mismatch_total"]
            order["pairs_distinct"] += record["order_pairs_distinct"]
            order["top_first_distinct"] += record["order_top_first_distinct"]
            order["pairs_same"] += record["order_pairs_same"]
            order["top_first_same"] += record["order_top_first_same"]
            for key, row in record["counts"].items():
                into = totals.setdefault(key, [0] * len(FIELDS))
                for i, value in enumerate(row):
                    into[i] += value
            for item in record["mismatches"]:
                kind = item.get("key") or item.get("msg")
                bucket = (item["kind"], kind)
                entry = examples.setdefault(bucket, {"kind": item["kind"], "where": kind, "recorded": 0, "first": {**item, "seed": record["seed"]}})
                entry["recorded"] += 1
    by_kind = {key: dict(zip(FIELDS, row)) for key, row in sorted(totals.items())}
    return {
        "duels": duels, "decisions": decisions, "decisions_listing_cards": card_decisions,
        "mismatch_total": mismatches,
        "by_field": {field: sum(row[field] for row in by_kind.values()) for field in MISMATCH_FIELDS},
        "extra_visible_own_deck_rows": extra, "prompts_with_extra_rows": extra_prompts,
        "own_deck_prompt_order": order, "by_kind": by_kind,
        "mismatch_groups": sorted(examples.values(), key=lambda e: (-e["recorded"], e["kind"], str(e["where"]))),
    }


def play(seed, games, deck0, deck1, so):
    """A child process: hash every field of every observation under one uniform policy."""
    sys.path[:0] = [str(ROOT), str(ROOT / "ygoenv")]
    if so:
        import importlib.util
        spec = importlib.util.spec_from_file_location("ygoenv.ygopro.ygopro_ygoenv", so)
        module = importlib.util.module_from_spec(spec)
        sys.modules["ygoenv.ygopro.ygopro_ygoenv"] = module
        spec.loader.exec_module(module)
    import random
    import numpy as np
    import ygoenv
    from ygoenv.ygopro import init_module
    decks = {p.stem: str(p) for folder in ("decks", "agent_decks") for p in (ROOT / "assets/edison" / folder).glob("*.ydk")}
    init_module(str(ROOT / "assets/locale/en/cards.cdb"), str(ROOT / "assets/edison/agent_codes.txt"), decks)
    digest = hashlib.sha256()
    decisions = 0
    keys = ("cards_", "global_", "actions_", "h_actions_", "decision_", "selection_", "mask_")
    for game in range(games):
        env = ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=1, num_threads=1, seed=seed + game, deck1=deck0, deck2=deck1,
                          play_mode="self", player=-1, async_reset=False, greedy_reward=False, oppo_info=False,
                          max_options=256, max_steps=10000, max_episode_steps=10000, timeout=10, verbose=False)
        rng = random.Random(seed + game)
        obs, info = env.reset()
        while True:
            for key in keys:
                if key in obs:
                    digest.update(np.ascontiguousarray(obs[key]).tobytes())
            digest.update(np.ascontiguousarray(info["to_play"]).tobytes())
            action = rng.randrange(int(info["num_options"][0]))
            decisions += 1
            obs, _, terminated, truncated, info = env.step(np.asarray([action]), np.asarray(info["env_id"]))
            if terminated[0] or truncated[0]:
                digest.update(bytes([int(info["winner"][0]) & 255]))
                break
    print("RESULT" + json.dumps({"sha256": digest.hexdigest(), "decisions": decisions}))
    sys.stdout.flush()
    os._exit(0)


def run_play(seed, games, deck0, deck1, so, audit_dir):
    env = {k: v for k, v in os.environ.items() if not k.startswith("EDISON_PROMPT_AUDIT")}
    if audit_dir:
        env["EDISON_PROMPT_AUDIT"] = str(audit_dir)
    command = [sys.executable, str(Path(__file__).resolve()), "play", "--seed", str(seed), "--games", str(games),
               "--deck0", deck0, "--deck1", deck1] + (["--so", str(so)] if so else [])
    done = subprocess.run(command, cwd=ROOT / "scripts", env=env, capture_output=True, text=True, timeout=600)
    if done.returncode:
        raise RuntimeError(done.stderr[-800:])
    return json.loads(done.stdout.split("RESULT", 1)[1])


def no_effect(seed, games, so):
    report = {"seed": seed, "games_per_pairing": games, "pairings": []}
    pairings = [("frog_monarch", "machina_gadget"), ("machina_gadget", "blackwing"), ("blackwing", "frog_monarch")]
    for index, (deck0, deck1) in enumerate(pairings):
        pair_seed = seed + index * 100
        off = run_play(pair_seed, games, deck0, deck1, so, None)
        with tempfile.TemporaryDirectory() as tmp:
            on = run_play(pair_seed, games, deck0, deck1, so, tmp)
            summary = summarize([tmp])
        report["pairings"].append({"deck0": deck0, "deck1": deck1, "seed": pair_seed, "off": off, "on": on,
                                   "identical": off == on, "audit_saw_decisions": summary["decisions"],
                                   "audit_saw_duels": summary["duels"]})
    report["identical"] = all(row["identical"] for row in report["pairings"])
    return report


def verdict_summary(report, min_duels, min_decisions, min_deck_options):
    """Why this differential does not pass, as a list: mismatches, or too little coverage to mean anything."""
    problems = []
    if report["mismatch_total"]:
        problems.append(f"{report['mismatch_total']} mismatch records")
    if report["extra_visible_own_deck_rows"]:
        problems.append(f"{report['extra_visible_own_deck_rows']} extra Deck rows")
    unknown = sum(row["unknown_code"] for row in report["by_kind"].values())
    if unknown:
        problems.append(f"{unknown} options with no prompt code to compare")
    deck_options = sum(row["options"] + row["lone_options"] for key, row in report["by_kind"].items() if "|deck|" in key)
    for name, have, least in (("duels", report["duels"], min_duels), ("decisions", report["decisions"], min_decisions),
                              ("Deck options", deck_options, min_deck_options)):
        if have < least:
            problems.append(f"only {have} {name}, below the minimum of {least}")
    return problems


def verdict_no_effect(report):
    problems = [f"{row['deck0']} against {row['deck1']}: traces differ" for row in report["pairings"] if not row["identical"]]
    if not report["pairings"]:
        problems.append("no pairings ran")
    problems += [f"{row['deck0']} against {row['deck1']}: the audit saw nothing" for row in report["pairings"]
                 if not row["audit_saw_duels"] or not row["audit_saw_decisions"]]
    return problems


def verdict_soak(report):
    problems = []
    for stage, row in report["stages"].items():
        if not row["duels_old"] or not row["duels_new"]:
            problems.append(f"{stage}: no duels ({row['duels_old']} old, {row['duels_new']} new)")
        if row["duels_old"] != row["duels_new"]:
            problems.append(f"{stage}: {row['duels_old']} old duels, {row['duels_new']} new")
        if row["rule_differences"]:
            problems.append(f"{stage}: {row['rule_differences']} rule differences")
        if row["chosen_actions_differ"]:
            problems.append(f"{stage}: chosen actions differ in {row['chosen_actions_differ']} duels")
    return problems


def verdict_fields(report):
    problems = []
    if not report["only_expected_fields_changed"]:
        problems.append("fields outside own Deck prompts changed")
    if not report["pairings"] or not all(row["decisions"] for row in report["pairings"]):
        problems.append("a pairing played no decisions")
    if not sum(row["own_deck_option_decisions"] for row in report["pairings"]):
        problems.append("no own Deck prompt was reached, so nothing was compared")
    return problems


def soak_records(stage):
    records = {}
    for log in stage.glob("b*/log*.jsonl"):
        for line in log.read_text(errors="replace").splitlines():
            try:
                record = json.loads(line)
            except ValueError:
                continue
            if "end" in record:
                records[record["end"]["id"]] = record["end"]
    return records


def compare_soak(old_root, new_root):
    """Everything a soak records that depends on the rules, not on labels shown at a Deck prompt."""
    report = {"stages": {}, "identical": True}
    rule_keys = ("status", "steps", "winner", "reason", "max_options", "prompts")
    for stage in ("lists", "sidein", "targets", "uniform"):
        old, new = soak_records(old_root / stage), soak_records(new_root / stage)
        different, chosen_different, trace_different = [], 0, 0
        for key in sorted(set(old) | set(new)):
            if key not in old or key not in new:
                different.append({"duel": key, "missing": "old" if key not in old else "new"})
                continue
            a, b = old[key], new[key]
            changed = [name for name in rule_keys if a.get(name) != b.get(name)]
            if changed:
                different.append({"duel": key, "fields": changed})
            chosen_different += sorted(map(tuple, a["chosen"])) != sorted(map(tuple, b["chosen"]))
            trace_different += a["trace"] != b["trace"]
        report["stages"][stage] = {"duels_old": len(old), "duels_new": len(new), "rule_differences": len(different),
                                   "first_differences": different[:10], "chosen_actions_differ": chosen_different,
                                   "label_traces_differ": trace_different}
        report["identical"] = report["identical"] and not different and not chosen_different
    return report


def decision_digests(seed, games, deck0, deck1, so):
    """A child process: per decision, digests of what the fix may and may not change."""
    sys.path[:0] = [str(ROOT), str(ROOT / "ygoenv")]
    import importlib.util
    spec = importlib.util.spec_from_file_location("ygoenv.ygopro.ygopro_ygoenv", so)
    module = importlib.util.module_from_spec(spec)
    sys.modules["ygoenv.ygopro.ygopro_ygoenv"] = module
    spec.loader.exec_module(module)
    import random
    import numpy as np
    import ygoenv
    from ygoenv.ygopro import init_module
    decks = {p.stem: str(p) for folder in ("decks", "agent_decks") for p in (ROOT / "assets/edison" / folder).glob("*.ydk")}
    init_module(str(ROOT / "assets/locale/en/cards.cdb"), str(ROOT / "assets/edison/agent_codes.txt"), decks)
    out = []
    for game in range(games):
        env = ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=1, num_threads=1, seed=seed + game, deck1=deck0, deck2=deck1,
                          play_mode="self", player=-1, async_reset=False, greedy_reward=False, oppo_info=False,
                          max_options=256, max_steps=10000, max_episode_steps=10000, timeout=10, verbose=False)
        rng = random.Random(seed + game)
        obs, info = env.reset()
        rows = []
        while True:
            count = int(info["num_options"][0])
            cards = obs["cards_"][0].copy()
            actions = obs["actions_"][0, :count].copy()
            own_deck = (cards[:, 2] == 1) & (cards[:, 4] == 0)
            to_deck = np.array([bool(a[0]) and bool(own_deck[int(a[0]) - 1]) for a in actions])
            actions_outside = actions.copy()
            actions_outside[to_deck, :3] = 0  # Row and label of own Deck options: allowed to change.
            rows.append({
                "rules": hashlib.sha256(obs["global_"].tobytes() + cards[~own_deck].tobytes() + bytes([int(own_deck.sum())]) +
                                        actions_outside.tobytes() + np.ascontiguousarray(info["to_play"]).tobytes()).hexdigest()[:16],
                "deck": hashlib.sha256(cards[own_deck].tobytes()).hexdigest()[:16],
                "options": hashlib.sha256(actions.tobytes()).hexdigest()[:16],
                "history": hashlib.sha256(obs["h_actions_"].tobytes()).hexdigest()[:16],
                "context": hashlib.sha256(obs["decision_"].tobytes() + obs["selection_"].tobytes()).hexdigest()[:16],
                "own_deck_options": int(to_deck.sum()),
            })
            action = rng.randrange(count)
            obs, _, terminated, truncated, info = env.step(np.asarray([action]), np.asarray(info["env_id"]))
            if terminated[0] or truncated[0]:
                rows.append({"winner": int(info["winner"][0])})
                break
        out.append(rows)
    print("RESULT" + json.dumps(out))
    sys.stdout.flush()
    os._exit(0)


def compare_fields(old_so, new_so, seed, games):
    report = {"seed": seed, "games_per_pairing": games, "pairings": []}
    pairings = [("frog_monarch", "machina_gadget"), ("machina_gadget", "blackwing"), ("blackwing", "frog_monarch"),
                ("frog_monarch", "blackwing")]
    for index, (deck0, deck1) in enumerate(pairings):
        pair_seed = seed + index * 100
        runs = []
        for so in (old_so, new_so):
            command = [sys.executable, str(Path(__file__).resolve()), "digests", "--seed", str(pair_seed), "--games", str(games),
                       "--deck0", deck0, "--deck1", deck1, "--so", str(so.resolve())]
            env = {k: v for k, v in os.environ.items() if not k.startswith("EDISON_PROMPT_AUDIT")}
            done = subprocess.run(command, cwd=ROOT / "scripts", env=env, capture_output=True, text=True, timeout=600)
            if done.returncode:
                raise RuntimeError(done.stderr[-800:])
            runs.append(json.loads(done.stdout.split("RESULT", 1)[1]))
        counts = {"decisions": 0, "own_deck_option_decisions": 0, "rules_digest_differs": 0, "deck_rows_differ": 0,
                  "deck_rows_differ_without_own_deck_options": 0, "options_differ": 0,
                  "options_differ_without_own_deck_options": 0, "context_differs": 0,
                  "context_differs_without_own_deck_options": 0, "history_differs": 0, "length_differs": 0, "winner_differs": 0}
        for old_game, new_game in zip(*runs):
            counts["length_differs"] += len(old_game) != len(new_game)
            counts["winner_differs"] += old_game[-1] != new_game[-1]
            for old, new in zip(old_game[:-1], new_game[:-1]):
                counts["decisions"] += 1
                deck_prompt = old["own_deck_options"] > 0
                counts["own_deck_option_decisions"] += deck_prompt
                counts["rules_digest_differs"] += old["rules"] != new["rules"]
                counts["history_differs"] += old["history"] != new["history"]
                for field, name in (("deck", "deck_rows"), ("options", "options"), ("context", "context")):
                    if old[field] != new[field]:
                        counts[name + ("_differ" if name != "context" else "_differs")] += 1
                        if not deck_prompt:
                            counts[name + ("_differ" if name != "context" else "_differs") + "_without_own_deck_options"] += 1
        report["pairings"].append({"deck0": deck0, "deck1": deck1, "seed": pair_seed, **counts})
    keys = ("rules_digest_differs", "length_differs", "winner_differs", "deck_rows_differ_without_own_deck_options",
            "options_differ_without_own_deck_options", "context_differs_without_own_deck_options")
    report["only_expected_fields_changed"] = all(not row[key] for row in report["pairings"] for key in keys)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    summary = sub.add_parser("summarize")
    summary.add_argument("directories", nargs="+", type=Path)
    summary.add_argument("--output", type=Path)
    summary.add_argument("--min-duels", type=int, default=1)
    summary.add_argument("--min-decisions", type=int, default=1)
    summary.add_argument("--min-deck-options", type=int, default=1)
    effect = sub.add_parser("no-effect")
    effect.add_argument("--seed", type=int, required=True)
    effect.add_argument("--games", type=int, default=8)
    effect.add_argument("--so", type=Path)
    effect.add_argument("--output", type=Path)
    soak = sub.add_parser("compare-soak")
    soak.add_argument("old", type=Path)
    soak.add_argument("new", type=Path)
    soak.add_argument("--output", type=Path)
    fields = sub.add_parser("compare-fields")
    fields.add_argument("old", type=Path)
    fields.add_argument("new", type=Path)
    fields.add_argument("--seed", type=int, required=True)
    fields.add_argument("--games", type=int, default=16)
    fields.add_argument("--output", type=Path)
    digests = sub.add_parser("digests")
    digests.add_argument("--seed", type=int, required=True)
    digests.add_argument("--games", type=int, required=True)
    digests.add_argument("--deck0", required=True)
    digests.add_argument("--deck1", required=True)
    digests.add_argument("--so", type=Path, required=True)
    child = sub.add_parser("play")
    child.add_argument("--seed", type=int, required=True)
    child.add_argument("--games", type=int, required=True)
    child.add_argument("--deck0", required=True)
    child.add_argument("--deck1", required=True)
    child.add_argument("--so", type=Path)
    args = parser.parse_args()
    if args.command == "play":
        play(args.seed, args.games, args.deck0, args.deck1, args.so)
        return 0
    if args.command == "digests":
        decision_digests(args.seed, args.games, args.deck0, args.deck1, args.so)
        return 0
    if args.command == "summarize":
        report = summarize(args.directories)
        problems = verdict_summary(report, args.min_duels, args.min_decisions, args.min_deck_options)
    elif args.command == "no-effect":
        report = no_effect(args.seed, args.games, args.so)
        problems = verdict_no_effect(report)
    elif args.command == "compare-soak":
        report = compare_soak(args.old, args.new)
        problems = verdict_soak(report)
    else:
        report = compare_fields(args.old, args.new, args.seed, args.games)
        problems = verdict_fields(report)
    report["problems"] = problems
    report["passed"] = not problems
    text = json.dumps(report, indent=1)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n")
    print(text)
    print(("FAILED: " + "; ".join(problems)) if problems else "PASSED")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
