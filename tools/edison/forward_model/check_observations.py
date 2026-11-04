#!/usr/bin/env python3
"""Check native observation bytes against the sha256 in the production trace.

The production worker hashes cards_, global_, decision_, selection_ and the offered
action rows for every decision. If the native test bed produces the same digests,
it is driving the production environment, not an approximation of it.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MAX_CARDS = 80
FIXED = (2 * MAX_CARDS * 41) + 23 + (12 * 4) + (2 * MAX_CARDS * 3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", type=Path, required=True)
    parser.add_argument("--games-per-cell", type=int, required=True)
    parser.add_argument("--trace", type=Path, default=ROOT / "build/forward_model/games/cell-0/trace-1.jsonl")
    args = parser.parse_args()
    games = {}
    for line in args.trace.open():
        rec = json.loads(line)
        if "terminal" not in rec and rec["env"] < args.games_per_cell:
            games.setdefault(rec["env"], []).append(rec)
    data = args.dump.read_bytes()
    offset = checked = bad = 0
    for env in sorted(games):
        for rec in games[env]:
            size = FIXED + rec["options"] * 12
            digest = hashlib.sha256(data[offset:offset + size]).hexdigest()
            offset += size
            checked += 1
            bad += digest != rec["state"]
    print(json.dumps({"observation_check": True, "decisions": checked, "mismatches": bad,
                      "bytes_left_over": len(data) - offset}))
    return 1 if bad or offset != len(data) else 0


if __name__ == "__main__":
    raise SystemExit(main())
