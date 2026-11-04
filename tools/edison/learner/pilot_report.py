#!/usr/bin/env python3
"""Tables and predeclared conclusions from the pilot evaluation output.

Reads build/edison-learner-2026-11-04/eval/*/cells.json.gz. Writes one summary JSON. Games in a void
cell never count, and any void cell in a compared group is listed.
"""
import argparse
import gzip
import json
from pathlib import Path
import random
import sys

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
EVAL = ROOT / "build/edison-learner-2026-11-04/eval"
OPTIMIZERS = (1702, 1703, 1704)


def load(name):
    path = EVAL / name / "cells.json.gz"
    if not path.exists():
        return None
    with gzip.open(path, "rt") as f:
        return json.load(f)


def outcomes(rows, challenger, opponent):
    """{(deck0, deck1, seat, seed): 1 win, 0 otherwise}, with draws counted as not a win."""
    table, void = {}, 0
    for row in rows:
        if row["challenger"] != challenger or row["opponent"] != opponent:
            continue
        if row["status"] != "PASS":
            void += row["games"]
            continue
        for duel in row["duels"]:
            table[(row["deck0"], row["deck1"], row["seat"], duel["seed"])] = int(duel["winner"] == row["seat"])
    return table, void


def rate(table):
    return round(float(np.mean(list(table.values()))), 4) if table else None


def group_rate(table, test):
    values = [v for k, v in table.items() if test(k)]
    return {"wins": int(sum(values)), "games": len(values), "rate": round(float(np.mean(values)), 4) if values else None}


def learner_deck(key):
    return key[0] if key[2] == 0 else key[1]


def describe(rows, challenger, opponent):
    table, void = outcomes(rows, challenger, opponent)
    decks = sorted({learner_deck(k) for k in table})
    return {"wins": int(sum(table.values())), "games": len(table), "rate": rate(table), "void_games": void,
            "seat0": group_rate(table, lambda k: k[2] == 0), "seat1": group_rate(table, lambda k: k[2] == 1),
            "by_deck": {d: group_rate(table, lambda k, d=d: learner_deck(k) == d) for d in decks}}


def arm_vs(prefix_rows, arm_label, opponent):
    return {seed: describe(prefix_rows, f"{arm_label}-{seed}", opponent) for seed in OPTIMIZERS}


def paired(new_rows, new_label, ref_rows, ref_label, opponent, draws=10000):
    """Mean gain across optimizers and a descriptive bootstrap over starts. Optimizers share starts."""
    per_seed, per_start = {}, {}
    for seed in OPTIMIZERS:
        new, _ = outcomes(new_rows, f"{new_label}-{seed}", opponent)
        ref, _ = outcomes(ref_rows, f"{ref_label}-{seed}", opponent)
        keys = sorted(set(new) & set(ref))
        if not keys:
            return None
        per_seed[seed] = {"gain_pp": round(100 * float(np.mean([new[k] - ref[k] for k in keys])), 2), "games": len(keys),
                          "new": rate({k: new[k] for k in keys}), "reference": rate({k: ref[k] for k in keys})}
        for k in keys:
            per_start.setdefault(k, []).append(new[k] - ref[k])
    gains = [row["gain_pp"] for row in per_seed.values()]
    starts = sorted(per_start)
    means = np.asarray([np.mean(per_start[k]) for k in starts])
    rng = np.random.default_rng(1702)
    boot = 100 * means[rng.integers(0, len(starts), (draws, len(starts)))].mean(axis=1)
    return {"mean_gain_pp": round(float(np.mean(gains)), 2), "sd_gain_pp": round(float(np.std(gains, ddof=1)), 2),
            "per_optimizer": per_seed, "descriptive_bootstrap_95_pp": [round(float(x), 2) for x in np.percentile(boot, [2.5, 97.5])],
            "starts": len(starts)}


def verdict_equal(gain_heuristic, h2h_rates):
    """Rule R1 in the protocol."""
    gains = [row["gain_pp"] for row in gain_heuristic["per_optimizer"].values()]
    if gain_heuristic["mean_gain_pp"] >= 5 and min(gains) > 0 and np.mean(h2h_rates) >= 0.5:
        return "BEATS_BASELINE"
    if gain_heuristic["mean_gain_pp"] <= -5:
        return "BELOW_BASELINE"
    return "NOT_DISTINGUISHED"


def head_to_head_rates(prefix, label):
    rates, detail = [], {}
    for seed in OPTIMIZERS:
        rows = load(f"{prefix}-{seed}")
        if rows is None:
            return None, None
        row = describe(rows, f"{label}-{seed}", f"numpy-{seed}")
        rates.append(row["rate"])
        detail[seed] = row
    return rates, detail


def m1_candidate(desc):
    """Rule R3: a candidate for a final test, never an M1 pass by itself."""
    rates = [desc[s]["rate"] for s in OPTIMIZERS]
    deck_rates = {}
    for seed in OPTIMIZERS:
        for deck, row in desc[seed]["by_deck"].items():
            deck_rates.setdefault(deck, []).append(row["rate"])
    return {"mean": round(float(np.mean(rates)), 4), "min_seed": min(rates),
            "deck_means": {d: round(float(np.mean(v)), 4) for d, v in deck_rates.items()},
            "candidate": bool(np.mean(rates) >= 0.55 and min(rates) >= 0.5)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--large-round", type=int, default=160)
    args = parser.parse_args()
    report = {"rows_are_shared_env_seeds": "Optimizers share starts, so these are not independent tournament samples."}
    numpy_pilot, numpy_val = load("numpy-pilot"), load("numpy-validation")
    control = load("control-pilot")
    if control:
        report["control_heuristic_mirror_pilot"] = describe(control, "heuristic-mirror", "heuristic")
    control_val = load("control-validation")
    if control_val:
        report["control_heuristic_mirror_validation"] = describe(control_val, "heuristic-mirror", "heuristic")
    control_random = load("control-random-pilot")
    if control_random:
        report["control_heuristic_vs_random_pilot"] = describe(control_random, "heuristic-vs-random", "random")
    if numpy_pilot:
        report["numpy"] = {"pilot": {op: arm_vs(numpy_pilot, "numpy", op) for op in ("heuristic", "random")}}
    if numpy_val:
        report.setdefault("numpy", {})["validation"] = {op: arm_vs(numpy_val, "numpy", op) for op in ("heuristic", "random")}
        table = {}
        total = sum(arm_vs(numpy_val, "numpy", "heuristic")[s]["wins"] for s in OPTIMIZERS)
        report["numpy"]["validation_wins_heuristic_of_288_archive_132"] = total
    report["arms"] = {}
    for arm, round_index in (("league", 32), ("schedule", 32), ("league", 160), ("league", args.large_round)):
        name = f"{arm}{round_index}"
        pilot, val = load(f"{name}-pilot"), load(f"{name}-validation")
        if not pilot:
            continue
        entry = {"pilot": {op: arm_vs(pilot, name, op) for op in ("heuristic", "random")}}
        if val:
            entry["validation"] = {op: arm_vs(val, name, op) for op in ("heuristic", "random")}
            entry["paired_validation_vs_numpy_heuristic"] = paired(val, name, numpy_val, "numpy", "heuristic") if numpy_val else None
        if numpy_pilot:
            entry["paired_pilot_vs_numpy"] = {op: paired(pilot, name, numpy_pilot, "numpy", op) for op in ("heuristic", "random")}
        rates, detail = head_to_head_rates(f"{name}-vs-numpy-pilot", name)
        if rates:
            entry["head_to_head"] = {"mean": round(float(np.mean(rates)), 4), "per_optimizer": detail}
            if numpy_pilot:
                entry["R1"] = verdict_equal(entry["paired_pilot_vs_numpy"]["heuristic"], rates)
        entry["R3"] = m1_candidate(entry["pilot"]["heuristic"])
        report["arms"][name] = entry
    league_small, league_large = load("league32-pilot"), load(f"league{args.large_round}-pilot")
    if league_small and league_large:
        gain = paired(league_large, f"league{args.large_round}", league_small, "league32", "heuristic")
        gains = [row["gain_pp"] for row in gain["per_optimizer"].values()]
        report["budget_gain_R2"] = {**gain, "verdict": "MORE_GAMES_HELP" if gain["mean_gain_pp"] >= 5 and min(gains) > 0 else "NOT_SHOWN"}
    curve = {}
    for round_index in (8, 32, 80, 160, args.large_round):
        rows = load(f"league{round_index}-pilot")
        if rows:
            curve[round_index * 96] = {s: describe(rows, f"league{round_index}-{s}", "heuristic")["rate"] for s in OPTIMIZERS}
    report["league_curve_vs_heuristic_greedy"] = curve
    sampled = {}
    for name, label in (("league32-sampled-pilot", "league32"), (f"league{args.large_round}-sampled-pilot", f"league{args.large_round}"),
                        ("numpy-sampled-pilot", "numpy")):
        rows = load(name)
        if rows:
            sampled[label] = {op: {s: describe(rows, f"{label}-{s}", op)["rate"] for s in OPTIMIZERS} for op in ("heuristic", "random")}
    report["sampled_diagnostic"] = sampled
    gauntlet = {}
    for name, label in (("gauntlet-net-on-gauntlet", "gauntlet160"), ("league-net-on-gauntlet", f"league{args.large_round}"),
                        ("numpy-on-gauntlet", "numpy")):
        rows = load(name)
        if rows:
            gauntlet[label] = {s: describe(rows, f"{label}-{s}", "heuristic") for s in OPTIMIZERS}
    rows = load("control-on-gauntlet")
    if rows:
        gauntlet["heuristic-mirror"] = describe(rows, "heuristic-mirror", "heuristic")
    report["gauntlet_descriptive"] = gauntlet
    rows = load("gauntlet-net-on-fixtures")
    if rows:
        report["gauntlet_net_on_fixtures"] = {op: arm_vs(rows, "gauntlet160", op) for op in ("heuristic", "random")}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1) + "\n")
    print(args.output)


if __name__ == "__main__":
    main()
