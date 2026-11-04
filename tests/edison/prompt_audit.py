#!/usr/bin/env python3
"""Read the test-only prompt differential's output, and show the differential changes nothing.

  summarize DIR [DIR ...]      Merge the audit-*.jsonl files of soak stages into one report.
  no-effect                    Play the same duels with the audit off and on, and require whole
                               observation traces to match byte for byte.

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
          "lone_options", "lone_mismatch", "unknown_code", "private_shown")
MISMATCH_FIELDS = ("label_mismatch", "row_mismatch", "zone_mismatch", "shared_row", "lone_mismatch", "private_shown")


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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    summary = sub.add_parser("summarize")
    summary.add_argument("directories", nargs="+", type=Path)
    summary.add_argument("--output", type=Path)
    effect = sub.add_parser("no-effect")
    effect.add_argument("--seed", type=int, required=True)
    effect.add_argument("--games", type=int, default=8)
    effect.add_argument("--so", type=Path)
    effect.add_argument("--output", type=Path)
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
    report = summarize(args.directories) if args.command == "summarize" else no_effect(args.seed, args.games, args.so)
    text = json.dumps(report, indent=1)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
