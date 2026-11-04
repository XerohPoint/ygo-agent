#!/usr/bin/env python3
"""Compare the audit module to the production module, and a pool-only code list to the closure.

(1) Same seeds through both modules must give the same outcome and decision trace.
(2) The pool-only list shows which crashes come from missing resource closure.
"""
import json, random, sys
from collections import Counter
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *
import gen_decks, soak

SEED = 71900000  # Reserved sub-block of 71000000; the soak stages use lower seeds.

def duels_for(pool, count, base, blocked):
    pool.blocked = set(blocked)
    duels, decks = [], {}
    for i in range(count):
        seed = base + i
        rng = random.Random(seed)
        decks[f"e{seed}_0"] = gen_decks.uniform_deck(pool, rng)
        decks[f"e{seed}_1"] = gen_decks.uniform_deck(pool, rng)
        duels.append({"id": str(seed), "seed": seed, "deck0": f"e{seed}_0", "deck1": f"e{seed}_1", "policy": "uniform"})
    return duels, decks

def outcomes(results, crashes):
    table = {rec["id"]: ("ok", rec["trace"], rec["steps"], rec["winner"]) for rec, _ in results}
    table.update({c["duel"]: ("crash", c["signature"]) for c in crashes if c.get("duel")})
    return table

def main():
    pool = gen_decks.Pool()
    q = json.loads((OUT / "quarantine.json").read_text())
    report = {}
    # A: quarantine blocked, so most duels finish and traces can be compared.
    duels, decks = duels_for(pool, 120, SEED, q)
    tmp = OUT / "equivalence"
    a = outcomes(*soak.run_batch(0, tmp / "audit_a", duels, decks, FULL_CODES, "audit", 900)[:2])
    b = outcomes(*soak.run_batch(0, tmp / "prod_a", duels, decks, FULL_CODES, "production", 900)[:2])
    same = sum(a.get(d["id"]) == b.get(d["id"]) for d in duels)
    report["blocked"] = {"duels": len(duels), "identical_outcome_and_trace": same,
                         "ok_in_audit": sum(v[0] == "ok" for v in a.values()), "ok_in_production": sum(v[0] == "ok" for v in b.values()),
                         "mismatches": [d["id"] for d in duels if a.get(d["id"]) != b.get(d["id"])][:10]}
    # B: nothing blocked, so interface crashes must also reproduce in the production binary.
    duels, decks = duels_for(pool, 80, SEED + 1000, [])
    a = outcomes(*soak.run_batch(0, tmp / "audit_b", duels, decks, FULL_CODES, "audit", 900)[:2])
    b = outcomes(*soak.run_batch(0, tmp / "prod_b", duels, decks, FULL_CODES, "production", 900)[:2])
    same = sum(a.get(d["id"]) == b.get(d["id"]) for d in duels)
    report["unblocked"] = {"duels": len(duels), "identical_outcome_and_trace": same,
                           "crash_in_audit": sum(v[0] == "crash" for v in a.values()), "crash_in_production": sum(v[0] == "crash" for v in b.values()),
                           "mismatches": [d["id"] for d in duels if a.get(d["id"]) != b.get(d["id"])][:10]}
    # C: pool-only code list on the production binary.
    duels, decks = duels_for(pool, 400, SEED + 2000, q)
    results, crashes, _ = soak.run_batch(0, tmp / "prod_pool_only", duels, decks, OUT / "codes_pool_only.txt", "production", 1800)
    report["pool_only_list"] = {"duels": len(duels), "ok": len(results),
                                "crash_signatures": Counter(c["signature"] for c in crashes).most_common(40)}
    (OUT / "equivalence.json").write_text(json.dumps(report, indent=1))
    print(json.dumps(report, indent=1)[:3000])

if __name__ == "__main__":
    main()
