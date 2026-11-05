#!/usr/bin/env python3
"""Search against the same network without search, on the gauntlet's neighbouring pairs in both seats.

  pilot      seeds 86100000-86199999: compares settings, to choose what the final test will use
  report D   summarises the games under build/edison-search/D
"""
import argparse
import itertools
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import search_common as sc  # noqa: E402

NETS = ("n1702", "n1703", "n1704")
PILOT_SEED = 86100000
PILOT = {"b64d16k4z1": (64, 16, 4, 1.0), "b64d4k4z1": (64, 4, 4, 1.0), "b64d48k4z1": (64, 48, 4, 1.0), "b64d16k4z0": (64, 16, 4, 0.0), "b64d16k4z2": (64, 16, 4, 2.0),
          "b64d16k8z1": (64, 16, 8, 1.0), "b256d16k4z1": (256, 16, 4, 1.0)}


def cells(names):
    return [(deck0, deck1, seat) for (deck0, deck1), seat in itertools.product(sc.neighbour_pairs(names), (0, 1))]


def wilson(wins, games, z=1.96):
    if not games:
        return (0.0, 0.0)
    p = wins / games
    d = 1 + z * z / games
    c = p + z * z / (2 * games)
    r = z * math.sqrt(p * (1 - p) / games + z * z / (4 * games * games))
    return ((c - r) / d, (c + r) / d)


def summarise(games, plain=None):
    ok = [g for g in games if g["status"] == "OK" and g["winner"] in (0, 1)]
    wins = sum(g["winner"] == g["search_seat"] for g in ok)
    low, high = wilson(wins, len(ok))
    out = {"games": len(games), "finished": len(ok), "void": sum(g["status"] != "OK" for g in games), "draws": sum(g["status"] == "OK" and g["winner"] not in (0, 1) for g in games),
           "search_win_rate": round(100 * wins / max(1, len(ok)), 2), "low": round(100 * low, 2), "high": round(100 * high, 2)}
    for key in ("searched", "departed", "fell_back", "deals_ok", "deals_refused", "rollouts", "worlds_void", "truncated", "net_errors", "net_calls", "option_mismatch", "step_overruns"):
        out[key] = sum(g.get(key, 0) for g in games)
    out["departed_share"] = round(out["departed"] / max(1, out["searched"]), 4)
    out["cpu_per_game"] = round(sum(g.get("total_cpu", 0) for g in games) / max(1, len(games)), 1)
    out["cpu_per_search_ms"] = round(1000 * sum(g.get("search_cpu", 0) for g in games) / max(1, out["searched"]), 1)
    return out


def pilot(args):
    names = sc.write_registry()
    all_cells = cells(names)
    plain_lines = [sc.job_line(f"p{i}", d0, d1, PILOT_SEED + i // 2, -1, NETS[i % 3]) for i, (d0, d1, seat) in enumerate(all_cells)]
    plain = sc.play(plain_lines, sc.OUT / "pilot-plain", args.workers)
    results = {}
    for label in args.configs or PILOT:
        budget, depth, candidates, z = PILOT[label]
        lines = [sc.job_line(f"g{i}", d0, d1, PILOT_SEED + i // 2, seat, NETS[i % 3], budget, depth, candidates, z, 1, 1000 + i, 0) for i, (d0, d1, seat) in enumerate(all_cells)]
        games = sc.play(lines, sc.OUT / f"pilot-{label}", args.workers)
        results[label] = summarise(list(games.values()))
        paired = [(games[f"g{i}"], plain[f"p{i}"]) for i in range(len(all_cells)) if games[f"g{i}"]["status"] == "OK" and plain[f"p{i}"]["status"] == "OK"]
        seat_base = sum(p["winner"] == g["search_seat"] for g, p in paired) / max(1, len(paired))
        results[label]["same_seed_plain_rate"] = round(100 * seat_base, 2)
        print(json.dumps({label: results[label]}), flush=True)
        (sc.OUT / "pilot-summary.json").write_text(json.dumps(results, indent=1) + "\n")


def report(args):
    games = list(sc.load(sc.OUT / args.directory).values())
    print(json.dumps(summarise(games), indent=1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("pilot")
    p.add_argument("--workers", type=int, default=8)
    p.add_argument("--configs", nargs="*")
    p.set_defaults(run=pilot)
    r = commands.add_parser("report")
    r.add_argument("directory")
    r.set_defaults(run=report)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
