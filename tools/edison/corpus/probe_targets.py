#!/usr/bin/env python3
"""Crash probe for named cards: both decks generated around one target, as the hardening
"targets" stage builds them (gen_decks.targeted_deck), played through the corpus soak path.

Used to test a crash lead from the corpus soak. These decks are generated, not corpus lists.
With --pair A.ydk B.ydk it instead plays --per-target duels between two given files, seats
alternating by seed, to compare a corpus pair with an edited copy of it.
Seeds stay in 88000000-88999999. Policies rotate uniform, eager, heuristic by seed.
"""
import argparse, json, random, sys, time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "pool_audit"))
from corpus_common import SEED_FIRST, SEED_LAST  # noqa: E402
import common as audit_common  # noqa: E402
import gen_decks  # noqa: E402
import importlib.util  # noqa: E402

# Load this directory's soak.py by path: pool_audit also has a soak.py, earlier on sys.path.
_spec = importlib.util.spec_from_file_location("corpus_soak", HERE / "soak.py")
soak = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(soak)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--targets", type=int, nargs="+", default=[])
    parser.add_argument("--pair", type=Path, nargs=2, help="Two .ydk files to play against each other instead")
    parser.add_argument("--per-target", type=int, default=3000)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=12)
    parser.add_argument("--batch", type=int, default=20)
    args = parser.parse_args()
    args.output = args.output.resolve()
    if args.workers > 12:
        parser.error("At most 12 workers")
    if args.pair:
        args.targets = [0]  # one arm, keyed 0
    last = args.seed + len(args.targets) * args.per_target - 1
    if not (SEED_FIRST <= args.seed and last <= SEED_LAST):
        parser.error("Seeds stay inside 88000000-88999999")
    pool = gen_decks.Pool()
    decks, duels, seed = {}, [], args.seed
    from corpus_common import parse_ydk_text
    pair = [parse_ydk_text(path.read_text()) for path in args.pair] if args.pair else None
    for code in args.targets:
        entry = None if pair else pool.by_id[code]
        for _ in range(args.per_target):
            rng = random.Random(seed)
            n0, n1 = f"t{seed}_0", f"t{seed}_1"
            if pair:
                decks[n0], decks[n1] = (pair[0], pair[1]) if seed % 2 == 0 else (pair[1], pair[0])
            else:
                decks[n0] = gen_decks.targeted_deck(pool, rng, [entry], lambda e: 1.0)
                decks[n1] = gen_decks.targeted_deck(pool, rng, [entry], lambda e: 1.0)
            duels.append({"id": str(seed), "seed": seed, "deck0": n0, "deck1": n1,
                          "policy": soak.POLICIES[seed % 3], "lists": [f"target{code}", f"target{code}"], "target": code})
            seed += 1
    args.output.mkdir(parents=True, exist_ok=True)
    batches = [duels[i:i + args.batch] for i in range(0, len(duels), args.batch)]
    started = time.time()
    finished, crashes = [], []
    with ThreadPoolExecutor(args.workers) as runner:
        futures = [runner.submit(soak.run_batch, i, args.output, b, decks, 900, audit_common.FULL_CODES.resolve(),
                                 (soak.ROOT / "scripts").resolve()) for i, b in enumerate(batches)]
        for future in futures:
            done, crashed, _, _ = future.result()
            finished += done; crashes += crashed
    target_of = {d["id"]: d["target"] for d in duels}
    rows = {}
    for code in args.targets:
        mine = [c for c in crashes if target_of.get(c["duel"]) == code]
        rows[str(code)] = {"duels": args.per_target, "finished": sum(target_of[r["id"]] == code for r in finished),
                           "statuses": dict(Counter(r["status"] for r in finished if target_of[r["id"]] == code)),
                           "crashes": len(mine), "signatures": dict(Counter(c["signature"] for c in mine)),
                           "crash_seeds": sorted(c["seed"] for c in mine)}
    summary = {"first_seed": args.seed, "last_seed": last, "per_target": args.per_target, "targets": rows,
               "pair": [str(p) for p in args.pair] if args.pair else None,
               "elapsed_seconds": round(time.time() - started, 1)}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=1) + "\n")
    (args.output / "crashes.json").write_text(json.dumps(crashes, indent=0) + "\n")
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
