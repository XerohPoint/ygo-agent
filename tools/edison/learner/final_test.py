#!/usr/bin/env python3
"""Run the predeclared M1 final test and apply its rules. Refuses a changed protocol or binary."""
import argparse
import gzip
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASE = ROOT / "build/edison-final-test-2026-11-04"
RUNS = ROOT / "build/edison-gauntlet-training-2026-11-04/runs"
EVIDENCE = ROOT / "tests/edison/reports/final-test-2026-11-04"
OPTIMIZERS = (1702, 1703, 1704)
FIXTURE_SEED, FIXTURE_COUNT = 79000000, 256
GAUNTLET_SEED, GAUNTLET_COUNT = 79010000, 32
NATIVE = "67423a99970519fb"


def evaluate(name, challengers, seed, count, decks, workers, guard=False):
    command = [sys.executable, str(HERE / "evaluate.py"), "--output", str(BASE / name), "--seed", str(seed), "--count", str(count), "--decks", decks,
               "--workers", str(workers), "--replay-cells", "24", "--opponent", "heuristic=heuristic"]
    for label, spec in challengers.items():
        command += ["--challenger", f"{label}={spec}"]
    if guard:
        command.append("--repeat-guard")
    print(" ".join(command), flush=True)
    subprocess.run(command, check=True)


def run(workers):
    nets = {f"net-{s}": f"net:{RUNS}/gauntlet-{s}/checkpoint-1280" for s in OPTIMIZERS}
    mirror = {"heuristic-mirror": "heuristic"}
    evaluate("control-fixtures", mirror, FIXTURE_SEED, FIXTURE_COUNT, "fixtures", workers)
    evaluate("fixtures", nets, FIXTURE_SEED, FIXTURE_COUNT, "fixtures", workers)
    evaluate("control-gauntlet", mirror, GAUNTLET_SEED, GAUNTLET_COUNT, "gauntlet", workers)
    evaluate("gauntlet", nets, GAUNTLET_SEED, GAUNTLET_COUNT, "gauntlet", workers, guard=True)
    evaluate("gauntlet-no-guard", nets, GAUNTLET_SEED, GAUNTLET_COUNT, "gauntlet", workers)


def load(name):
    with gzip.open(BASE / name / "cells.json.gz", "rt") as f:
        return json.load(f)


def tables(name):
    per, voids, reasons = {s: {} for s in OPTIMIZERS}, {s: 0 for s in OPTIMIZERS}, {}
    for row in load(name):
        seed = int(row["challenger"].split("-")[1])
        if row["status"] != "PASS":
            voids[seed] += row["games"]
            reason = str(row.get("reason"))[:120]
            reasons[reason] = reasons.get(reason, 0) + row["games"]
            continue
        for duel in row["duels"]:
            per[seed][row["deck0"], row["deck1"], row["seat"], duel["seed"]] = int(duel["winner"] == row["seat"])
    return per, voids, reasons


def wilson_low(wins, games, z=1.96):
    p = wins / games
    denominator = 1 + z * z / games
    return (p + z * z / (2 * games) - z * math.sqrt(p * (1 - p) / games + z * z / (4 * games * games))) / denominator


def learner_deck(key):
    return key[0] if key[2] == 0 else key[1]


def pooled(per, position):
    groups = {}
    for table in per.values():
        for key, value in table.items():
            groups.setdefault(position(key), []).append(value)
    return {str(k): round(100 * float(np.mean(v)), 2) for k, v in sorted(groups.items(), key=lambda item: str(item[0]))}


def block(name, decks_required):
    per, voids, reasons = tables(name)
    rates = [round(100 * float(np.mean(list(per[s].values()))), 2) for s in OPTIMIZERS]
    lows = [round(100 * wilson_low(sum(per[s].values()), len(per[s])), 2) for s in OPTIMIZERS]
    decks, seats = pooled(per, learner_deck), pooled(per, lambda k: k[2])
    fractions = [voids[s] / max(voids[s] + len(per[s]), 1) for s in OPTIMIZERS]
    invalid = any(f > 0.01 for f in fractions)
    by_optimizer_deck = {str(s): pooled({s: per[s]}, learner_deck) for s in OPTIMIZERS}
    checks = {"mean_at_least_55": float(np.mean(rates)) >= 55, "every_lower_bound_above_50": min(lows) > 50, "both_seats_at_least_50": min(seats.values()) >= 50, "valid": not invalid}
    if decks_required:
        checks["every_deck_at_least_50"] = min(decks.values()) >= 50
    return {"by_optimizer": rates, "mean": round(float(np.mean(rates)), 2), "lower_bounds": lows, "games": [len(per[s]) for s in OPTIMIZERS], "void_games": voids, "void_reasons": reasons,
            "by_deck": decks, "by_seat": seats, "by_optimizer_deck": by_optimizer_deck, "checks": checks, "verdict": "PASS" if all(checks.values()) else "FAIL"}, per


def report(output):
    fixtures, _ = block("fixtures", True)
    gauntlet, guarded = block("gauntlet", False)
    bare, free = block("gauntlet-no-guard", False)
    differences = []
    for seed in OPTIMIZERS:
        common = sorted(set(guarded[seed]) & set(free[seed]))
        differences.append({"common_games": len(common), "guard_minus_no_guard": round(100 * float(np.mean([guarded[seed][k] - free[seed][k] for k in common])), 2)})
    summaries = {name: json.loads((BASE / name / "summary.json").read_text()) for name in ("control-fixtures", "fixtures", "control-gauntlet", "gauntlet", "gauntlet-no-guard")}
    mirror = {name: round(100 * summaries[name]["total"]["heuristic-mirror|heuristic"]["win_rate"], 2) for name in ("control-fixtures", "control-gauntlet")}
    result = {"protocol_sha256": hashlib.sha256((EVIDENCE / "protocol.md").read_bytes()).hexdigest(), "verdict": "M1_EXIT_MET" if fixtures["verdict"] == gauntlet["verdict"] == "PASS" else "NOT_MET",
              "F1_fixtures": fixtures, "F2_gauntlet": gauntlet,
              "F4_guard_diagnostic": {"no_guard": {k: bare[k] for k in ("by_optimizer", "mean", "games", "void_games", "void_reasons")}, "paired": differences,
                                      "guard_fires": summaries["gauntlet"]["guard_fires"]},
              "controls": mirror,
              "provenance": {"replayed_cells": sum(s["replayed_cells"] for s in summaries.values()), "replay_mismatches": sum(len(s["replay_mismatches"]) for s in summaries.values()),
                             "native_sha256": sorted({v for s in summaries.values() for k, v in s["native_sha256"].items() if "cpython" in k}),
                             "policy_sha256": summaries["fixtures"]["policy_sha256"], "elapsed_seconds": {k: round(s["elapsed_seconds"]) for k, s in summaries.items()}}}
    output.write_text(json.dumps(result, indent=1) + "\n")
    print(json.dumps(result, indent=1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("run", "report"))
    parser.add_argument("--workers", type=int, default=8)
    args = parser.parse_args()
    expected = (EVIDENCE / "protocol.sha256").read_text().split()[0]
    if hashlib.sha256((EVIDENCE / "protocol.md").read_bytes()).hexdigest() != expected:
        raise SystemExit("Protocol changed after hashing")
    if args.command == "run":
        native = ROOT / "ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so"
        if not hashlib.sha256(native.read_bytes()).hexdigest().startswith(NATIVE):
            raise SystemExit("Native module is not the certified build")
        run(args.workers)
    report(EVIDENCE / "summary.json")


if __name__ == "__main__":
    main()
