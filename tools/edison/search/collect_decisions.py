#!/usr/bin/env python3
"""Play full games on all 21 lists and record every raw observation for the differential.

Reuses actor.py's environment settings, audit, normalization, pack and NetPolicy. Each ordered
pair of neighbouring lists plays one cell, so every list sits in both seats. Policies rotate
by cell. Decisions are written only when their game ends, so a voided cell contributes nothing.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import sys

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "learner"))
sys.path.insert(0, str(HERE))
import decks as deck_registry  # noqa: E402
from decks import ROOT  # noqa: E402
from dump_format import RECORD  # noqa: E402

SEED_BASE, SEED_END = 85000000, 85099999
NATIVE_PREFIX = "b8fcd2bcf7774ae4"
CHECKPOINTS = {seed: f"/home/mada/ygo-agent/build/edison-retrain-2026-11-04/runs/gauntlet-{seed}/checkpoint-2560" for seed in (1702, 1703, 1704)}
# One per cell, in rotation. A seat is net:<optimizer>, net-greedy:<optimizer>, heuristic or random.
ROTATION = (("net:1702", "net:1703"), ("net:1704", "heuristic"), ("heuristic", "net:1702"),
            ("net-greedy:1703", "net-greedy:1704"), ("random", "net:1703"), ("random", "random"))


def all_cells(games, base):
    names = sorted(deck_registry.registry())
    if len(names) != 21:
        raise ValueError("Expected 21 lists")
    pairs = [(names[i], names[(i + step) % 21]) for i in range(21) for step in range(1, 5)]
    pairs += [(b, a) for a, b in pairs]
    cells = []
    for index, (deck0, deck1) in enumerate(pairs):
        seed = base + index * games
        if not SEED_BASE <= seed <= SEED_END - games:
            raise ValueError("Seed is outside the block given to this tool")
        cells.append({"index": index, "deck0": deck0, "deck1": deck1, "seed": seed,
                      "deck0_id": names.index(deck0), "deck1_id": names.index(deck1),
                      "policies": ROTATION[index % len(ROTATION)]})
    return names, cells


class Seat:
    def __init__(self, spec, nets, metadata, seed):
        self.kind, _, name = spec.partition(":")
        self.greedy = self.kind == "net-greedy"
        self.net = nets[int(name)] if name else None
        self.spec = spec
        if self.kind == "heuristic":
            from ygoai.edison_policy import Heuristic
            self.heuristic = Heuristic(metadata, seed)
        self.random = random.Random(seed ^ 0xED150)
        self.sampler = np.random.default_rng([seed, 0x5050])


def play_cell(cell, games, nets, metadata, n_codes, make_env):
    from actor import RepeatGuard
    from obs import OBS_KEYS, audit, normalize_history_turn, pack, segment_distribution
    env = make_env(cell, games)
    seats = {(eid, seat): Seat(cell["policies"][seat], nets, metadata, cell["seed"] + eid * 2 + seat)
             for eid in range(games) for seat in (0, 1)}
    guards = {key: RepeatGuard() for key, seat in seats.items() if seat.greedy}
    buffers = {eid: [] for eid in range(games)}
    steps = {eid: 0 for eid in range(games)}
    finished = {}
    obs, info = env.reset()
    while len(finished) < games:
        ids = np.asarray(info["env_id"], dtype=np.int32)
        order = np.argsort(ids)
        ids = ids[order]
        raw = {key: obs[key][order] for key in OBS_KEYS}
        counts = np.asarray(info["num_options"])[order]
        legal = np.asarray(info["legal_options"])[order]
        overflow = np.asarray(info["action_overflow"])[order]
        to_play = np.asarray(info["to_play"])[order]
        audit(raw, counts, legal, overflow, n_codes)
        for row, eid in enumerate(ids.tolist()):
            rec = np.zeros(1, dtype=RECORD)
            for field, key in (("cards", "cards_"), ("selection", "selection_"), ("actions", "actions_"), ("hist", "h_actions_"),
                               ("decision", "decision_"), ("global", "global_")):
                value = raw[key][row]
                if not np.can_cast(value.dtype, RECORD[field].base, "safe") or value.shape != RECORD[field].shape:
                    raise ValueError(f"Unexpected observation array {key}: {value.dtype} {value.shape}")
                rec[field] = value
            rec["counts"], rec["legal"], rec["overflow"] = counts[row], legal[row], int(overflow[row])
            rec["to_play"], rec["deck0"], rec["deck1"] = to_play[row], cell["deck0_id"], cell["deck1_id"]
            rec["game"], rec["step"] = cell["index"] * 1000 + eid, steps[eid]
            buffers[eid].append(rec)
            steps[eid] += 1
        norm = dict(raw)
        norm["h_actions_"] = normalize_history_turn(raw["h_actions_"], raw["global_"])
        actions = np.zeros(len(ids), dtype=np.int64)
        for row, eid in enumerate(ids.tolist()):
            seat = seats[eid, int(to_play[row])]
            count = int(counts[row])
            if seat.kind == "random":
                actions[row] = seat.random.randrange(count)
            elif seat.kind == "heuristic":
                actions[row] = seat.heuristic.choose(raw["cards_"][row], raw["global_"][row], raw["actions_"][row, :count])[0]
            else:
                sub = {key: norm[key][row:row + 1] for key in OBS_KEYS}
                packed = pack(sub, counts[row:row + 1])
                logits, _ = seat.net.infer(packed)
                logp, _ = segment_distribution(logits, packed["offsets"])
                if seat.greedy:
                    key = hashlib.sha256(b"".join(raw[name][row].tobytes() for name in ("cards_", "selection_", "global_", "decision_"))
                                         + raw["actions_"][row, :count].tobytes()).digest()
                    actions[row] = guards[eid, int(to_play[row])].choose(key, logp)
                else:
                    actions[row] = min(int(np.searchsorted(np.cumsum(np.exp(logp)), seat.sampler.random())), count - 1)
        obs, rewards, terminated, truncated, info = env.step(actions, ids)
        if np.any(truncated):
            raise RuntimeError("environment truncation")
        keep = []
        for row, eid in enumerate(np.asarray(info["env_id"]).tolist()):
            if terminated[row]:
                finished[eid] = int(info["winner"][row])
            else:
                keep.append(row)
        if keep:
            obs = {key: obs[key][keep] for key in OBS_KEYS}
            info = {key: np.asarray(info[key])[keep] for key in ("env_id", "num_options", "legal_options", "action_overflow", "to_play")}
    return buffers, finished


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--games", type=int, default=2, help="Games per cell")
    parser.add_argument("--seed", type=int, default=SEED_BASE)
    parser.add_argument("--shard", type=int, default=0)
    parser.add_argument("--shards", type=int, default=1)
    parser.add_argument("--cells", type=int, default=0, help="Stop after this many cells; 0 runs all")
    args = parser.parse_args()
    sys.path.insert(0, str(ROOT))
    sys.path.insert(0, str(ROOT / "ygoenv"))
    os.chdir(ROOT / "scripts")
    import ygoenv
    from ygoenv.ygopro import init_module
    from ygoenv.ygopro import ygopro_ygoenv
    native = hashlib.sha256(Path(ygopro_ygoenv.__file__).read_bytes()).hexdigest()
    if not native.startswith(NATIVE_PREFIX):
        raise SystemExit(f"Native module is not the certified build: {native}")
    from actor import NetPolicy, MAX_OPTIONS, MAX_STEPS
    registry = deck_registry.registry()
    init_module(str(deck_registry.DB), str(deck_registry.CODES), {k: str(v) for k, v in registry.items()})
    metadata = deck_registry.card_metadata()
    n_codes = len(metadata)
    nets = {}
    for seed, stem in CHECKPOINTS.items():
        meta = json.loads(Path(stem + ".json").read_text())
        nets[seed] = NetPolicy(stem, meta["params_sha256"])

    def make_env(cell, games):
        return ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=games, num_threads=1, seed=cell["seed"],
                           deck1=cell["deck0"], deck2=cell["deck1"], play_mode="self", player=-1, async_reset=False,
                           greedy_reward=False, oppo_info=False, max_options=MAX_OPTIONS, max_steps=MAX_STEPS,
                           max_episode_steps=MAX_STEPS, timeout=10, verbose=False)

    names, cells = all_cells(args.games, args.seed)
    if args.cells:
        cells = cells[:args.cells]
    report = {"native_sha256": native, "lists": names, "seed_base": args.seed, "games_per_cell": args.games, "cells": [], "void": []}
    with args.output.open("wb") as out:
        for cell in cells[args.shard::args.shards]:
            try:
                buffers, finished = play_cell(cell, args.games, nets, metadata, n_codes, make_env)
            except Exception as error:  # A failed cell contributes no games.
                report["void"].append({"cell": cell["index"], "seed": cell["seed"], "reason": f"{type(error).__name__}: {error}"})
                print(f"void cell {cell['index']}: {error}", flush=True)
                continue
            count = 0
            for eid in sorted(buffers):
                np.concatenate(buffers[eid]).tofile(out)
                count += len(buffers[eid])
            report["cells"].append({"cell": cell["index"], "seed": cell["seed"], "deck0": cell["deck0"], "deck1": cell["deck1"],
                                    "policies": cell["policies"], "decisions": count, "winners": list(finished.values())})
            print(f"cell {cell['index']} {cell['deck0']} vs {cell['deck1']} {cell['policies']}: {count} decisions", flush=True)
    args.output.with_suffix(".json").write_text(json.dumps(report, indent=1) + "\n")


if __name__ == "__main__":
    main()
