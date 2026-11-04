#!/usr/bin/env python3
"""Run production heuristic-vs-heuristic duels and export their action traces.

Uses the unmodified audited production worker (tests/edison/agent_match.py) and
reduces each JSONL trace to the fields the native forward-model bench replays:
env seed, chosen action indices, acting player, option counts, and the result.

The seed block starts at 70000000; 21-22M, 30M, 40M, 50M and 60M are in use.
"""
import argparse
import itertools
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/edison"))
from agent_match import run_worker  # noqa: E402

DECKS = ["blackwing", "frog_monarch", "machina_gadget"]
SEED_BLOCK = 70000000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--games-per-pairing", type=int, default=32)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--output", type=Path, default=ROOT / "build/forward_model/games")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    lines = []
    summary = []
    for cell, (d0, d1) in enumerate(itertools.permutations(DECKS, 2)):
        seed = SEED_BLOCK + cell * 1000
        out = args.output / f"cell-{cell}"
        report = run_worker(seed, args.games_per_pairing, args.threads, d0, d1, out,
                            policy0="heuristic", policy1="heuristic")
        games = {}
        terminal = {}
        with (out / "trace-1.jsonl").open() as handle:
            for line in handle:
                rec = json.loads(line)
                if "terminal" in rec:
                    terminal[rec["terminal"]["env"]] = rec["terminal"]
                    continue
                games.setdefault(rec["env"], []).append(rec)
        for env, recs in sorted(games.items()):
            term = terminal[env]
            assert [r["step"] for r in recs] == list(range(len(recs)))
            lines.append(" ".join(map(str, [
                "GAME", d0, d1, seed, env, term["winner"], term["win_reason"], len(recs)])))
            lines.append(" ".join(str(r["action"]) for r in recs))
            lines.append(" ".join(str(r["to_play"]) for r in recs))
            lines.append(" ".join(str(r["options"]) for r in recs))
        summary.append({"cell": cell, "deck0": d0, "deck1": d1, "seed": seed,
                        "games": len(games), "decisions": report["decisions"],
                        "decisions_per_second": report["decisions_per_second"]})
        print(summary[-1], flush=True)
    (args.output / "games.txt").write_text("\n".join(lines) + "\n")
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
