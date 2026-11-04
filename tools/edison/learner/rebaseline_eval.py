#!/usr/bin/env python3
"""Replay the frozen learner models on the hardened simulator. Refuses a changed protocol or binary."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
RUNS = ROOT / "build/edison-learner-2026-11-04/pilot"
OUT = ROOT / "build/edison-rebaseline-2026-11-04/eval"
EVIDENCE = ROOT / "tests/edison/reports/rebaseline-2026-11-04"
OPTIMIZERS = (1702, 1703, 1704)
FIXTURE_SEED = 75000000
GAUNTLET_SEED = 75010000
NATIVE = "038e6dd19f4efc17cbe7"


def nets(arm, round_index):
    return {f"{arm}{round_index}-{s}": f"net:{RUNS}/{arm}-{s}/checkpoint-{round_index:04d}" for s in OPTIMIZERS}


def baseline(seed):
    return f"baseline:{ROOT}/tests/edison/reports/rl-budget-2026-11-02/seed-{seed}/final-policy.json"


def call(name, challengers, opponents, seed, count, workers, decks="fixtures", replay=0, guard=False):
    command = [sys.executable, str(HERE / "evaluate.py"), "--output", str(OUT / name), "--seed", str(seed), "--count", str(count),
               "--decks", decks, "--workers", str(workers)]
    for label, spec in challengers.items():
        command += ["--challenger", f"{label}={spec}"]
    for label, spec in opponents.items():
        command += ["--opponent", f"{label}={spec}"]
    if replay:
        command += ["--replay-cells", str(replay)]
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
        raise SystemExit("Native module is not the certified hardened build")
    both = {"heuristic": "heuristic", "random": "random"}
    numpy_models = {f"numpy-{s}": baseline(s) for s in OPTIMIZERS}
    call("control-pilot", {"heuristic-mirror": "heuristic"}, {"heuristic": "heuristic"}, FIXTURE_SEED, 64, w, replay=6)
    call("control-random-pilot", {"heuristic-vs-random": "heuristic"}, {"random": "random"}, FIXTURE_SEED, 64, w, replay=6)
    call("numpy-pilot", numpy_models, both, FIXTURE_SEED, 64, w, replay=24)
    for arm, round_index in (("league", 32), ("league", 160), ("league", 320), ("gauntlet", 160)):
        call(f"{arm}{round_index}-pilot", nets(arm, round_index), both, FIXTURE_SEED, 64, w, replay=24)
    for arm, round_index in (("league", 320), ("gauntlet", 160)):
        for seed in OPTIMIZERS:
            name = f"{arm}{round_index}-{seed}"
            call(f"{arm}{round_index}-vs-numpy-pilot-{seed}", {name: nets(arm, round_index)[name]},
                 {f"numpy-{seed}": baseline(seed)}, FIXTURE_SEED, 64, w, replay=24)
    gauntlet = {"heuristic": "heuristic"}
    call("gauntlet160-on-gauntlet", nets("gauntlet", 160), gauntlet, GAUNTLET_SEED, 8, w, decks="gauntlet", replay=24, guard=True)
    call("league320-on-gauntlet", nets("league", 320), gauntlet, GAUNTLET_SEED, 8, w, decks="gauntlet", replay=24, guard=True)
    call("numpy-on-gauntlet", numpy_models, gauntlet, GAUNTLET_SEED, 8, w, decks="gauntlet", replay=24)
    call("control-on-gauntlet", {"heuristic-mirror": "heuristic"}, gauntlet, GAUNTLET_SEED, 8, w, decks="gauntlet", replay=24)
    print(json.dumps({"finished": True}))


if __name__ == "__main__":
    main()
