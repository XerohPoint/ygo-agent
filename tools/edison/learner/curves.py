#!/usr/bin/env python3
"""Training diagnostics from rounds.jsonl: sampled training win rates in blocks of rounds."""
import json
from pathlib import Path
import sys


def curve(path, block=8):
    rows = [json.loads(line) for line in (Path(path) / "rounds.jsonl").read_text().splitlines()]
    out = []
    for start in range(0, len(rows), block):
        part = rows[start:start + block]
        totals = {}
        for row in part:
            for kind, value in row["sampled_training_results"].items():
                entry = totals.setdefault(kind, [0, 0])
                entry[0] += value["wins"]
                entry[1] += value["games"]
        last = part[-1]["history"][-1]
        out.append({"duels": part[-1]["duels"], **{k: f"{v[0]}/{v[1]}={v[0]/v[1]:.3f}" for k, v in totals.items()},
                    "H": round(last["entropy"], 3), "kl": round(last["approx_kl"], 4), "vmse": round(last["value_mse"], 3),
                    "updates": sum(r["updates"] for r in part), "update_s": round(sum(r["update_seconds"] for r in part)),
                    "collect_s": round(sum(r["collect_seconds"] for r in part)),
                    "decisions": sum(r["decisions"] for r in part), "void": sum(r["void_games"] for r in part)})
    return out


if __name__ == "__main__":
    for path in sys.argv[1:]:
        print(path)
        for row in curve(path):
            print(" ", row)
