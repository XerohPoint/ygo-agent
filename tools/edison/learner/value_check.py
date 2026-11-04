#!/usr/bin/env python3
"""Is the value head usable as a search leaf? Calibration on games it did not train on.

Greedy net against the heuristic on the 3 fixtures, using development seeds in 73800000+. Reports
squared error against a win-rate constant, the correlation with the outcome, and a calibration table.
"""
import argparse
import itertools
import json
from pathlib import Path
import sys
import tempfile

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import decks as deck_registry  # noqa: E402
from actor import Pool, checkpoint_paths  # noqa: E402

SEED = 73800000
COUNT = 16


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--net", action="append", required=True, help="label=stem")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=2)
    args = parser.parse_args()
    report = {"seed": SEED, "seeds_per_cell": COUNT}
    pool = Pool(args.workers, args.output.parent / (args.output.stem + "-logs"))
    try:
        for item in args.net:
            label, stem = item.split("=", 1)
            meta = json.loads(checkpoint_paths(stem)[1].read_text())
            spec = {"kind": "net", "checkpoint": stem, "sha256": meta["params_sha256"], "greedy": True}
            with tempfile.TemporaryDirectory() as tmp:
                jobs = []
                for index, ((a, b), seat) in enumerate(itertools.product(itertools.permutations(deck_registry.FIXTURE_NAMES, 2), (0, 1))):
                    policies = [spec, {"kind": "heuristic"}] if seat == 0 else [{"kind": "heuristic"}, spec]
                    jobs.append({"seed": SEED, "games": COUNT, "deck0": a, "deck1": b, "policies": policies,
                                 "collect": [seat == 0, seat == 1], "rollout": f"{tmp}/{index}.npz"})
                results = pool.map(jobs)
                if any(r["status"] != "PASS" for r in results):
                    raise SystemExit(f"Void cell: {[r.get('reason') for r in results if r['status'] != 'PASS']}")
                values, outcomes, turns = [], [], []
                for job in jobs:
                    with np.load(job["rollout"]) as data:
                        values.append(data["value"])
                        outcomes.append(data["outcome"])
                        turns.append(data["global"][:, 4])
            v, o, t = map(np.concatenate, (values, outcomes, turns))
            bins = np.linspace(-1, 1, 9)
            table = []
            for lo, hi in zip(bins[:-1], bins[1:]):
                mask = (v >= lo) & (v <= hi if hi == 1 else v < hi)
                if mask.any():
                    table.append({"value": [float(lo), float(hi)], "decisions": int(mask.sum()), "mean_value": round(float(v[mask].mean()), 3),
                                  "mean_outcome": round(float(o[mask].mean()), 3)})
            phases = {}
            for name, low, high in (("turn 1-4", 0, 4), ("turn 5-8", 5, 8), ("turn 9-12", 9, 12), ("turn 13+", 13, 99)):
                mask = (t >= low) & (t <= high)
                if mask.any():
                    phases[name] = {"decisions": int(mask.sum()), "correlation": round(float(np.corrcoef(v[mask], o[mask])[0, 1]), 3),
                                    "mse": round(float(np.mean((v[mask] - o[mask]) ** 2)), 3),
                                    "constant_mse": round(float(np.var(o[mask])), 3)}
            report[label] = {"decisions": len(v), "mse": round(float(np.mean((v - o) ** 2)), 4), "constant_mse": round(float(np.var(o)), 4),
                             "correlation": round(float(np.corrcoef(v, o)[0, 1]), 4), "calibration": table, "by_turn": phases}
            print(label, {k: report[label][k] for k in ("decisions", "mse", "constant_mse", "correlation")}, flush=True)
    finally:
        pool.close()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1) + "\n")


if __name__ == "__main__":
    main()
