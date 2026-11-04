#!/usr/bin/env python3
"""Find where two production modules first differ on the same seeded duels.

Runs tests/edison/agent_match.py's worker under a reference .so and under this checkout's
module, then compares the JSONL decision traces. A rule fix should diverge only at a decision
that the fix changes, so the report prints the first differing decision of each duel with
offered cards decoded. Reference modules are only read.
"""
import argparse, importlib.util, json, os, sqlite3, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TESTS = ROOT / "tests/edison"
PROFILE = ROOT / "assets/edison/agent_codes.txt"

def child(args):
    sys.path.insert(0, str(TESTS))
    sys.path.insert(0, str(ROOT / "ygoenv"))
    if args.so:
        spec = importlib.util.spec_from_file_location("ygoenv.ygopro.ygopro_ygoenv", args.so)
        module = importlib.util.module_from_spec(spec)
        sys.modules["ygoenv.ygopro.ygopro_ygoenv"] = module
        spec.loader.exec_module(module)
    import agent_match
    worker_args = argparse.Namespace(worker=1, seed=args.seed, num_envs=args.count, threads=1, deck1=args.deck0,
        deck2=args.deck1, policy0=args.policy0, policy1=args.policy1, model=None, collect=False,
        rl_rollout=False, verbose=False, output=args.output)
    args.output.mkdir(parents=True, exist_ok=True)
    agent_match.worker(worker_args)

def records(path):
    per_env = {}
    for line in path.read_text().splitlines():
        record = json.loads(line)
        if "terminal" in record:
            per_env.setdefault(record["terminal"]["env"], []).append(record)
        else:
            per_env.setdefault(record["env"], []).append(record)
    return per_env

def describe(offered, names):
    rows = []
    for a in offered:
        cid = a[1] * 256 + a[2]
        rows.append(f"msg={a[3]} act={a[4]} {names.get(cid, cid)}")
    return rows

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, help="Reference production .so")
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--count", type=int, default=8)
    parser.add_argument("--deck0", default="blackwing")
    parser.add_argument("--deck1", default="frog_monarch")
    parser.add_argument("--policy0", default="heuristic")
    parser.add_argument("--policy1", default="random")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--child", action="store_true")
    parser.add_argument("--so", type=Path)
    args = parser.parse_args()
    args.output = args.output.resolve()
    if args.child:
        child(args); return 0
    runs = {}
    for label, so in (("reference", args.reference.resolve()), ("current", None)):
        output = args.output / label
        cmd = [sys.executable, str(Path(__file__).resolve()), "--child", "--seed", str(args.seed), "--count", str(args.count),
               "--deck0", args.deck0, "--deck1", args.deck1, "--policy0", args.policy0, "--policy1", args.policy1,
               "--output", str(output)] + (["--so", str(so)] if so else [])
        subprocess.run(cmd, cwd=ROOT / "scripts", check=True, capture_output=True, text=True, timeout=300)
        runs[label] = records(output / "trace-1.jsonl")
    db = sqlite3.connect(ROOT / "assets/locale/en/cards.cdb")
    names = {}
    for cid, line in enumerate(PROFILE.read_text().splitlines(), 1):
        row = db.execute("select name from texts where id=?", (int(line.split()[0]),)).fetchone()
        names[cid] = row[0] if row else line.split()[0]
    report = {"seed": args.seed, "count": args.count, "deck0": args.deck0, "deck1": args.deck1, "duels": []}
    for env in sorted(runs["reference"]):
        old, new = runs["reference"][env], runs["current"][env]
        entry = {"env": env, "reference_steps": len(old) - 1, "current_steps": len(new) - 1, "identical": True}
        for index, (a, b) in enumerate(zip(old, new)):
            if a != b:
                entry.update(identical=False, step=index, to_play=a.get("to_play"),
                             reference=describe(a.get("offered", []), names), current=describe(b.get("offered", []), names),
                             only_in_current=[r for r in describe(b.get("offered", []), names) if r not in describe(a.get("offered", []), names)],
                             only_in_reference=[r for r in describe(a.get("offered", []), names) if r not in describe(b.get("offered", []), names)])
                break
        report["duels"].append(entry)
    (args.output / "divergence.json").write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps(report, indent=1))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
