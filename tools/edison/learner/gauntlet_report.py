#!/usr/bin/env python3
"""Tables and predeclared conclusions G1-G5 from the long gauntlet run. Games in a void cell never count."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from curves import curve  # noqa: E402

ROOT = HERE.parents[2]
BASE = ROOT / "build/edison-gauntlet-training-2026-11-04"
EVAL = BASE / "eval"
EVIDENCE = ROOT / "tests/edison/reports/gauntlet-training-2026-11-04"
OPTIMIZERS = (1702, 1703, 1704)
CHECKPOINTS = (160, 320, 640, 1280)
BLOCKS = {"fixtures": "pilot", "gauntlet": "on-gauntlet"}


def load(name):
    with gzip.open(EVAL / name / "cells.json.gz", "rt") as f:
        return json.load(f)


def outcomes(rows, challenger, opponent):
    table, void, reasons = {}, 0, {}
    for row in rows:
        if row["challenger"] != challenger or row["opponent"] != opponent:
            continue
        if row["status"] != "PASS":
            void += row["games"]
            reason = str(row.get("reason"))[:120]
            reasons[reason] = reasons.get(reason, 0) + row["games"]
            continue
        for duel in row["duels"]:
            table[row["deck0"], row["deck1"], row["seat"], duel["seed"]] = int(duel["winner"] == row["seat"])
    return table, void, reasons


def pct(table):
    return round(100 * float(np.mean(list(table.values()))), 2) if table else None


def arm(rows, label, opponent):
    per, void, reasons = {}, 0, {}
    for seed in OPTIMIZERS:
        table, voided, why = outcomes(rows, f"{label}-{seed}", opponent)
        per[seed], void = table, void + voided
        for reason, games in why.items():
            reasons[reason] = reasons.get(reason, 0) + games
    rates = [pct(per[s]) for s in OPTIMIZERS]
    games = sum(len(t) for t in per.values())
    fraction = void / max(games + void, 1)
    return {"by_optimizer": rates, "mean": round(float(np.mean(rates)), 2), "wins": sum(sum(t.values()) for t in per.values()), "games": games, "void_games": void,
            "void_reasons": reasons, "validity": "INVALID" if fraction > 0.01 else "VALID"}, per


def learner_deck(key):
    return key[0] if key[2] == 0 else key[1]


def paired_gain(net, base):
    gains = []
    for seed in OPTIMIZERS:
        common = sorted(set(net[seed]) & set(base[seed]))
        gains.append(round(100 * float(np.mean([net[seed][k] - base[seed][k] for k in common])), 2))
    return gains


def split(per, position):
    groups = {}
    for table in per.values():
        for key, value in table.items():
            groups.setdefault(position(key), []).append(value)
    return {str(k): round(100 * float(np.mean(v)), 2) for k, v in sorted(groups.items(), key=lambda item: str(item[0]))}


def bootstrap_matchups(per, rounds=2000):
    """Descriptive interval for the mean of optimizers: resample deck pairs, since games in a pair share decks."""
    rng = np.random.default_rng(0)
    pairs = sorted({(k[0], k[1]) for t in per.values() for k in t}, key=str)
    index = {pair: i for i, pair in enumerate(pairs)}
    wins, games = np.zeros((len(OPTIMIZERS), len(pairs))), np.zeros((len(OPTIMIZERS), len(pairs)))
    for row, seed in enumerate(OPTIMIZERS):
        for key, value in per[seed].items():
            wins[row, index[key[0], key[1]]] += value
            games[row, index[key[0], key[1]]] += 1
    means = []
    for _ in range(rounds):
        pick = rng.integers(0, len(pairs), len(pairs))
        means.append(float(np.mean(wins[:, pick].sum(axis=1) / games[:, pick].sum(axis=1))))
    low, high = np.percentile(means, [2.5, 97.5])
    return [round(100 * float(low), 2), round(100 * float(high), 2)]


def training(seed):
    path = BASE / "runs" / f"gauntlet-{seed}"
    rows = [json.loads(line) for line in (path / "rounds.jsonl").read_text().splitlines()]
    return {"rounds": len(rows), "duels": rows[-1]["duels"], "void_games": sum(r["void_games"] for r in rows), "wall_hours": round(sum(r["wall_seconds"] for r in rows) / 3600, 2),
            "collect_hours": round(sum(r["collect_seconds"] for r in rows) / 3600, 2), "update_hours": round(sum(r["update_seconds"] for r in rows) / 3600, 2),
            "updates": sum(r["updates"] for r in rows), "final_params_sha256": rows[-1]["params_sha256"], "curve": curve(path, block=160)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    expected = (EVIDENCE / "protocol.sha256").read_text().split()[0]
    report = {"protocol_sha256": hashlib.sha256((EVIDENCE / "protocol.md").read_bytes()).hexdigest(), "protocol_expected": expected}
    tables, blocks = {}, {}
    for block, suffix in BLOCKS.items():
        blocks[block] = {}
        for round_index in CHECKPOINTS:
            label = f"gauntlet{round_index}"
            blocks[block][label], tables[block, round_index] = arm(load(f"{label}-{suffix}"), label, "heuristic")
        name = "numpy-pilot" if block == "fixtures" else "numpy-on-gauntlet"
        blocks[block]["numpy"], tables[block, "numpy"] = arm(load(name), "numpy", "heuristic")
    report["blocks"] = blocks
    report["fixtures_vs_random"], _ = arm(load("gauntlet1280-pilot"), "gauntlet1280", "random")
    report["numpy_vs_random"], _ = arm(load("numpy-pilot"), "numpy", "random")
    h2h, h2h_void = [], 0
    for seed in OPTIMIZERS:
        table, void, _ = outcomes(load(f"gauntlet1280-vs-numpy-pilot-{seed}"), f"gauntlet1280-{seed}", f"numpy-{seed}")
        h2h.append(pct(table))
        h2h_void += void
    report["head_to_head"] = {"by_optimizer": h2h, "mean": round(float(np.mean(h2h)), 2), "void_games": h2h_void}
    report["controls"] = {"fixture_mirror": pct(outcomes(load("control-pilot"), "heuristic-mirror", "heuristic")[0]),
                          "fixture_heuristic_vs_random": pct(outcomes(load("control-random-pilot"), "heuristic-vs-random", "random")[0]),
                          "gauntlet_mirror": pct(outcomes(load("control-on-gauntlet"), "heuristic-mirror", "heuristic")[0])}
    conclusions = {"G1": {}, "G2": {}, "G4": {}}
    for block in BLOCKS:
        top, start, middle = (blocks[block][f"gauntlet{r}"] for r in (1280, 160, 640))
        diff = [round(a - b, 2) for a, b in zip(top["by_optimizer"], start["by_optimizer"])]
        conclusions["G1"][block] = {"by_optimizer": diff, "mean": round(float(np.mean(diff)), 2),
                                    "verdict": "MORE_GAMES_HELP" if np.mean(diff) >= 5 and min(diff) > 0 else "NOT_SHOWN"}
        recent = round(top["mean"] - middle["mean"], 2)
        conclusions["G2"][block] = {"mean": recent, "by_optimizer": [round(a - b, 2) for a, b in zip(top["by_optimizer"], middle["by_optimizer"])],
                                    "verdict": "STILL_RISING" if recent >= 2 else "FLAT_OR_NOISE"}
        gain = paired_gain(tables[block, 1280], tables[block, "numpy"])
        conclusions["G4"][block] = {"gain_by_optimizer": gain, "mean_gain": round(float(np.mean(gain)), 2)}
    ahead = all(conclusions["G4"][b]["mean_gain"] > 0 for b in BLOCKS) and report["head_to_head"]["mean"] >= 50
    conclusions["G4"].update({"head_to_head_mean": report["head_to_head"]["mean"], "verdict": "NET_AHEAD" if ahead else "NOT_SHOWN"})
    rows = {b: blocks[b]["gauntlet1280"] for b in BLOCKS}
    candidate = all(r["mean"] >= 55 and min(r["by_optimizer"]) >= 50 for r in rows.values())
    conclusions["G3"] = {b: {"mean": r["mean"], "lowest": min(r["by_optimizer"])} for b, r in rows.items()}
    conclusions["G3"]["verdict"] = "CANDIDATE" if candidate else "NO_CANDIDATE"
    groups = {f"{b}/{name}": row for b, values in blocks.items() for name, row in values.items()}
    conclusions["G5"] = {"void_games": {k: v["void_games"] for k, v in groups.items() if v["void_games"]}, "invalid_groups": [k for k, v in groups.items() if v["validity"] == "INVALID"],
                         "void_reasons": {k: v["void_reasons"] for k, v in groups.items() if v["void_reasons"]},
                         "training_void_games": {}, "training_aborted": []}
    report["conclusions"] = conclusions

    report["training"] = {str(seed): training(seed) for seed in OPTIMIZERS}
    for seed, entry in report["training"].items():
        conclusions["G5"]["training_void_games"][seed] = entry["void_games"]
        if entry["rounds"] != 1280:
            conclusions["G5"]["training_aborted"].append(seed)
    for block in BLOCKS:
        report[f"by_deck_{block}"] = {name: split(tables[block, name], learner_deck) for name in (1280, "numpy")}
        report[f"by_seat_{block}"] = {name: split(tables[block, name], lambda k: k[2]) for name in (1280, "numpy")}
        report[f"matchup_interval_{block}"] = {"1280": bootstrap_matchups(tables[block, 1280])}
    replayed, mismatches, natives, policies = 0, 0, set(), set()
    fires, decisions = {}, {}
    for path in sorted(EVAL.iterdir()):
        summary = json.loads((path / "summary.json").read_text())
        replayed += summary["replayed_cells"]
        mismatches += len(summary["replay_mismatches"])
        natives.update(v for k, v in summary["native_sha256"].items() if "cpython" in k)
        if summary.get("repeat_guard"):
            fires[path.name] = summary["guard_fires"]
            decisions[path.name] = summary["decisions"]
    report["guard"] = {"fires": fires, "decisions": decisions}
    report["provenance"] = {"phases": len(list(EVAL.iterdir())), "replayed_cells": replayed, "replay_mismatches": mismatches, "native_sha256": sorted(natives)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1) + "\n")
    show = {k: report[k] for k in ("protocol_sha256", "protocol_expected", "conclusions", "controls", "head_to_head", "provenance", "guard", "matchup_interval_fixtures", "matchup_interval_gauntlet")}
    print(json.dumps(show, indent=1))
    for block in BLOCKS:
        for name, row in blocks[block].items():
            print(block, name, row["by_optimizer"], "mean", row["mean"], "void", row["void_games"])
    print("random", report["fixtures_vs_random"]["by_optimizer"], report["numpy_vs_random"]["by_optimizer"])


if __name__ == "__main__":
    main()
