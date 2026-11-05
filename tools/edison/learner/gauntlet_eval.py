#!/usr/bin/env python3
"""Evaluate the long gauntlet training run at its fixed checkpoints. Refuses a changed protocol or binary."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASE = ROOT / "build/edison-gauntlet-training-2026-11-04"
EVIDENCE = ROOT / "tests/edison/reports/gauntlet-training-2026-11-04"
OPTIMIZERS = (1702, 1703, 1704)
CHECKPOINTS = (160, 320, 640, 1280)
TOP = 1280
FIXTURE_SEED = 78000000
GAUNTLET_SEED = 78010000
NATIVE = "67423a99970519fb"


def nets(round_index):
    return {f"gauntlet{round_index}-{s}": f"net:{BASE}/runs/gauntlet-{s}/checkpoint-{round_index:04d}" for s in OPTIMIZERS}


def baseline(seed):
    return f"baseline:{ROOT}/tests/edison/reports/rl-budget-2026-11-02/seed-{seed}/final-policy.json"


def call(name, challengers, opponents, seed, count, workers, decks="fixtures", replay=24, guard=False):
    command = [sys.executable, str(HERE / "evaluate.py"), "--output", str(BASE / "eval" / name), "--seed", str(seed),
               "--count", str(count), "--decks", decks, "--workers", str(workers), "--replay-cells", str(replay)]
    for label, spec in challengers.items():
        command += ["--challenger", f"{label}={spec}"]
    for label, spec in opponents.items():
        command += ["--opponent", f"{label}={spec}"]
    if guard:
        command.append("--repeat-guard")
    print(" ".join(command), flush=True)
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workers", type=int, default=8)
    args = parser.parse_args()
    w = args.workers
    expected = (EVIDENCE / "protocol.sha256").read_text().split()[0]
    if hashlib.sha256((EVIDENCE / "protocol.md").read_bytes()).hexdigest() != expected:
        raise SystemExit("Protocol changed after hashing")
    native = ROOT / "ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so"
    if not hashlib.sha256(native.read_bytes()).hexdigest().startswith(NATIVE):
        raise SystemExit("Native module is not the certified build")
    heuristic = {"heuristic": "heuristic"}
    both = {**heuristic, "random": "random"}
    numpy_models = {f"numpy-{s}": baseline(s) for s in OPTIMIZERS}
    call("control-pilot", {"heuristic-mirror": "heuristic"}, heuristic, FIXTURE_SEED, 64, w)
    call("control-random-pilot", {"heuristic-vs-random": "heuristic"}, {"random": "random"}, FIXTURE_SEED, 64, w)
    call("numpy-pilot", numpy_models, both, FIXTURE_SEED, 64, w)
    for round_index in CHECKPOINTS:
        call(f"gauntlet{round_index}-pilot", nets(round_index), both if round_index == TOP else heuristic, FIXTURE_SEED, 64, w)
    for seed in OPTIMIZERS:
        name = f"gauntlet{TOP}-{seed}"
        call(f"gauntlet{TOP}-vs-numpy-pilot-{seed}", {name: nets(TOP)[name]}, {f"numpy-{seed}": baseline(seed)}, FIXTURE_SEED, 64, w)
    call("control-on-gauntlet", {"heuristic-mirror": "heuristic"}, heuristic, GAUNTLET_SEED, 8, w, decks="gauntlet")
    call("numpy-on-gauntlet", numpy_models, heuristic, GAUNTLET_SEED, 8, w, decks="gauntlet")
    for round_index in CHECKPOINTS:
        call(f"gauntlet{round_index}-on-gauntlet", nets(round_index), heuristic, GAUNTLET_SEED, 8, w, decks="gauntlet", guard=True)


if __name__ == "__main__":
    main()
