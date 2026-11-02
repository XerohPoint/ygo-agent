#!/usr/bin/env python3
"""Reproduce the fixed-deck random-agent baseline and scheduling controls."""
import argparse
import hashlib
import json
import platform
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT/"build/edison-baseline")
    parser.add_argument("--seed", type=int, default=20100424)
    parser.add_argument("--num-envs", type=int, default=16)
    args = parser.parse_args()
    if args.num_envs < 2:
        parser.error("Use at least two environments to verify parallel execution")
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    report = {"status": "FAIL", "seed": args.seed, "policy": "uniform_random_legal_option",
              "num_envs": args.num_envs, "python": platform.python_version(),
              "platform": platform.platform(), "matchups": [], "controls": {}}
    started = time.perf_counter()

    def run(label, a, b, count, threads):
        output = args.output/label
        command = [sys.executable, str(ROOT/"tests/edison/smoke.py"), "--seed", str(args.seed),
                   "--num-envs", str(count), "--threads", str(threads), "--deck1", a, "--deck2", b,
                   "--output", str(output)]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=400)
        (args.output/(label+".txt")).write_text(result.stdout+result.stderr)
        if result.returncode:
            raise RuntimeError(f"{label} failed: "+result.stdout[-2500:]+result.stderr[-1000:])
        data = json.loads((output/"report.json").read_text())
        assert data["status"] == "PASS" and data["repeatability"]
        print(f"{label}: PASS, {count} duels per run", flush=True)
        return data

    try:
        for a,b in [("blackwing","blackwing"),("blackwing","frog_monarch"),
                    ("frog_monarch","blackwing"),("frog_monarch","frog_monarch")]:
            report["matchups"].append(run(f"{a}-{b}", a,b,args.num_envs,args.num_envs))
        reference = report["matchups"][1]["runs"][0]["duels"]
        serial = run("serial", "blackwing", "frog_monarch", 1,1)
        single = run("single-thread", "blackwing", "frog_monarch", args.num_envs,1)
        assert serial["runs"][0]["duels"][0] == reference[0], "Serial/parallel environment 0 differs"
        assert single["runs"][0]["duels"] == reference, "Worker scheduling changes seeded duels"
        report["controls"] = {"serial": serial, "single_thread": single,
                              "serial_parallel_agree": True, "thread_counts_agree": True}
        report["unique_seeded_matchup_duels"] = 4*args.num_envs
        report["completed_execution_count"] = 2*(5*args.num_envs+1)
        report["completion_rate"] = 1.0
        report["decision_limits_hit"] = report["timeouts"] = report["crashes"] = 0
        report["elapsed_including_process_startup_seconds"] = time.perf_counter()-started
        report["source_sha256"] = {
            str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in [Path(__file__), ROOT/"tests/edison/smoke.py"]}
        report["status"] = "PASS"
    except (AssertionError, RuntimeError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
        print("FAIL:", error)
    (args.output/"report.json").write_text(json.dumps(report, indent=2)+"\n")
    return int(report["status"] != "PASS")

if __name__ == "__main__":
    raise SystemExit(main())
