#!/usr/bin/env python3
"""Replay evaluated cells and log what one deck's player did, decision by decision.

Post-hoc tool on consumed seeds. It changes no result. Each replayed game must match its stored trace hash, so the log
describes the game that was scored. Run with --cell-file listing {phase, challenger, opponent, deck0, deck1, seat, seed}.
"""
import argparse
import concurrent.futures
import gzip
import hashlib
import json
import multiprocessing
import os
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import decks as deck_registry  # noqa: E402
from actor import MAX_OPTIONS, MAX_STEPS, NetPolicy, RepeatGuard, configure_child_environment  # noqa: E402
from decks import ROOT  # noqa: E402

RUNTIME = {}


def setup():
    os.environ.update(configure_child_environment())
    sys.path[:0] = [str(ROOT), str(ROOT / "ygoenv")]
    os.chdir(ROOT / "scripts")
    import ygoenv
    from ygoenv.ygopro import init_module
    init_module(str(deck_registry.DB), str(deck_registry.CODES), {k: str(v) for k, v in deck_registry.registry().items()})
    RUNTIME.update(ygoenv=ygoenv, metadata=deck_registry.card_metadata(), nets={})


def code(a, b):
    return int(a) * 256 + int(b)


def public_zones(cards, viewer, player):
    """What anyone can see of a player's zones from this viewer's observation: location -> codes."""
    import numpy as np
    relative = 0 if viewer == player else 1
    rows = cards[(cards[:, 4] == relative) & np.isin(cards[:, 2], (2, 3, 4, 5, 6))]
    zones = {}
    for row in rows:
        zones.setdefault(int(row[2]), []).append(code(row[0], row[1]))
    return zones


def replay(cell):
    import numpy as np
    from obs import OBS_KEYS, normalize_history_turn, pack, segment_distribution
    from ygoai.edison_policy import Heuristic
    seed, games, specs, stored = cell["seed"], cell["games"], cell["policies"], {d["env"]: d for d in cell["duels"]}
    watch = cell["seat"]
    nets = RUNTIME["nets"]
    env = RUNTIME["ygoenv"].make("YGOPro-v1", env_type="gymnasium", num_envs=games, num_threads=1, seed=seed, deck1=cell["deck0"], deck2=cell["deck1"], play_mode="self",
                                 player=-1, async_reset=False, greedy_reward=False, oppo_info=False, max_options=MAX_OPTIONS, max_steps=MAX_STEPS, max_episode_steps=MAX_STEPS,
                                 timeout=10, verbose=False)
    heuristics, net_seats = {}, {}
    for seat, spec in enumerate(specs):
        if spec["kind"] == "heuristic":
            heuristics[seat] = [Heuristic(RUNTIME["metadata"], seed + eid * 2 + seat) for eid in range(games)]
        elif spec["kind"] == "net":
            net_seats[seat] = nets.setdefault(spec["sha256"], NetPolicy(spec["checkpoint"], spec["sha256"]))
        else:
            raise ValueError("Only net and heuristic cells are logged")
    guards = {(eid, seat): RepeatGuard() for eid in range(games) for seat, spec in enumerate(specs) if spec.get("repeat_guard")}
    traces = [hashlib.sha256() for _ in range(games)]
    logs = {eid: [] for eid in range(games)}
    results = {}
    obs, info = env.reset()
    while len(results) < games:
        ids = np.asarray(info["env_id"], dtype=np.int32)
        order = np.argsort(ids)
        ids = ids[order]
        sub = {key: obs[key][order] for key in OBS_KEYS}
        counts = np.asarray(info["num_options"])[order]
        seats = np.asarray(info["to_play"])[order]
        sub["h_actions_"] = normalize_history_turn(sub["h_actions_"], sub["global_"])
        actions = np.zeros(len(ids), dtype=np.int64)
        for seat, net in net_seats.items():
            rows = np.nonzero(seats == seat)[0]
            if not rows.size:
                continue
            packed = pack({key: sub[key][rows] for key in OBS_KEYS}, counts[rows])
            logits, _ = net.infer(packed)
            logp, _ = segment_distribution(logits, packed["offsets"])
            offsets = packed["offsets"]
            for i, row in enumerate(rows):
                eid = int(ids[row])
                segment = logp[offsets[i]:offsets[i + 1]]
                if specs[seat].get("repeat_guard"):
                    key = hashlib.sha256(b"".join(sub[name][row].tobytes() for name in ("cards_", "selection_", "global_", "decision_")) + sub["actions_"][row, :int(counts[row])].tobytes()).digest()
                    actions[row] = guards[eid, seat].choose(key, segment)
                else:
                    actions[row] = int(np.argmax(segment))
        for row, (eid, seat) in enumerate(zip(ids.tolist(), seats.tolist())):
            count = int(counts[row])
            if seat in heuristics:
                actions[row] = heuristics[seat][eid].choose(sub["cards_"][row], sub["global_"][row], sub["actions_"][row, :count])[0]
            trace = traces[eid]
            for name in ("cards_", "selection_", "global_"):
                trace.update(sub[name][row].tobytes())
            trace.update(sub["actions_"][row, :count].tobytes())
            trace.update(sub["decision_"][row].tobytes())
            trace.update(int(actions[row]).to_bytes(2, "little"))
            glob = sub["global_"][row]
            entry = {"turn": int(glob[4]), "phase": int(glob[5]), "mine": int(seat == watch), "tp": int(glob[7]) if seat == watch else 1 - int(glob[7]), "public": public_zones(sub["cards_"][row], seat, watch)}
            if seat == watch:
                entry["chosen"] = int(actions[row])
                entry["options"] = [[int(a[3]), int(a[4]), code(a[1], a[2]), int(a[6])] for a in sub["actions_"][row, :count]]
            logs[eid].append(entry)
        obs, _, terminated, truncated, info = env.step(actions, ids)
        if np.any(truncated):
            raise RuntimeError("truncated")
        keep = []
        for row, eid in enumerate(np.asarray(info["env_id"]).tolist()):
            if terminated[row]:
                results[eid] = {"winner": int(info["winner"][row]), "trace": traces[eid].hexdigest()}
            else:
                keep.append(row)
        if keep:
            obs = {key: obs[key][keep] for key in OBS_KEYS}
            info = {key: np.asarray(info[key])[keep] for key in ("env_id", "num_options", "legal_options", "action_overflow", "to_play")}
    mismatches = [eid for eid in range(games) if results[eid]["trace"] != stored[eid]["trace"] or results[eid]["winner"] != stored[eid]["winner"]]
    return {"label": cell["label"], "challenger": cell["challenger"], "deck": cell["deck0"] if watch == 0 else cell["deck1"], "other": cell["deck1"] if watch == 0 else cell["deck0"], "seat": watch, "seed": seed, "mismatches": mismatches,
            "games": [{"seed": seed + eid, "won": int(results[eid]["winner"] == watch), "log": logs[eid]} for eid in range(games)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--eval", type=Path, required=True, help="Evaluation directory holding phases")
    parser.add_argument("--plan", type=Path, required=True, help="JSON list of {label, phase, challenger, opponent, decks}")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=6)
    args = parser.parse_args()
    cells = []
    for item in json.loads(args.plan.read_text()):
        with gzip.open(args.eval / item["phase"] / "cells.json.gz", "rt") as f:
            rows = json.load(f)
        for row in rows:
            learner = row["deck0"] if row["seat"] == 0 else row["deck1"]
            if row["challenger"] == item["challenger"] and row["opponent"] == item["opponent"] and learner in item["decks"] and row["status"] == "PASS":
                cells.append({**row, "label": item["label"]})
    print("cells", len(cells), flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    context = multiprocessing.get_context("spawn")
    bad = 0
    with concurrent.futures.ProcessPoolExecutor(args.workers, mp_context=context, initializer=setup) as pool, gzip.open(args.output, "wt") as out:
        for result in pool.map(replay, cells):
            bad += len(result["mismatches"])
            out.write(json.dumps(result) + "\n")
    print("trace mismatches", bad)
    if bad:
        raise SystemExit("Replay does not match the stored games")


if __name__ == "__main__":
    main()
