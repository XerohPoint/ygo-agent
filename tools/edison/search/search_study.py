#!/usr/bin/env python3
"""Search against the same network without search, on the gauntlet's neighbouring pairs in both seats.

  pilot      seeds 86100000-86199999: compares settings, to choose what the final test will use
  cross      search on one net against another net without search, beside the same two nets without search
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
          "b64d16k8z1": (64, 16, 8, 1.0), "b256d16k4z1": (256, 16, 4, 1.0),
          "b64d96k4z1": (64, 96, 4, 1.0), "b64d256k4z1": (64, 256, 4, 1.0)}


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


def calibration(games):
    """Estimated win rate of the option played against how those games ended. Decisions in one game are not independent."""
    bins = [[0, 0.0, 0.0] for _ in range(10)]
    for g in games:
        if g["status"] != "OK" or g["winner"] not in (0, 1):
            continue
        won = g["winner"] == g["search_seat"]
        for row in g.get("log", []):
            if row[2] < 2:
                continue
            estimate = (row[7] + 1) / 2
            b = bins[min(9, int(estimate * 10))]
            b[0] += 1
            b[1] += estimate
            b[2] += won
    return [{"bin": i / 10, "decisions": n, "estimate": round(100 * e / n, 1), "won": round(100 * w / n, 1)} for i, (n, e, w) in enumerate(bins) if n]


def splits(games):
    out = {}
    ok = [g for g in games if g["status"] == "OK" and g["winner"] in (0, 1)]
    for label, key in (("by_seat", lambda g: g["search_seat"]), ("by_net", lambda g: g["_job"][5]), ("by_search_deck", lambda g: g["_job"][1 + g["search_seat"]])):
        table = {}
        for g in ok:
            t = table.setdefault(str(key(g)), [0, 0])
            t[0] += g["winner"] == g["search_seat"]
            t[1] += 1
        out[label] = {k: {"games": n, "rate": round(100 * w / n, 1)} for k, (w, n) in sorted(table.items())}
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


def cross(args):
    """The rollouts play the opponent with the search's own net, which here is not the net it faces."""
    names = sc.write_registry()
    all_cells = cells(names)
    base = args.seed
    tag = args.tag
    plain_lines = []
    for i, (d0, d1, seat) in enumerate(all_cells):
        a, b = NETS[i % 3], NETS[(i + 1) % 3]
        plain_lines.append(sc.job_line(f"p{i}", d0, d1, base + i // 2, -1, a if seat == 0 else b, opponent_net=b if seat == 0 else a))
    plain = sc.play(plain_lines, sc.OUT / f"{tag}-plain", args.workers)
    results = {}
    for label in args.configs:
        budget, depth, candidates, z = PILOT[label]
        lines = [sc.job_line(f"g{i}", d0, d1, base + i // 2, seat, NETS[i % 3], budget, depth, candidates, z, 1, 2000 + i, 0, NETS[(i + 1) % 3]) for i, (d0, d1, seat) in enumerate(all_cells)]
        games = sc.play(lines, sc.OUT / f"{tag}-{label}", args.workers)
        results[label] = summarise(list(games.values()))
        paired = [(games[f"g{i}"], plain[f"p{i}"]) for i in range(len(all_cells)) if games[f"g{i}"]["status"] == "OK" and plain[f"p{i}"]["status"] == "OK"]
        results[label]["same_seed_plain_rate"] = round(100 * sum(p["winner"] == g["search_seat"] for g, p in paired) / max(1, len(paired)), 2)
        print(json.dumps({label: results[label]}), flush=True)
        (sc.OUT / f"{tag}-summary.json").write_text(json.dumps(results, indent=1) + "\n")


def report(args):
    played = sc.load(sc.OUT / args.directory)
    jobs = {line.split("\t")[0]: line.split("\t") for line in (sc.OUT / (args.directory + ".tsv")).read_text().splitlines()}
    games = [dict(g, _job=jobs[k]) for k, g in played.items()]
    print(json.dumps({**summarise(games), **splits(games), "calibration": calibration(games)}, indent=1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("pilot")
    p.add_argument("--workers", type=int, default=8)
    p.add_argument("--configs", nargs="*")
    p.set_defaults(run=pilot)
    c = commands.add_parser("cross")
    c.add_argument("--workers", type=int, default=8)
    c.add_argument("--configs", nargs="+", required=True)
    c.add_argument("--seed", type=int, required=True)
    c.add_argument("--tag", required=True)
    c.set_defaults(run=cross)
    r = commands.add_parser("report")
    r.add_argument("directory")
    r.set_defaults(run=report)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
