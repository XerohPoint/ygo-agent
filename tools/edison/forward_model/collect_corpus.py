#!/usr/bin/env python3
"""Play the gauntlet through the certified production module and record every decision.

The forward model is compared against this corpus: acting player, option count, winner, win reason
and the bytes of every observation field at every decision of every game.

Pairs: each of the 21 registry lists against its next neighbour, in both seats (42 cells).
Policy: production's Heuristic in both seats, seeded as tools/edison/learner/actor.py seeds it.
Seeds: 85400000 + cell * 1000 + env. Only 85400000-85499999 may be used here.

Output, under --output:
  registry.txt  name and .ydk path of each list, read by the native bench
  games.txt     one game per four lines: header, actions, acting players, option counts
  digests.txt   one line per decision: sha256 (first 16 hex) of cards_ global_ decision_ selection_
                actions_ (offered rows only) h_actions_
  summary.json  per-cell counts and the module hash
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools/edison/learner"))
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "ygoenv"))
import decks as deck_registry  # noqa: E402

SEED_FIRST, SEED_LAST = 85400000, 85499999
CERTIFIED_MODULE = "b8fcd2bcf7774ae4"
MODULE = ROOT / "ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so"
MAX_STEPS = 10000
MAX_OPTIONS = 256
FIELDS = ("cards_", "global_", "decision_", "selection_")


def next_neighbour_pairs(names):
    pairs = []
    for i, name in enumerate(names):
        other = names[(i + 1) % len(names)]
        pairs.append((name, other))
        pairs.append((other, name))
    return pairs


def field_digest(array):
    return hashlib.sha256(array.tobytes()).hexdigest()[:16]


def play_cell(ygoenv, np, Heuristic, metadata, deck0, deck1, seed, games, threads, out_games, out_digests):
    env = ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=games, num_threads=threads, seed=seed,
                      deck1=deck0, deck2=deck1, play_mode="self", player=-1, async_reset=False,
                      greedy_reward=False, oppo_info=False, max_options=MAX_OPTIONS, max_steps=MAX_STEPS,
                      max_episode_steps=MAX_STEPS, timeout=10, verbose=False)
    agents = {(eid, seat): Heuristic(metadata, seed + eid * 2 + seat) for eid in range(games) for seat in (0, 1)}
    trace = {eid: [] for eid in range(games)}
    results = {}
    obs, info = env.reset()
    while len(results) < games:
        ids = np.asarray(info["env_id"], dtype=np.int32)
        actions = []
        for row, eid in enumerate(ids.tolist()):
            count = int(info["num_options"][row])
            if not 0 < count <= MAX_OPTIONS or info["action_overflow"][row]:
                raise RuntimeError(f"action overflow env {eid}: {count}")
            seat = int(info["to_play"][row])
            offered = np.ascontiguousarray(obs["actions_"][row, :count])
            action = agents[eid, seat].choose(obs["cards_"][row], obs["global_"][row], offered)[0]
            digests = [field_digest(np.ascontiguousarray(obs[key][row])) for key in FIELDS]
            digests += [field_digest(offered), field_digest(np.ascontiguousarray(obs["h_actions_"][row]))]
            trace[eid].append((int(action), seat, count, digests))
            actions.append(action)
        obs, rewards, terminated, truncated, info = env.step(np.asarray(actions), ids)
        if np.any(truncated):
            raise RuntimeError("environment truncation")
        keep = []
        for row, eid in enumerate(np.asarray(info["env_id"]).tolist()):
            if terminated[row]:
                results[eid] = (int(info["winner"][row]), int(info["win_reason"][row]))
            else:
                keep.append(row)
        if keep:
            obs = {key: value[keep] for key, value in obs.items()}
            info = {key: np.asarray(info[key])[keep] for key in
                    ("env_id", "num_options", "to_play", "legal_options", "action_overflow")}
    decisions = 0
    for eid in range(games):
        records = trace[eid]
        winner, reason = results[eid]
        decisions += len(records)
        out_games.write(f"GAME {deck0} {deck1} {seed} {eid} {winner} {reason} {len(records)}\n")
        out_games.write(" ".join(str(r[0]) for r in records) + "\n")
        out_games.write(" ".join(str(r[1]) for r in records) + "\n")
        out_games.write(" ".join(str(r[2]) for r in records) + "\n")
        for r in records:
            out_digests.write(" ".join(r[3]) + "\n")
    return decisions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--games-per-cell", type=int, default=16)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--seed-base", type=int, default=SEED_FIRST)
    parser.add_argument("--output", type=Path, default=ROOT / "build/forward_model/corpus")
    args = parser.parse_args()
    sha = hashlib.sha256(MODULE.read_bytes()).hexdigest()
    if not sha.startswith(CERTIFIED_MODULE):
        raise SystemExit(f"Refusing module {sha}: not the certified build {CERTIFIED_MODULE}")
    if os.path.basename(os.getcwd()) != "scripts":
        raise SystemExit("Run from the checkout's scripts directory, so ./script resolves")
    args.output.mkdir(parents=True, exist_ok=True)

    registry = deck_registry.registry()
    names = list(registry)
    if len(names) != 21:
        raise SystemExit(f"Registry has {len(names)} lists, not 21")
    (args.output / "registry.txt").write_text("".join(f"{k} {Path(v).resolve()}\n" for k, v in registry.items()))

    import numpy as np
    import ygoenv
    from ygoenv.ygopro import init_module
    from ygoai.edison_policy import Heuristic
    init_module(str(deck_registry.DB), str(deck_registry.CODES), {k: str(v) for k, v in registry.items()})
    metadata = deck_registry.card_metadata()

    pairs = next_neighbour_pairs(names)
    last = args.seed_base + (len(pairs) - 1) * 1000 + args.games_per_cell - 1
    if args.seed_base < SEED_FIRST or last > SEED_LAST:
        raise SystemExit(f"Seeds {args.seed_base}-{last} are outside {SEED_FIRST}-{SEED_LAST}")
    summary = []
    with (args.output / "games.txt").open("w") as games, (args.output / "digests.txt").open("w") as digests:
        for cell, (deck0, deck1) in enumerate(pairs):
            seed = args.seed_base + cell * 1000
            decisions = play_cell(ygoenv, np, Heuristic, metadata, deck0, deck1, seed, args.games_per_cell,
                                  args.threads, games, digests)
            summary.append({"cell": cell, "deck0": deck0, "deck1": deck1, "seed": seed,
                            "games": args.games_per_cell, "decisions": decisions})
            print(json.dumps(summary[-1]), flush=True)
    report = {"module_sha256": sha, "cells": len(pairs), "games": sum(s["games"] for s in summary),
              "decisions": sum(s["decisions"] for s in summary), "seed_first": args.seed_base, "seed_last": last,
              "lists": names, "per_cell": summary}
    (args.output / "summary.json").write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps({k: report[k] for k in ("module_sha256", "cells", "games", "decisions", "seed_first", "seed_last")}))


if __name__ == "__main__":
    main()
