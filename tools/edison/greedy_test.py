#!/usr/bin/env python3
"""Run the predeclared greedy-versus-sampled test of frozen PPO models.

Protocol: tests/edison/reports/greedy-test-2026-11-04/protocol.md
Run without arguments for the study. "--worker" is the subprocess mode that
agent_match.run_worker() spawns for the greedy arm.
"""
from concurrent.futures import ProcessPoolExecutor
import itertools
import json
from pathlib import Path
import random
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/edison"))
import agent_match  # noqa: E402
import numpy as np  # noqa: E402
import ygoai.edison_rl as rl_module  # noqa: E402

OUT = ROOT / "build/edison-greedy-test-2026-11-04"
EVIDENCE = ROOT / "tests/edison/reports/greedy-test-2026-11-04"
SEED, COUNT = 72000000, 16
OPTIMIZERS = (1702, 1703, 1704)
DECKS = ("blackwing", "frog_monarch", "machina_gadget")
PAIRINGS = tuple(itertools.permutations(DECKS, 2))
WORKERS = 6


class GreedyAgent(rl_module.RLAgent):
    def choose(self, cards, glob, actions, decision, selection):
        x = self.model.mask_features(self.encoder.encode(cards, glob, actions, decision, selection))
        logits, _ = self.model.forward(x)
        probs, logp, _ = rl_module.distribution(logits, np.array([0, len(x)]))
        choice = int(np.argmax(probs))
        value = float(self.model.value(x.mean(axis=0)))
        self.encoder.observe(cards, actions[choice])
        return choice, x, float(logp[choice]), value


def run_cell(job):
    arm, opponent, optimizer, deck0, deck1, seat, threads, run = job
    policies = ("rl", opponent) if seat == 0 else (opponent, "rl")
    model = ROOT / f"tests/edison/reports/rl-budget-2026-11-02/seed-{optimizer}/final-policy.json"
    # run_worker() spawns Path(__file__); the sampled arm keeps the unmodified worker.
    agent_match.__file__ = str(ROOT / "tests/edison/agent_match.py") if arm == "sampled" else __file__
    output = OUT / arm / opponent / f"seed-{optimizer}" / f"{deck0}-{deck1}-seat{seat}"
    result = agent_match.run_worker(SEED, COUNT, threads, deck0, deck1, output, run, *policies, model)
    duels = [(duel["winner"], duel["seed"] if "seed" in duel else index)
             for index, duel in enumerate(result["duels"])]
    assert len(duels) == COUNT, "Incomplete worker"
    return {"arm": arm, "opponent": opponent, "optimizer": optimizer, "deck0": deck0, "deck1": deck1,
            "seat": seat, "threads": threads, "winners": [winner for winner, _ in duels],
            "decisions": result["decisions"], "elapsed": result["elapsed_seconds"], "duels": result["duels"]}


def run_control(pairing):
    deck0, deck1 = pairing
    agent_match.__file__ = str(ROOT / "tests/edison/agent_match.py")
    output = OUT / "control-heuristic" / f"{deck0}-{deck1}"
    result = agent_match.run_worker(SEED, COUNT, 1, deck0, deck1, output, 1, "heuristic", "heuristic")
    return {"deck0": deck0, "deck1": deck1, "winners": [duel["winner"] for duel in result["duels"]]}


def comparable(duel):
    return {key: value for key, value in duel.items() if "time" not in key and "latency" not in key and "elapsed" not in key}


def outcomes(rows, arm, opponent):
    return {(r["optimizer"], r["deck0"], r["deck1"], r["seat"], index): int(winner == r["seat"])
            for r in rows if r["arm"] == arm and r["opponent"] == opponent
            for index, winner in enumerate(r["winners"])}


def compare(rows, opponent):
    sampled, greedy = outcomes(rows, "sampled", opponent), outcomes(rows, "greedy", opponent)
    assert sampled.keys() == greedy.keys() and len(sampled) == 576
    per_optimizer = {}
    for optimizer in OPTIMIZERS:
        keys = [key for key in sampled if key[0] == optimizer]
        s, g = sum(sampled[k] for k in keys), sum(greedy[k] for k in keys)
        per_optimizer[optimizer] = {"sampled_wins": s, "greedy_wins": g, "games": len(keys),
                                    "gain_pp": round(100 * (g - s) / len(keys), 2)}
    gains = [row["gain_pp"] for row in per_optimizer.values()]
    starts = sorted({key[1:] for key in sampled})
    rng, draws = random.Random(SEED), []
    differences = {start: np.mean([greedy[(o, *start)] - sampled[(o, *start)] for o in OPTIMIZERS]) for start in starts}
    for _ in range(10000):
        draws.append(100 * np.mean([differences[rng.choice(starts)] for _ in starts]))
    by_deck = {}
    for deck in DECKS:
        keys = [k for k in sampled if (k[1] if k[3] == 0 else k[2]) == deck]
        by_deck[deck] = {"sampled": round(np.mean([sampled[k] for k in keys]), 4),
                         "greedy": round(np.mean([greedy[k] for k in keys]), 4), "games": len(keys)}
    return {"sampled_wins": sum(sampled.values()), "greedy_wins": sum(greedy.values()), "games_per_arm": len(sampled),
            "sampled_rate": round(np.mean(list(sampled.values())), 4), "greedy_rate": round(np.mean(list(greedy.values())), 4),
            "mean_gain_pp": round(float(np.mean(gains)), 2), "sd_gain_pp": round(float(np.std(gains, ddof=1)), 2),
            "descriptive_bootstrap_95_pp": [round(float(x), 2) for x in np.percentile(draws, [2.5, 97.5])],
            "per_optimizer": per_optimizer, "by_learner_deck": by_deck}


def main():
    assert not OUT.exists(), "Refusing to overwrite; use a fresh output"
    OUT.mkdir(parents=True)
    cells = [(optimizer, *pairing, seat) for optimizer in OPTIMIZERS for pairing in PAIRINGS for seat in (0, 1)]
    jobs = [(arm, opponent, *cell, 1, 1) for arm in ("sampled", "greedy")
            for opponent in ("heuristic", "random") for cell in cells]
    replays = [("greedy", "heuristic", *cell, 4, 2) for cell in cells]
    with ProcessPoolExecutor(max_workers=WORKERS) as pool:
        rows = list(pool.map(run_cell, jobs))
        replay_rows = list(pool.map(run_cell, replays))
        control = list(pool.map(run_control, PAIRINGS))
    key = lambda row: (row["optimizer"], row["deck0"], row["deck1"], row["seat"])
    original = {key(r): r for r in rows if r["arm"] == "greedy" and r["opponent"] == "heuristic"}
    mismatches = [key(r) for r in replay_rows
                  if [comparable(d) for d in r["duels"]] != [comparable(d) for d in original[key(r)]["duels"]]]
    winners = [winner for row in rows + replay_rows for winner in row["winners"]]
    heuristic, random_ = compare(rows, "heuristic"), compare(rows, "random")
    checks = {"mean_gain_vs_heuristic_at_least_5pp": heuristic["mean_gain_pp"] >= 5,
              "every_optimizer_gain_above_zero": all(v["gain_pp"] > 0 for v in heuristic["per_optimizer"].values()),
              "vs_random_not_below_minus_3pp": random_["mean_gain_pp"] >= -3,
              "no_incomplete_duels": all(winner in (0, 1, 2) for winner in winners),
              "replay_matches": not mismatches}
    mirror = [winner for row in control for winner in row["winners"]]
    summary = {"seeds": [SEED, SEED + COUNT - 1], "verdict": "PASS" if all(checks.values()) else "FAIL", "checks": checks,
               "vs_heuristic": heuristic, "vs_random": random_, "draws": sum(winner == 2 for winner in winners),
               "replay_mismatches": mismatches, "replayed_cells": len(replay_rows),
               "control_heuristic_mirror": {"games": len(mirror), "seat0_wins": sum(w == 0 for w in mirror),
                                            "seat1_wins": sum(w == 1 for w in mirror), "draws": sum(w == 2 for w in mirror)},
               "decisions_per_second_serial": {arm: round(sum(r["decisions"] for r in rows if r["arm"] == arm) /
                                                          sum(r["elapsed"] for r in rows if r["arm"] == arm))
                                               for arm in ("sampled", "greedy")}}
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    (EVIDENCE / "summary.json").write_text(json.dumps(summary, indent=1) + "\n")
    (OUT / "cells.json").write_text(json.dumps([{k: v for k, v in r.items() if k != "duels"} for r in rows], indent=0))
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    if "--worker" in sys.argv:
        rl_module.RLAgent = GreedyAgent
        agent_match.main()
    else:
        main()
