#!/usr/bin/env python3
"""Full-cell throughput on the quiet machine: audit, policy, env and bookkeeping together."""
import argparse
import itertools
import json
from pathlib import Path
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import decks as deck_registry  # noqa: E402
from decks import ROOT  # noqa: E402
from actor import Pool, checkpoint_paths  # noqa: E402

SEED = 73891000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--net", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    meta = json.loads(checkpoint_paths(args.net)[1].read_text())
    net = {"kind": "net", "checkpoint": args.net, "sha256": meta["params_sha256"], "greedy": True}
    numpy_model = str(ROOT / "tests/edison/reports/rl-budget-2026-11-02/seed-1702/final-policy.json")
    numpy_policy = {"kind": "baseline", "checkpoint": numpy_model, "greedy": True}
    report = {}
    cells = list(itertools.permutations(deck_registry.FIXTURE_NAMES, 2))
    for workers, games in ((1, 8), (1, 64), (8, 8), (8, 64)):
        pool = Pool(workers, args.output.parent / "bench-cells-logs")
        try:
            for name, policies in (("net-vs-heuristic", [net, {"kind": "heuristic"}]), ("net-self", [net, net]),
                                   ("numpy-vs-heuristic", [numpy_policy, {"kind": "heuristic"}]),
                                   ("heuristic-vs-heuristic", [{"kind": "heuristic"}, {"kind": "heuristic"}])):
                jobs = [{"seed": SEED + i * games, "games": games, "deck0": a, "deck1": b, "policies": policies}
                        for i, (a, b) in enumerate(cells * 4)]
                pool.map(jobs[:workers])  # Warm jit shapes; not timed.
                started = time.perf_counter()
                results = pool.map(jobs)
                wall = time.perf_counter() - started
                assert all(r["status"] == "PASS" for r in results), [r.get("reason") for r in results]
                decisions = sum(r["counters"]["decisions"] for r in results)
                net_decisions = sum(r["counters"]["net_decisions"] for r in results)
                row = {"workers": workers, "games_per_cell": games, "duels": len(jobs) * games, "decisions_per_second": decisions / wall,
                       "duels_per_second": len(jobs) * games / wall, "decisions": decisions,
                       "env_share": sum(r["counters"]["env_seconds"] for r in results) / sum(r["elapsed"] for r in results),
                       "net_share": sum(r["counters"]["net_seconds"] for r in results) / sum(r["elapsed"] for r in results),
                       "python_policy_share": sum(r["counters"]["python_policy_seconds"] for r in results) / sum(r["elapsed"] for r in results),
                       "audit_share": sum(r["counters"]["audit_seconds"] for r in results) / sum(r["elapsed"] for r in results),
                       "net_us_per_decision": 1e6 * sum(r["counters"]["net_seconds"] for r in results) / net_decisions if net_decisions else None,
                       "mean_net_batch": net_decisions / max(sum(r["counters"]["net_calls"] for r in results), 1)}
                report[f"{name}/w{workers}/g{games}"] = row
                print(name, {k: (round(v, 3) if isinstance(v, float) else v) for k, v in row.items()}, flush=True)
        finally:
            pool.close()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1) + "\n")


if __name__ == "__main__":
    main()
