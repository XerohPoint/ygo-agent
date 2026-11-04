#!/usr/bin/env python3
"""Tables and predeclared conclusions from the hardened re-baseline. Games in a void cell never count."""
import argparse
import gzip
import json
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
EVAL = ROOT / "build/edison-rebaseline-2026-11-04/eval"
OPTIMIZERS = (1702, 1703, 1704)
BEFORE = {"numpy": 45.2, "league32": 32.8, "league160": 49.8, "league320": 51.0, "gauntlet160": 43.4}


def load(name):
    with gzip.open(EVAL / name / "cells.json.gz", "rt") as f:
        return json.load(f)


def outcomes(rows, challenger, opponent):
    table, void = {}, 0
    for row in rows:
        if row["challenger"] != challenger or row["opponent"] != opponent:
            continue
        if row["status"] != "PASS":
            void += row["games"]
            continue
        for duel in row["duels"]:
            table[row["deck0"], row["deck1"], row["seat"], duel["seed"]] = int(duel["winner"] == row["seat"])
    return table, void


def pct(table):
    return round(100 * float(np.mean(list(table.values()))), 2) if table else None


def arm(rows, label, opponent):
    per, void = {}, 0
    for seed in OPTIMIZERS:
        table, voided = outcomes(rows, f"{label}-{seed}", opponent)
        per[seed], void = table, void + voided
    rates = [pct(per[s]) for s in OPTIMIZERS]
    wins = sum(sum(t.values()) for t in per.values())
    games = sum(len(t) for t in per.values())
    return {"by_optimizer": rates, "mean": round(float(np.mean(rates)), 2), "wins": wins, "games": games, "void_games": void}, per


def learner_deck(key):
    return key[0] if key[2] == 0 else key[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = {"fixtures": {}, "head_to_head": {}, "gauntlet": {}, "void_cells": {}, "provenance": {}}
    tables = {}
    for name in ("numpy", "league32", "league160", "league320", "gauntlet160"):
        rows = load(f"{name}-pilot")
        report["fixtures"][name] = {}
        for opponent in ("heuristic", "random"):
            report["fixtures"][name][opponent], tables[name, opponent] = arm(rows, name, opponent)
    for label in ("league320", "gauntlet160"):
        rates, void = [], 0
        for seed in OPTIMIZERS:
            table, voided = outcomes(load(f"{label}-vs-numpy-pilot-{seed}"), f"{label}-{seed}", f"numpy-{seed}")
            rates.append(pct(table))
            void += voided
        report["head_to_head"][label] = {"by_optimizer": rates, "mean": round(float(np.mean(rates)), 2), "void_games": void}
    mirror, _ = outcomes(load("control-pilot"), "heuristic-mirror", "heuristic")
    random_control, _ = outcomes(load("control-random-pilot"), "heuristic-vs-random", "random")
    report["controls"] = {"heuristic_mirror": pct(mirror), "heuristic_vs_random": pct(random_control)}

    gain = []
    for seed in OPTIMIZERS:
        net, base = tables["league320", "heuristic"][seed], tables["numpy", "heuristic"][seed]
        common = sorted(set(net) & set(base))
        gain.append(round(100 * float(np.mean([net[k] - base[k] for k in common])), 2))
    league = report["fixtures"]["league320"]["heuristic"]
    h2h = report["head_to_head"]["league320"]
    report["by_deck_league320"] = {}
    for deck in sorted({learner_deck(k) for k in tables["league320", "heuristic"][OPTIMIZERS[0]]}):
        report["by_deck_league320"][deck] = {
            name: round(100 * float(np.mean([v for s in OPTIMIZERS for k, v in tables[name, "heuristic"][s].items() if learner_deck(k) == deck])), 2)
            for name in ("league320", "numpy")}
    best = max(report["fixtures"], key=lambda n: report["fixtures"][n]["heuristic"]["mean"] if n != "numpy" else -1)
    best_row = report["fixtures"][best]["heuristic"]
    conclusions = {"H1": {}, "H2": {"gain_by_optimizer": gain, "mean_gain": round(float(np.mean(gain)), 2), "head_to_head_mean": h2h["mean"],
                                   "verdict": "NET_AHEAD" if np.mean(gain) > 0 and h2h["mean"] >= 50 else "NOT_SHOWN"},
                   "H3": {"best": best, "mean": best_row["mean"], "lowest": min(best_row["by_optimizer"]),
                          "verdict": "CANDIDATE" if best_row["mean"] >= 55 and min(best_row["by_optimizer"]) >= 50 else "NO_CANDIDATE"}}
    for name, before in BEFORE.items():
        now = report["fixtures"][name]["heuristic"]["mean"]
        conclusions["H1"][name] = {"before": before, "after": now, "shift": round(now - before, 2),
                                   "verdict": "CARRIES_OVER" if abs(now - before) <= 3 else "SHIFTED"}

    limit_voids, fires = 0, {}
    for name, labels in (("gauntlet160-on-gauntlet", ["gauntlet160"]), ("league320-on-gauntlet", ["league320"]), ("numpy-on-gauntlet", ["numpy"])):
        rows = load(name)
        row, _ = arm(rows, labels[0], "heuristic")
        row["void_fraction"] = round(row["void_games"] / max(row["games"] + row["void_games"], 1), 4)
        row["validity"] = "INVALID" if row["void_fraction"] > 0.01 else "VALID"
        report["gauntlet"][labels[0]] = row
        fires[labels[0]] = sum(r["counters"].get("guard_fires", 0) for r in rows if r["status"] == "PASS")
        bad = [{"challenger": r["challenger"], "deck0": r["deck0"], "deck1": r["deck1"], "seat": r["seat"], "reason": r.get("reason")}
               for r in rows if r["status"] != "PASS"]
        report["void_cells"][name] = bad
        limit_voids += sum("decision limit" in (b["reason"] or "") or "timed out" in (b["reason"] or "") for b in bad)
    report["gauntlet"]["guard_fires"] = fires
    table, void = outcomes(load("control-on-gauntlet"), "heuristic-mirror", "heuristic")
    report["gauntlet"]["heuristic_mirror"] = {"rate": pct(table), "games": len(table), "void_games": void}
    conclusions["H4"] = {"decision_limit_voids": limit_voids, "verdict": "GUARD_WORKS" if limit_voids == 0 else "GUARD_INSUFFICIENT"}
    report["conclusions"] = conclusions

    replayed, mismatches, fixture_void = 0, 0, 0
    natives = set()
    for path in sorted(EVAL.iterdir()):
        summary = json.loads((path / "summary.json").read_text())
        replayed += summary["replayed_cells"]
        mismatches += len(summary["replay_mismatches"])
        if summary["decks"] == "fixtures":
            fixture_void += sum(entry["void_games"] for entry in summary["total"].values())
        natives.update(v for k, v in summary["native_sha256"].items() if "cpython" in k)
    report["provenance"] = {"replayed_cells": replayed, "replay_mismatches": mismatches, "fixture_void_games": fixture_void, "native_sha256": sorted(natives),
                            "phases": len(list(EVAL.iterdir()))}
    args.output.write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps({k: report[k] for k in ("conclusions", "controls", "head_to_head", "provenance")}, indent=1))


if __name__ == "__main__":
    main()
