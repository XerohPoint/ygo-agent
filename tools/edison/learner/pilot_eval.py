#!/usr/bin/env python3
"""Run the evaluation phases named in the pilot protocol. Refuses to run on a changed protocol.

Each phase calls evaluate.py with a fixed matrix. Output directories are fresh, so a repeat
fails rather than overwriting. Checkpoints are the fixed rounds named here; none is chosen by result.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASE = ROOT / "build/edison-learner-2026-11-04"
RUNS = BASE / "pilot"
OUT = BASE / "eval"
EVIDENCE = ROOT / "tests/edison/reports/learner-2026-11-04"
OPTIMIZERS = (1702, 1703, 1704)
VALIDATION = 40000000
PILOT = 73900000
GAUNTLET_EVAL = 73910000


def net(arm, seed, round_index):
    return f"net:{RUNS}/{arm}-{seed}/checkpoint-{round_index:04d}"


def baseline(seed):
    return f"baseline:{ROOT}/tests/edison/reports/rl-budget-2026-11-02/seed-{seed}/final-policy.json"


def nets(arm, round_index):
    return {f"{arm}{round_index}-{seed}": net(arm, seed, round_index) for seed in OPTIMIZERS}


def baselines():
    return {f"numpy-{seed}": baseline(seed) for seed in OPTIMIZERS}


def call(name, challengers, opponents, seed, count, workers, decks="fixtures", sampled=False, replay=0):
    command = [sys.executable, str(HERE / "evaluate.py"), "--output", str(OUT / name), "--seed", str(seed), "--count", str(count),
               "--decks", decks, "--workers", str(workers)]
    for label, spec in challengers.items():
        command += ["--challenger", f"{label}={spec}"]
    for label, spec in opponents.items():
        command += ["--opponent", f"{label}={spec}"]
    if sampled:
        command.append("--sampled")
    if replay:
        command += ["--replay-cells", str(replay)]
    print(" ".join(command), flush=True)
    subprocess.run(command, check=True)


def head_to_head(name, arm, round_index, seed_value, count, workers):
    for seed in OPTIMIZERS:
        call(f"{name}-{seed}", {f"{arm}{round_index}-{seed}": net(arm, seed, round_index)}, {f"numpy-{seed}": baseline(seed)},
             seed_value, count, workers, replay=24)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("control", "numpy", "equal", "league-curve", "gauntlet", "sampled"))
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--large-round", type=int, default=160, help="Largest league round reached")
    args = parser.parse_args()
    protocol = EVIDENCE / "protocol.md"
    expected = (EVIDENCE / "protocol.sha256").read_text().split()[0]
    if hashlib.sha256(protocol.read_bytes()).hexdigest() != expected:
        raise SystemExit("Protocol changed after hashing")
    both = {"heuristic": "heuristic", "random": "random"}
    w = args.workers
    if args.phase == "control":
        call("control-validation", {"heuristic-mirror": "heuristic"}, {"heuristic": "heuristic"}, VALIDATION, 8, w, replay=6)
        call("control-pilot", {"heuristic-mirror": "heuristic"}, {"heuristic": "heuristic"}, PILOT, 64, w, replay=6)
        call("control-random-pilot", {"heuristic-vs-random": "heuristic"}, {"random": "random"}, PILOT, 64, w)
    elif args.phase == "numpy":
        call("numpy-validation", baselines(), both, VALIDATION, 8, w, replay=12)
        call("numpy-pilot", baselines(), both, PILOT, 64, w, replay=24)
    elif args.phase == "equal":
        for arm in ("league", "schedule"):
            call(f"{arm}32-validation", nets(arm, 32), both, VALIDATION, 8, w, replay=12)
            call(f"{arm}32-pilot", nets(arm, 32), both, PILOT, 64, w, replay=24)
            head_to_head(f"{arm}32-vs-numpy-pilot", arm, 32, PILOT, 64, w)
    elif args.phase == "league-curve":
        # Directories already present mean a smaller checkpoint was done on an earlier call.
        for round_index in (8, 80, 160, args.large_round):
            if (OUT / f"league{round_index}-pilot").exists():
                continue
            if round_index in (8, 80):
                call(f"league{round_index}-pilot", nets("league", round_index), {"heuristic": "heuristic"}, PILOT, 64, w)
            else:
                call(f"league{round_index}-validation", nets("league", round_index), both, VALIDATION, 8, w, replay=12)
                call(f"league{round_index}-pilot", nets("league", round_index), both, PILOT, 64, w, replay=24)
                head_to_head(f"league{round_index}-vs-numpy-pilot", "league", round_index, PILOT, 64, w)
    elif args.phase == "gauntlet":
        trained = nets("gauntlet", 160)  # The gauntlet arm stops at 160 rounds whatever the league reaches.
        call("gauntlet-net-on-gauntlet", trained, {"heuristic": "heuristic"}, GAUNTLET_EVAL, 8, w, decks="gauntlet", replay=64)
        call("league-net-on-gauntlet", nets("league", args.large_round), {"heuristic": "heuristic"}, GAUNTLET_EVAL, 8, w, decks="gauntlet")
        call("numpy-on-gauntlet", baselines(), {"heuristic": "heuristic"}, GAUNTLET_EVAL, 8, w, decks="gauntlet")
        call("control-on-gauntlet", {"heuristic-mirror": "heuristic"}, {"heuristic": "heuristic"}, GAUNTLET_EVAL, 8, w, decks="gauntlet")
        call("gauntlet-net-on-fixtures", trained, both, PILOT, 64, w)
    elif args.phase == "sampled":
        call("league32-sampled-pilot", nets("league", 32), both, PILOT, 64, w, sampled=True)
        call(f"league{args.large_round}-sampled-pilot", nets("league", args.large_round), both, PILOT, 64, w, sampled=True)
        call("numpy-sampled-pilot", baselines(), both, PILOT, 64, w, sampled=True)


if __name__ == "__main__":
    main()
