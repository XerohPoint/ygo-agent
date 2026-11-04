#!/usr/bin/env python3
"""Evaluate frozen policies on a fixed matrix of decks, seats and duel seeds.

Every challenger meets every opponent on the same starts. A cell that fails in any way is
voided and reported; it can never add a win or a loss. Replays repeat a subset of cells on a
different env thread count, and any trace mismatch fails the run.
"""
import argparse
import gzip
import hashlib
import itertools
import json
import os
from pathlib import Path
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import decks as deck_registry  # noqa: E402
from decks import ROOT, digest  # noqa: E402
from actor import Pool, checkpoint_paths  # noqa: E402


def policy(text, greedy, repeat_guard=False):
    """heuristic | random | net:<stem> | baseline:<path>"""
    if text in ("heuristic", "random"):
        return {"kind": text}
    kind, path = text.split(":", 1)
    if kind == "net":
        meta = json.loads(checkpoint_paths(path)[1].read_text())
        return {"kind": "net", "checkpoint": str(Path(path).resolve()), "sha256": meta["params_sha256"], "greedy": greedy,
                "repeat_guard": repeat_guard}
    if kind == "baseline":
        return {"kind": "baseline", "checkpoint": str(Path(path).resolve()), "greedy": greedy}
    raise ValueError(text)


def fixture_pairs():
    return list(itertools.permutations(deck_registry.FIXTURE_NAMES, 2))


def gauntlet_pairs(opponents_per_deck=4):
    """Every list meets the next few lists in a fixed cycle, in both deck orders' seats."""
    names = sorted(deck_registry.registry())
    return [(names[i], names[(i + step) % len(names)]) for i in range(len(names))
            for step in range(1, opponents_per_deck + 1)]


def build_jobs(challengers, opponents, pairs, seed, count, greedy, opponent_greedy=True, repeat_guard=False):
    jobs = []
    for (label, spec), (op_label, op_spec), (deck0, deck1), seat in itertools.product(
            challengers.items(), opponents.items(), pairs, (0, 1)):
        c = policy(spec, greedy, repeat_guard)
        o = policy(op_spec, opponent_greedy, repeat_guard)
        jobs.append({"challenger": label, "opponent": op_label, "deck0": deck0, "deck1": deck1, "seat": seat, "seed": seed,
                     "games": count, "policies": [c, o] if seat == 0 else [o, c]})
    return jobs


def summarize(rows, by=("challenger", "opponent")):
    table = {}
    for row in rows:
        key = "|".join(str(row[k]) for k in by)
        entry = table.setdefault(key, {"wins": 0, "losses": 0, "draws": 0, "games": 0, "void_games": 0, "decisions": 0})
        if row["status"] != "PASS":
            entry["void_games"] += row["games"]
            continue
        for duel in row["duels"]:
            entry["games"] += 1
            entry["wins"] += duel["winner"] == row["seat"]
            entry["draws"] += duel["winner"] == 2
            entry["losses"] += duel["winner"] == 1 - row["seat"]
            entry["decisions"] += duel["steps"]
    for entry in table.values():
        entry["win_rate"] = round(entry["wins"] / entry["games"], 4) if entry["games"] else None
    return table


def learner_deck(row):
    return row["deck0"] if row["seat"] == 0 else row["deck1"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--challenger", action="append", required=True, help="label=policy")
    parser.add_argument("--opponent", action="append", required=True, help="label=policy")
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--decks", choices=("fixtures", "gauntlet"), default="fixtures")
    parser.add_argument("--sampled", action="store_true", help="Sample instead of argmax; diagnostic only")
    parser.add_argument("--repeat-guard", action="store_true", help="Greedy nets avoid repeating an action in a public state")
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--replay-cells", type=int, default=0, help="Replay every Nth cell with 4 env threads")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise ValueError("Refusing to overwrite: use a fresh --output")
    output.mkdir(parents=True)
    forbidden = [(22000000, 22999999), (50000000, 50999999), (60000000, 60999999), (70000000, 72999999), (74000000, 74999999)]
    if any(lo <= args.seed <= hi for lo, hi in forbidden):
        raise ValueError("Seed is in a consumed or reserved block")
    challengers = dict(item.split("=", 1) for item in args.challenger)
    opponents = dict(item.split("=", 1) for item in args.opponent)
    pairs = fixture_pairs() if args.decks == "fixtures" else gauntlet_pairs()
    jobs = build_jobs(challengers, opponents, pairs, args.seed, args.count, not args.sampled, repeat_guard=args.repeat_guard)
    pool = Pool(args.workers, output / "logs", cell_timeout=1800)
    started = time.perf_counter()
    try:
        results = pool.map(jobs)
        rows = []
        for job, result in zip(jobs, results):
            rows.append({**{k: job[k] for k in ("challenger", "opponent", "deck0", "deck1", "seat", "seed", "games")},
                         "policies": job["policies"], **result})
        elapsed = time.perf_counter() - started
        mismatches, replayed = [], 0
        if args.replay_cells:
            picks = list(range(0, len(jobs), args.replay_cells))
            again = pool.map([{**jobs[i], "threads": 4} for i in picks])
            for index, result in zip(picks, again):
                replayed += 1
                first = rows[index]
                if result.get("status") != first.get("status") or result.get("duels") != first.get("duels"):
                    mismatches.append(index)
    finally:
        pool.close()
    void = [(r["challenger"], r["opponent"], r["deck0"], r["deck1"], r["seat"], r.get("reason")) for r in rows if r["status"] != "PASS"]
    summary = {"seed": args.seed, "seeds_per_cell": args.count, "decks": args.decks, "greedy": not args.sampled,
               "elapsed_seconds": elapsed, "total": summarize(rows),
               "by_deck": summarize([{**r, "deck": learner_deck(r)} for r in rows], ("challenger", "opponent", "deck")),
               "by_seat": summarize(rows, ("challenger", "opponent", "seat")),
               "void_cells": void, "replayed_cells": replayed, "replay_mismatches": mismatches,
               "decisions": sum(r["counters"]["decisions"] for r in rows if r["status"] == "PASS"),
               "repeat_guard": args.repeat_guard,
               "guard_fires": sum(r["counters"].get("guard_fires", 0) for r in rows if r["status"] == "PASS"),
               "policies": {**challengers, **opponents},
               "policy_sha256": {j["challenger"]: j["policies"][0 if j["seat"] == 0 else 1].get("sha256") for j in jobs},
               "codes_sha256": deck_registry.load_codes()[0],
               "native_sha256": {p.name: digest(p) for p in (ROOT / "ygoenv/ygoenv/ygopro").glob("ygopro_ygoenv*.so")}}
    (output / "summary.json").write_text(json.dumps(summary, indent=1) + "\n")
    with gzip.open(output / "cells.json.gz", "wt") as f:
        json.dump(rows, f)
    print(json.dumps({k: summary[k] for k in ("total", "void_cells", "replayed_cells", "replay_mismatches", "elapsed_seconds")}, indent=1))
    if mismatches:
        raise SystemExit("Replay mismatch: results are not reproducible")


if __name__ == "__main__":
    main()
