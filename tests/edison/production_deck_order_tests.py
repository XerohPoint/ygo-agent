#!/usr/bin/env python3
"""Test 087: what a policy sees at a Deck prompt does not depend on the hidden order of a Deck.

Two duels share one seed, so the same random stream deals by the same permutation of list positions.
A probe with 40 distinct cards shows which list positions land in the hand and on top of the Deck. The
two duels keep those positions and reverse the rest, so public state and private hands match while the
Deck below them is in another order. Every decision on the first turn must then give byte-identical
policy inputs, and the prompt's true sequences must differ, which the differential reports. The first
turn is the limit: the next draw could expose the different order by simple play, not by the interface.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT))
sys.path.insert(0, str(ROOT / "ygoenv"))
sys.path.insert(0, str(ROOT / "tools/edison/pool_audit"))
from production_prompt_tests import DB, MARKERS, code_list, code_of  # noqa: E402
from production_deck_label_tests import DUPE, OPPOSING_DECK, REVEALED_COPY, UPSTART, overlay_scripts  # noqa: E402

SEEDS = (80820000, 80821000)
MAX_OPTIONS = 256
SEQUENCES = ("cards_", "global_", "h_actions_", "decision_", "selection_")
ACT_SUMMON, ACT_ACTIVATE = 4, 8
IDLE, CHAIN, CARD, TRIBUTE, EFFECT_YN, UNSELECT = 1, 2, 3, 4, 6, 12
PROTECT_TOP = 8

# label: (title, seat's hand cards, seat's Deck cards, the other seat's Deck cards, cards to play, picks in preference order, planted script, needed)
CASES = {
    "A": ("SUBSTITOAD AND SWAP FROG", ["Substitoad"], [("Swap Frog", 3), ("Dupe Frog", 3), ("Treeborn Frog", 2)], [],
          ["Substitoad", "Swap Frog"], ["Swap Frog"], None, {"own_deck": 2, "mixed": 1}),
    "B": ("TWO PICKS FROM THE DECK", ["Thunder Dragon"], [("Thunder Dragon", 2)], [], ["Thunder Dragon"], ["Thunder Dragon"], None, {"own_deck": 1, "selected": 1}),
    "C": ("UNSELECT LIST FROM THE DECK", ["Advanced Ritual Art", "Demise, King of Armageddon"], [], [],
          ["Advanced Ritual Art"], [], None, {"own_deck": 2, "selected": 1}),
    "D": ("THE OPPONENT'S DECK", ["Upstart Goblin"], [], [("Upstart Goblin", 3)], ["Upstart Goblin"], [], OPPOSING_DECK, {"opposing_deck": 1}),
    "E": ("A COPY ALREADY REVEALED", ["Upstart Goblin"], [("Dupe Frog", 3)], [], ["Upstart Goblin"], [], REVEALED_COPY, {"own_deck": 1, "revealed": 1}),
}


def filler(db, count, used):
    manifest = json.loads((ROOT / "assets/edison/legality.json").read_text())
    pool = {code for card in manifest["cards"] for code in card["accepted_ids"]}
    limited = {card["code"]: card["limit"] for card in manifest["restrictions"]}
    rows = [row[0] for row in db.execute("SELECT id FROM datas WHERE type=17 AND alias=0 AND level<=4 AND atk>=1000 ORDER BY id")
            if row[0] in pool and row[0] not in limited and row[0] not in used]
    assert len(rows) >= count, "Not enough distinct filler cards"
    return rows[:count]


def write_list(path, cards):
    assert len(cards) == 40
    path.write_text("#main\n" + "\n".join(map(str, cards)) + "\n#extra\n!side\n")


def load_module(so):
    if so:
        import importlib.util
        spec = importlib.util.spec_from_file_location("ygoenv.ygopro.ygopro_ygoenv", str(so))
        module = importlib.util.module_from_spec(spec)
        sys.modules["ygoenv.ygopro.ygopro_ygoenv"] = module
        spec.loader.exec_module(module)


def make_env(seed, deck0, deck1, oppo_info=False):
    import ygoenv
    return ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=1, num_threads=1, seed=seed, deck1=deck0, deck2=deck1, play_mode="self",
                       player=-1, async_reset=False, greedy_reward=False, oppo_info=oppo_info, max_options=MAX_OPTIONS, max_steps=10000,
                       max_episode_steps=10000, timeout=10, verbose=False)


def probe(codes, deck, first_seed, so):
    """Which list positions land in each hand and near the top. oppo_info=True exposes Deck order, which is the point here."""
    load_module(so)
    from ygoenv.ygopro import init_module
    init_module(str(DB), str(codes), {"probe": str(deck)})
    id_to_code = {i + 1: int(line.split()[0]) for i, line in enumerate(codes.read_text().splitlines())}
    listed = [int(line) for line in deck.read_text().splitlines() if line.isdigit()]
    seed = first_seed
    while True:
        env = make_env(seed, "probe", "probe", oppo_info=True)
        obs, info = env.reset()
        if int(info["to_play"][0]) == 0 and int(obs["global_"][0][7]) == 1:
            break
        seed += 1
    protect = {}
    for seat in (0, 1):
        relative = seat  # Seat 0 is viewing, so column 4 is 0 for seat 0.
        rows = [row for row in obs["cards_"][0] if int(row[4]) == relative]
        codes_of = lambda wanted: [id_to_code[int(row[0]) * 256 + int(row[1])] for row in rows if int(row[2]) == wanted]
        hand, deck_order = codes_of(2), codes_of(1)
        protect[seat] = {"hand": [listed.index(code) for code in hand], "top": [listed.index(code) for code in deck_order[-PROTECT_TOP:]]}
    print("RESULT" + json.dumps({"seed": seed, "protect": protect}))
    sys.stdout.flush()
    os._exit(0)


def build(db, case, protect):
    """Seat lists for one case: the fixed positions, then the Deck order and its reverse."""
    name, hand, deck, other_deck, play, picks, script, needed = CASES[case]
    own_cards = [code_of(db, card) for card in hand]
    own_deck = [code_of(db, card) for card, copies in deck for _ in range(copies)]
    other = [code_of(db, card) for card, copies in other_deck for _ in range(copies)]
    used = set(own_cards) | set(own_deck) | set(other) | {DUPE, UPSTART}
    extra = filler(db, 80, used)
    lists = {}
    for seat, chosen_hand, chosen_deck in ((0, own_cards, own_deck), (1, [], other)):
        fixed = protect[str(seat)]["hand"] + protect[str(seat)]["top"]
        deep = [i for i in range(40) if i not in fixed]
        take = list(extra[:40 - len(chosen_hand) - len(chosen_deck)])
        extra = extra[len(take):]
        slots = [None] * 40
        for index, card in zip(protect[str(seat)]["hand"], chosen_hand):
            slots[index] = card
        rest_fixed = [i for i in fixed if slots[i] is None]
        for index in rest_fixed:
            slots[index] = take.pop()
        # Mix wanted cards among filler by a fixed stride, so the reverse really moves them.
        pool = chosen_deck + take
        order = sorted(range(len(pool)), key=lambda i: (i * 7) % len(pool))
        pool = [pool[i] for i in order]
        assert len(pool) == len(deep)
        first = list(slots)
        second = list(slots)
        for index, card in zip(deep, pool):
            first[index] = card
        for index, card in zip(deep, reversed(pool)):
            second[index] = card
        assert sorted(first) == sorted(second)
        lists[seat] = (first, second)
    return lists


def worker(case, codes, directory, seed, so):
    load_module(so)
    import numpy as np
    from ygoenv.ygopro import init_module
    name, hand, deck, other_deck, play, picks, script, needed = CASES[case]
    decks = {f"{tag}{seat}": str(directory / f"{tag}{seat}.ydk") for tag in "ab" for seat in (0, 1)}
    init_module(str(DB), str(codes), decks)
    id_to_code = {i + 1: int(line.split()[0]) for i, line in enumerate(codes.read_text().splitlines())}
    code_to_id = {code: cid for cid, code in id_to_code.items()}
    db = sqlite3.connect(DB)
    play_ids = {code_to_id[code_of(db, card)] for card in play}
    pick_ids = [code_to_id[code_of(db, card)] for card in picks]
    trace = Path(os.environ["EDISON_PROMPT_AUDIT"]) / f"audit-{os.getpid()}.jsonl"
    position = 0

    def new_lines():
        nonlocal position
        if not trace.exists():
            return []
        with trace.open() as stream:
            stream.seek(position)
            text = stream.read()
            position = stream.tell()
        return [json.loads(line) for line in text.splitlines() if line]

    def run(tag):
        env = make_env(seed, f"{tag}0", f"{tag}1")
        obs, info = env.reset()
        out = []
        while True:
            glob = obs["global_"][0]
            if int(glob[4]) > 1:
                return out
            count = int(info["num_options"][0])
            offered = obs["actions_"][0, :count]
            digests = {key: hashlib.sha256(np.ascontiguousarray(obs[key][0]).tobytes()).hexdigest()[:16] for key in SEQUENCES}
            digests["actions_"] = hashlib.sha256(np.ascontiguousarray(offered).tobytes()).hexdigest()[:16]
            digests["info"] = [int(info["to_play"][0]), count, int(info["legal_options"][0])]
            lines = [line for line in new_lines() if line["type"] == "decision"]
            shown = lines[-1] if lines else None
            ids = [int(a[1]) * 256 + int(a[2]) for a in offered]
            msg = int(offered[0, 3])
            action = count - 1  # Passing, or the last phase, otherwise.
            wanted = [i for i, cid in enumerate(ids) if cid in play_ids and int(offered[i, 4]) in (ACT_ACTIVATE, ACT_SUMMON)]
            if int(info["to_play"][0]) == 0:
                if wanted:
                    action = wanted[0]
                elif msg in (CARD, UNSELECT, TRIBUTE):
                    choices = [i for pick in pick_ids for i, cid in enumerate(ids) if cid == pick]
                    action = choices[0] if choices else 0
                elif msg == EFFECT_YN:
                    action = 0
            elif msg in (CARD, UNSELECT, TRIBUTE):
                action = 0
            out.append({"digests": digests, "action": action, "turn": int(glob[4]),
                        "msg": shown["msg"] if shown else None,
                        "options": [(e["spec_kind"], e["controller"] == shown["player"]) for e in shown["options"]] if shown else [],
                        "selected": len(shown["selected"]) if shown else 0,
                        "revealed": len(shown["revealed_own_deck"]) if shown else 0,
                        "sequences": shown["deck_sequences"] if shown else [],
                        "extra_rows": shown["extra_rows"] if shown else 0,
                        "real": [e["prompt_sequence"] for e in shown["options"] if e["spec_kind"] == "deck"] if shown else [],
                        "labels": ids})
            obs, _, terminated, truncated, info = env.step(np.asarray([action]), np.asarray(info["env_id"]))
            if terminated[0] or truncated[0]:
                return out

    first, second = run("a"), run("b")
    print("RESULT" + json.dumps({"a": first, "b": second}))
    sys.stdout.flush()
    os._exit(0)


def compare(case, first, second):
    name, hand, deck, other_deck, play, picks, script, needed = CASES[case]
    differences = []
    moved = 0
    for index, (a, b) in enumerate(zip(first, second)):
        if a["digests"] != b["digests"]:
            fields = [key for key in a["digests"] if a["digests"][key] != b["digests"][key]]
            differences.append({"decision": index, "fields": fields, "msg": a["msg"], "labels_a": a["labels"][:8], "labels_b": b["labels"][:8]})
            break
        if a["sequences"] != b["sequences"]:
            moved += 1
    assert len(first) == len(second), f"Different number of first-turn decisions: {len(first)}, {len(second)}"
    assert not differences, f"Observations depend on hidden Deck order: {differences[0]}"
    own = [row for row in first if ["deck", True] in row["options"]]
    opposing = [row for row in first if ["deck", False] in row["options"]]
    counts = {"own_deck": len(own), "opposing_deck": len(opposing),
              "mixed": sum(["mzone", True] in row["options"] for row in own),
              "selected": sum(row["selected"] > 0 for row in own), "revealed": sum(row["revealed"] > 0 for row in own)}
    for key, least in needed.items():
        assert counts[key] >= least, f"The first turn did not reach its prompt: {counts}, needed {needed}"
    assert moved > 0, "The two duels never showed different true Deck sequences at a prompt, so this proves nothing"
    assert not any(row["extra_rows"] for row in first + second), "Deck rows shown beyond the prompt"
    return {**counts, "decisions": len(first), "prompts_with_different_true_sequences": moved}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument("--so", type=Path)
    parser.add_argument("--worker")
    parser.add_argument("--probe", action="store_true")
    parser.add_argument("--codes", type=Path)
    parser.add_argument("--directory", type=Path)
    parser.add_argument("--deck", type=Path)
    parser.add_argument("--seed", type=int)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.probe:
        probe(args.codes, args.deck, args.seed, args.so)
        return 0
    if args.worker:
        worker(args.worker, args.codes, args.directory, args.seed, args.so)
        return 0
    passed = failed = 0
    results = {}
    db = sqlite3.connect(DB)
    so = [str(args.so.resolve())] if args.so else []
    with tempfile.TemporaryDirectory(prefix="edison-deck-order-") as tmp:
        tmp = Path(tmp)
        codes = tmp / "codes.txt"
        code_list(codes)
        probe_deck = tmp / "probe.ydk"
        write_list(probe_deck, filler(db, 40, set()))
        layouts = []
        for first_seed in SEEDS:
            command = [sys.executable, str(Path(__file__).resolve()), "--probe", "--codes", str(codes), "--deck", str(probe_deck),
                       "--seed", str(first_seed)] + (["--so"] + so if so else [])
            done = subprocess.run(command, cwd=ROOT / "scripts", capture_output=True, text=True, timeout=30)
            if done.returncode:
                raise SystemExit(done.stderr[-600:])
            layouts.append(json.loads(done.stdout.split("RESULT", 1)[1]))
        for label, (name, hand, deck, other_deck, play, picks, script, needed) in CASES.items():
            print(f"\nEDISON RULE TEST 087{label} - {name}")
            print("Expected: first-turn policy inputs are byte-identical across two hidden Deck orders, and true sequences at the prompt differ.")
            try:
                merged = {}
                for layout in layouts:
                    directory = tmp / f"{label}-{layout['seed']}"
                    directory.mkdir()
                    audit = directory / "audit"
                    audit.mkdir()
                    cwd = ROOT / "scripts"
                    lists = build(db, label, layout["protect"])
                    for seat in (0, 1):
                        write_list(directory / f"a{seat}.ydk", lists[seat][0])
                        write_list(directory / f"b{seat}.ydk", lists[seat][1])
                    if script:
                        cwd = overlay_scripts(directory / "overlay", script)
                    env = {**os.environ, "EDISON_PROMPT_AUDIT": str(audit), "EDISON_PROMPT_AUDIT_TRACE": "1"}
                    command = [sys.executable, str(Path(__file__).resolve()), "--worker", label, "--codes", str(codes), "--directory", str(directory),
                               "--seed", str(layout["seed"])] + (["--so"] + so if so else [])
                    done = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True, timeout=60)
                    output = done.stdout + done.stderr
                    body = "\n".join(l for l in output.splitlines() if not l.startswith(("Gym has", "Please upgrade", "Users of", "See the migration", "RESULT")))
                    assert done.returncode == 0, f"Worker exited {done.returncode}: {body[-500:]}"
                    assert not any(m.lower() in body.lower() for m in MARKERS), f"Native diagnostic: {body[-500:]}"
                    result = json.loads(done.stdout.split("RESULT", 1)[1])
                    for key, value in compare(label, result["a"], result["b"]).items():
                        merged[key] = merged.get(key, 0) + value
                results[label] = merged
                print(f"Observed: {merged}")
                print("RESULT: PASS")
                passed += 1
            except (AssertionError, RuntimeError, OSError, ValueError, subprocess.TimeoutExpired) as error:
                results[label] = {"failure": str(error)[:400]}
                print(f"RESULT: FAIL — {error}")
                failed += 1
                if args.fail_fast:
                    break
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({"seeds": [layout["seed"] for layout in layouts], "cases": results}, indent=1) + "\n")
    print(f"\nSUMMARY: {passed} PASS, {failed} FAIL")
    return int(failed != 0)


if __name__ == "__main__":
    raise SystemExit(main())
