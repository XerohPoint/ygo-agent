#!/usr/bin/env python3
"""Bounded production duels with fixed legal decks and a uniform-random agent."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
DECKS = ROOT / "assets/edison/decks"
MAX_STEPS = 10000
MAX_OPTIONS = 256

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def worker(args):
    import numpy as np
    import ygoenv
    from ygoenv.ygopro import init_module
    init_module(str(ROOT / "assets/locale/en/cards.cdb"),
                str(ROOT / "assets/edison/smoke_codes.txt"),
                {p.stem: str(p) for p in sorted(DECKS.glob("*.ydk"))})
    env = ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=args.num_envs,
                     num_threads=args.threads or args.num_envs, seed=args.seed,
                     deck1=args.deck1, deck2=args.deck2, play_mode="self",
                     player=-1, async_reset=False, greedy_reward=False,
                     max_options=MAX_OPTIONS, max_steps=MAX_STEPS,
                     max_episode_steps=MAX_STEPS, timeout=10, verbose=args.verbose)
    rng = [random.Random((args.seed+i) ^ 0xED150) for i in range(args.num_envs)]
    hashes = [hashlib.sha256() for _ in rng]
    steps = [0 for _ in rng]
    results = {}
    card_ids = {i+1: int(line.split()[0]) for i, line in enumerate(
        (ROOT / "assets/edison/smoke_codes.txt").read_text().splitlines())}
    interactions = [{} for _ in rng]
    prompt_counts = {}
    max_choices = 0
    longest_step = 0.0
    start = time.perf_counter()
    obs, info = env.reset()
    with (args.output / f"trace-{args.worker}.jsonl").open("w", buffering=1) as log:
        while len(results) < args.num_envs:
            ids = np.asarray(info["env_id"], dtype=np.int32)
            actions, previous = [], {}
            for row, eid in enumerate(ids.tolist()):
                count = int(info["num_options"][row])
                assert 0 < count < MAX_OPTIONS, ("empty/truncated action space", eid, count)
                max_choices = max(max_choices, count)
                player = int(info["to_play"][row])
                assert player in (0, 1)
                action = rng[eid].randrange(count)
                previous[eid] = player
                actions.append(action)
                # Ignore padding/history buffers. Hash all visible card features,
                # global game features, and the exact offered action rows.
                state_hash = hashlib.sha256()
                for key in ("cards_", "global_"):
                    state_hash.update(np.ascontiguousarray(obs[key][row]).tobytes())
                offered = np.ascontiguousarray(obs["actions_"][row, :count])
                state_hash.update(offered.tobytes())
                chosen = offered[action]
                code = card_ids.get(int(chosen[1])*256+int(chosen[2]), 0)
                interaction = f"{code}:{int(chosen[3])}:{int(chosen[4])}"
                interactions[eid][interaction] = interactions[eid].get(interaction, 0)+1
                record = {"env": eid, "step": steps[eid], "to_play": player,
                          "options": count, "action": action,
                          "offered": offered.tolist(), "state": state_hash.hexdigest()}
                line = json.dumps(record, sort_keys=True, separators=(",", ":"))
                hashes[eid].update(line.encode())
                log.write(line + "\n")
                msg = str(int(offered[0, 3]))
                prompt_counts[msg] = prompt_counts.get(msg, 0) + 1
                steps[eid] += 1
                assert steps[eid] < MAX_STEPS, ("decision limit; not a completed duel", eid)
            before = time.perf_counter()
            obs, rewards, terminated, truncated, info = env.step(np.asarray(actions), ids)
            longest_step = max(longest_step, time.perf_counter()-before)
            assert not np.any(truncated), "Environment truncation is not a natural win"
            keep = []
            for row, eid in enumerate(np.asarray(info["env_id"]).tolist()):
                if terminated[row]:
                    reward = float(rewards[row])
                    assert reward in (-1.0, 1.0), ("invalid terminal reward", reward)
                    results[eid] = {"env": eid, "seed": args.seed+eid, "steps": steps[eid],
                                    "winner": previous[eid] if reward > 0 else 1-previous[eid],
                                    "win_reason": int(info["win_reason"][row]),
                                    "trace_sha256": hashes[eid].hexdigest(),
                                    "interactions": dict(sorted(interactions[eid].items()))}
                    log.write(json.dumps({"terminal": results[eid]}, sort_keys=True)+"\n")
                else:
                    assert float(rewards[row]) == 0
                    keep.append(row)
            if keep:
                obs = {k: v[keep] for k, v in obs.items()}
                info = {k: info[k][keep] for k in ("env_id", "num_options", "to_play", "win_reason")}
    elapsed = time.perf_counter()-start
    report = {"status": "PASS", "duels": [results[i] for i in sorted(results)],
              "decisions": sum(steps), "elapsed_seconds": elapsed,
              "decisions_per_second": sum(steps)/elapsed,
              "duels_per_second": len(results)/elapsed,
              "longest_step_seconds": longest_step, "max_options_seen": max_choices,
              "prompt_counts": prompt_counts}
    (args.output / f"worker-{args.worker}.json").write_text(json.dumps(report, indent=2)+"\n")

def run_worker(seed, num_envs, threads, deck1, deck2, output, run=1, verbose=False):
    output.mkdir(parents=True, exist_ok=True)
    cmd = [sys.executable, str(Path(__file__).resolve()), "--worker", str(run),
           "--seed", str(seed), "--num-envs", str(num_envs), "--threads", str(threads),
           "--deck1", deck1, "--deck2", deck2, "--output", str(output)]
    if verbose:
        cmd.append("--verbose")
    process = subprocess.run(cmd, cwd=ROOT/"scripts", capture_output=True,
                             text=True, timeout=180)
    (output/f"stdout-{run}.txt").write_text(process.stdout)
    (output/f"stderr-{run}.txt").write_text(process.stderr)
    if process.returncode:
        raise RuntimeError(f"Worker {run} exited {process.returncode}: "+
                           process.stderr[-2500:]+process.stdout[-2500:])
    bad = ("timeout", "Script not found", "Card not found", "Spec not found",
           "cannot find id", "MSG_RETRY", "Error", "stack traceback")
    if any(marker.lower() in (process.stdout+process.stderr).lower() for marker in bad):
        raise RuntimeError("Native diagnostic detected; inspect stdout/stderr")
    return json.loads((output/f"worker-{run}.json").read_text())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deck1", choices=("blackwing", "frog_monarch"), default="blackwing")
    parser.add_argument("--deck2", choices=("blackwing", "frog_monarch"), default="frog_monarch")
    parser.add_argument("--num-envs", type=int, default=16)
    parser.add_argument("--threads", type=int)
    parser.add_argument("--seed", type=int, default=20100424)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--output", type=Path, default=ROOT / "build/edison-smoke")
    parser.add_argument("--worker", type=int)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.worker is not None:
        worker(args)
        return 0
    if args.num_envs < 1 or args.repeats < 2:
        parser.error("Require positive num-envs and at least two fresh runs")
    report = {"status": "FAIL", "policy": "uniform_random_legal_option",
              "seed": args.seed, "num_envs": args.num_envs,
              "threads": args.threads or args.num_envs,
              "deck1": args.deck1, "deck2": args.deck2, "runs": []}
    try:
        lock = json.loads((ROOT / "tests/edison/smoke_assets.lock.json").read_text())
        for path, expected in lock.items():
            if digest(ROOT / path) != expected:
                raise RuntimeError("Smoke asset hash mismatch: "+path)
        report["assets_sha256"] = lock
        report["source_sha256"] = digest(Path(__file__))
        binary = next((ROOT / "ygoenv/ygoenv/ygopro").glob("ygopro_ygoenv*.so"))
        report["production_binary_sha256"] = digest(binary)
        baseline = None
        for run in range(1, args.repeats+1):
            result = run_worker(args.seed, args.num_envs, args.threads or args.num_envs,
                                args.deck1, args.deck2, args.output, run, args.verbose)
            report["runs"].append(result)
            if baseline is None:
                baseline = result["duels"]
            elif baseline != result["duels"]:
                raise RuntimeError("Fresh seeded production runs differ")
            print(f"Run {run}: {len(result['duels'])} complete duels; {result['decisions']} decisions; "
                  f"{result['decisions_per_second']:.0f} decisions/s", flush=True)
        report["repeatability"] = True
        report["completion_rate"] = 1.0
        report["p0_wins"] = sum(d["winner"] == 0 for d in baseline)
        report["p1_wins"] = sum(d["winner"] == 1 for d in baseline)
        report["status"] = "PASS"
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
        print("FAIL:", error, flush=True)
    (args.output/"report.json").write_text(json.dumps(report, indent=2)+"\n")
    return int(report["status"] != "PASS")

if __name__ == "__main__":
    raise SystemExit(main())
