#!/usr/bin/env python3
"""Scoped production soak for the hardening workstream, seed block 74000000 and up.

Runs real lists (the 18 gauntlet candidates and the 3 fixtures) and named targets through
the production module. Each batch is a separate process, so a native abort costs one duel.
Unlike the pool audit, stdout and stderr are kept from every batch, including crashed ones,
and classified with the same marker words as agent_match.run_worker().

Stages (see --stage):
  a2      lists: Main and Extra, every list against the next three, both seats
  sidein  lists: the whole Side moved in, same pairing
  targets one card at a time, random decks built around it from --targets
  uniform random decks from the whole pool, nothing blocked, as in the audit's first stage
"""
import argparse, json, os, random, re, subprocess, sys, time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "pool_audit"))
import build_gauntlet as bg
import gen_decks
import soak as audit
from common import ROOT, FULL_CODES

MARKERS = ("timeout", "Script not found", "Card not found", "Spec not found",
           "cannot find id", "MSG_RETRY", "Error", "stack traceback")
BENIGN = ("Gym has been unmaintained", "Please upgrade to Gymnasium",
          "Users of this version of Gym", "See the migration guide")
FIXTURES = ROOT / "assets/edison/decks"

def lists(pool):
    decks = audit.load_gauntlet(pool)
    for path in sorted(FIXTURES.glob("*.ydk")) + sorted((ROOT / "assets/edison/agent_decks").glob("*.ydk")):
        decks[f"fixture_{path.stem}"] = bg.parse_ydk(path.read_text())
    return decks

def plan_lists(pool, variant, per_pair, seed):
    decks, duels = {}, []
    base = lists(pool)
    names = sorted(base)
    for index, a in enumerate(names):
        for step in (1, 2, 3):
            b = names[(index + step) % len(names)]
            for repeat in range(per_pair):
                rng = random.Random(seed)
                d0, d1 = base[a], base[b]
                if variant == "sidein":
                    d0, d1 = (audit.side_in(d, pool, rng) if d["side"] else d for d in (d0, d1))
                if repeat % 2:
                    d0, d1, first, second = d1, d0, b, a
                else:
                    first, second = a, b
                n0, n1 = f"h{seed}_0", f"h{seed}_1"
                decks[n0], decks[n1] = d0, d1
                duels.append({"id": str(seed), "seed": seed, "deck0": n0, "deck1": n1,
                              "policy": "eager" if repeat % 2 else "uniform",
                              "lists": [first, second]})
                seed += 1
    return duels, decks, seed

def plan_targets(pool, targets, per_target, seed):
    decks, duels = {}, []
    for code in targets:
        entry = pool.by_id[code]
        for _ in range(per_target):
            rng = random.Random(seed)
            weight = lambda e: 1.0
            d0 = gen_decks.targeted_deck(pool, rng, [entry], weight)
            d1 = gen_decks.targeted_deck(pool, rng, [entry], weight)
            n0, n1 = f"h{seed}_0", f"h{seed}_1"
            decks[n0], decks[n1] = d0, d1
            duels.append({"id": str(seed), "seed": seed, "deck0": n0, "deck1": n1,
                          "policy": "eager", "target": code})
            seed += 1
    return duels, decks, seed

def plan_uniform(pool, count, seed):
    decks, duels = {}, []
    for _ in range(count):
        rng = random.Random(seed)
        pair = []
        for side in (0, 1):
            for attempt in range(25):
                try:
                    deck = gen_decks.uniform_deck(pool, rng)
                    break
                except ValueError:
                    continue
            else:
                raise RuntimeError("deck generation failed")
            name = f"h{seed}_{side}"
            decks[name] = deck
            pair.append(name)
        duels.append({"id": str(seed), "seed": seed, "deck0": pair[0], "deck1": pair[1],
                      "policy": "eager" if seed % 2 else "uniform"})
        seed += 1
    return duels, decks, seed

def signature(err, out, kind):
    what = [l for l in err.splitlines() if "what():" in l or "Traceback" in l or "Segmentation" in l]
    tail = [l for l in (err + "\n" + out).splitlines() if l.strip()]
    text = what[-1] if what else (tail[-1] if tail else kind)
    return f"{kind}: {re.sub(r'[0-9]{5,}', 'N', text.strip())[:200]}"

def run_batch(index, stage_dir, duels, decks, module_timeout, so=None, audit_dir=None):
    bdir = stage_dir / f"b{index:05d}"
    bdir.mkdir(parents=True, exist_ok=True)
    paths = {}
    for name in {d for x in duels for d in (x["deck0"], x["deck1"])}:
        paths[name] = bdir / f"{name}.ydk"
        gen_decks.write_ydk(decks[name], paths[name])
    finished, crashes, lines = [], [], Counter()
    remaining, attempt = list(duels), 0
    while remaining:
        attempt += 1
        spec, log = bdir / f"spec{attempt}.json", bdir / f"log{attempt}.jsonl"
        spec.write_text(json.dumps({"decks": {n: str(p) for n, p in paths.items()}, "duels": remaining}))
        cmd = [sys.executable, str(ROOT / "tools/edison/pool_audit/soak_worker.py"), "--spec", str(spec),
               "--log", str(log), "--audit-log", str(bdir / "unused.log"), "--codes", str(FULL_CODES),
               "--module", "production"] + (["--so", str(so)] if so else [])
        kind = None
        env = {**os.environ, "EDISON_PROMPT_AUDIT": str(audit_dir)} if audit_dir else None
        try:
            done = subprocess.run(cmd, cwd=ROOT / "scripts", capture_output=True, text=True,
                                  errors="replace", timeout=module_timeout, env=env)
            out, err, code = done.stdout, done.stderr, done.returncode
        except subprocess.TimeoutExpired as error:
            out = (error.stdout or b"").decode(errors="replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
            err = (error.stderr or b"").decode(errors="replace") if isinstance(error.stderr, bytes) else (error.stderr or "")
            code, kind = None, "timeout"
        for line in (out + "\n" + err).splitlines():
            if line.strip() and not any(line.startswith(b) for b in BENIGN):
                lines[line.strip()[:240]] += 1
        ended, began, last = {}, None, None
        if log.exists():
            for raw in log.read_text(errors="replace").splitlines():
                try: record = json.loads(raw)
                except ValueError: continue
                if "begin" in record: began, last = record["begin"], None
                elif "last" in record: last = record["last"]
                elif "end" in record: ended[record["end"]["id"]] = record["end"]
        finished += list(ended.values())
        unfinished = [d for d in remaining if d["id"] not in ended]
        if code == 0 and kind is None and not unfinished:
            break
        crash = next((d for d in remaining if d["id"] == began and d["id"] not in ended), None)
        crashes.append({"duel": crash["id"] if crash else None, "last": last,
                        "signature": signature(err, out, kind or f"exit{code}"),
                        "lists": crash.get("lists") if crash else None,
                        "target": crash.get("target") if crash else None})
        if crash is None:
            for dead in unfinished:
                crashes.append({"duel": dead["id"], "signature": signature(err, out, "startup"), "last": None,
                                "lists": dead.get("lists"), "target": dead.get("target")})
            break
        remaining = [d for d in unfinished if d["id"] != crash["id"]]
    return finished, crashes, lines

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--stage", choices=["a2", "sidein", "targets", "uniform"], required=True)
    parser.add_argument("--seed", type=int, required=True, help="First seed; the blocks are 74000000 and up, 76000000 and up for the trigger-order work, and 80000000 and up for the Deck label work")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=6)
    parser.add_argument("--batch", type=int, default=20)
    parser.add_argument("--per-pair", type=int, default=2)
    parser.add_argument("--targets", type=Path, help="JSON list of card codes")
    parser.add_argument("--per-target", type=int, default=6)
    parser.add_argument("--count", type=int, default=1000, help="Duels for the uniform stage")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--so", type=Path, help="Run this module file instead of the checkout's, to compare builds")
    parser.add_argument("--prompt-audit", action="store_true",
                        help="Turn on the test-only prompt differential; results land in <output>/prompt-audit")
    args = parser.parse_args()
    args.output = args.output.resolve()  # Workers run with cwd=scripts.
    if not (74000000 <= args.seed < 75000000 or 76000000 <= args.seed < 77000000 or 80000000 <= args.seed < 81000000):
        parser.error("Hardening seeds stay inside the 74000000, 76000000 or 80000000 block")
    if args.workers > 6:
        parser.error("At most 6 workers")
    pool = gen_decks.Pool()
    if args.stage == "targets":
        targets = json.loads(args.targets.read_text())
        duels, decks, next_seed = plan_targets(pool, targets, args.per_target, args.seed)
    elif args.stage == "uniform":
        duels, decks, next_seed = plan_uniform(pool, args.count, args.seed)
    else:
        variant = "sidein" if args.stage == "sidein" else "main"
        duels, decks, next_seed = plan_lists(pool, variant, args.per_pair, args.seed)
    args.output.mkdir(parents=True, exist_ok=True)
    audit_dir = None
    if args.prompt_audit:
        audit_dir = args.output / "prompt-audit"
        audit_dir.mkdir(exist_ok=True)
    batches = [duels[i:i + args.batch] for i in range(0, len(duels), args.batch)]
    started = time.time()
    finished, crashes, lines = [], [], Counter()
    with ThreadPoolExecutor(args.workers) as pool_runner:
        futures = [pool_runner.submit(run_batch, i, args.output, batch, decks, args.timeout, args.so, audit_dir)
                   for i, batch in enumerate(batches)]
        for future in futures:
            done, crashed, output = future.result()
            finished += done; crashes += crashed; lines.update(output)
    statuses = Counter(r["status"] for r in finished)
    violations = Counter()
    for record in finished:
        violations.update(record.get("violations", {}))
    signatures = Counter(c["signature"] for c in crashes)
    prompts = Counter()
    for record in finished:
        prompts.update(record.get("prompts", {}))
    diagnostics = {line: count for line, count in lines.items()
                   if any(m.lower() in line.lower() for m in MARKERS)}
    summary = {"stage": args.stage, "first_seed": args.seed, "last_seed": next_seed - 1,
               "planned": len(duels), "finished": len(finished), "statuses": dict(statuses),
               "crashes": len(crashes), "crash_signatures": dict(signatures.most_common()),
               "diagnostic_lines": diagnostics, "other_output_lines": dict(Counter(
                   {l: c for l, c in lines.items() if l not in diagnostics}).most_common(20)),
               "contract_violations": dict(violations),
               "decisions": sum(r["steps"] for r in finished),
               "prompt_rows_by_msg_id": dict(sorted(prompts.items(), key=lambda kv: int(kv[0]))),
               "max_options": max((r["max_options"] for r in finished), default=0),
               "max_repeat": max((r["max_repeat"] for r in finished), default=0),
               "elapsed_seconds": round(time.time() - started, 1),
               "lists": sorted({n for d in duels for n in d.get("lists", [])})}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=1) + "\n")
    (args.output / "crashes.json").write_text(json.dumps(crashes, indent=0) + "\n")
    print(json.dumps(summary, indent=1))
    clean = not crashes and not diagnostics and not violations and set(statuses) <= {"ok"} and len(finished) == len(duels)
    print("SOAK: " + ("CLEAN" if clean else "NEEDS REVIEW"))
    return 0 if clean else 1

if __name__ == "__main__":
    raise SystemExit(main())
