#!/usr/bin/env python3
"""Check the generated code list on real play: every ordered pair of the 21 lists.

Half of each cell plays uniform random, half plays the frozen heuristic, because they reach
different cards. Any crash, diagnostic or contract failure shows up as a void cell.
"""
import argparse
import itertools
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import decks as deck_registry  # noqa: E402
from actor import Pool  # noqa: E402

SEED = 73590000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=2)
    args = parser.parse_args()
    names = sorted(deck_registry.registry())
    jobs = []
    for index, (a, b) in enumerate(itertools.permutations(names, 2)):
        for kind, offset in (("random", 0), ("heuristic", 2)):
            jobs.append({"seed": SEED + index * 4 + offset, "games": 2, "deck0": a, "deck1": b,
                         "policies": [{"kind": kind}, {"kind": kind}]})
    pool = Pool(args.workers, args.output.parent / (args.output.stem + "-logs"))
    try:
        results = pool.map(jobs)
    finally:
        pool.close()
    void = [{"deck0": j["deck0"], "deck1": j["deck1"], "kind": j["policies"][0]["kind"], "seed": j["seed"],
             "reason": r.get("reason"), "log": r.get("log_tail", "")[-300:]}
            for j, r in zip(jobs, results) if r["status"] != "PASS"]
    duels = [d for r in results if r["status"] == "PASS" for d in r["duels"]]
    report = {"lists": len(names), "cells": len(jobs), "duels_completed": len(duels), "void_cells": void,
              "draws": sum(d["winner"] == 2 for d in duels), "decisions": sum(d["steps"] for d in duels),
              "max_options": max(r["counters"]["max_options"] for r in results if r["status"] == "PASS"),
              "seat0_wins": sum(d["winner"] == 0 for d in duels), "seed_range": [SEED, SEED + len(jobs) * 4],
              "codes_sha256": deck_registry.load_codes()[0]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps({k: (v if k != "void_cells" else len(v)) for k, v in report.items()}, indent=1))
    for row in void[:10]:
        print(row)


if __name__ == "__main__":
    main()
