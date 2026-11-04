#!/usr/bin/env python3
"""Diagnose a greedy decision loop: replay a voided cell and print the repeating actions.

Post-hoc tool for the gauntlet void cells. It changes no result. Run from scripts/ cwd via --root.
"""
import argparse
import json
import os
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
os.environ.setdefault("JAX_PLATFORMS", "cpu")
os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
import decks as deck_registry  # noqa: E402
from decks import ROOT  # noqa: E402
sys.path[:0] = [str(ROOT), str(ROOT / "ygoenv")]

ACTS = {0: "none", 1: "set", 2: "repo", 3: "spsummon", 4: "summon", 5: "mset", 6: "attack", 7: "direct", 8: "activate", 9: "cancel"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stem", required=True)
    parser.add_argument("--deck0", required=True)
    parser.add_argument("--deck1", required=True)
    parser.add_argument("--seat", type=int, required=True)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--games", type=int, default=8)
    parser.add_argument("--limit", type=int, default=2000)
    args = parser.parse_args()
    os.chdir(ROOT / "scripts")
    import numpy as np
    import ygoenv
    from ygoenv.ygopro import init_module
    from actor import NetPolicy, checkpoint_paths
    from obs import OBS_KEYS, normalize_history_turn, pack, segment_distribution
    from ygoai.edison_policy import Heuristic
    init_module(str(deck_registry.DB), str(deck_registry.CODES), {k: str(v) for k, v in deck_registry.registry().items()})
    metadata = deck_registry.card_metadata()
    meta = json.loads(checkpoint_paths(args.stem)[1].read_text())
    net = NetPolicy(args.stem, meta["params_sha256"])
    env = ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=args.games, num_threads=1, seed=args.seed, deck1=args.deck0,
                      deck2=args.deck1, play_mode="self", player=-1, async_reset=False, greedy_reward=False, oppo_info=False,
                      max_options=256, max_steps=10000, max_episode_steps=10000, timeout=10, verbose=False)
    heuristics = [Heuristic(metadata, args.seed + eid * 2 + (1 - args.seat)) for eid in range(args.games)]
    obs, info = env.reset()
    logs = {eid: [] for eid in range(args.games)}
    steps = [0] * args.games
    finished = {}
    boards = {}
    for _ in range(args.limit):
        ids = np.asarray(info["env_id"])
        order = np.argsort(ids)
        ids = ids[order]
        sub = {k: obs[k][order] for k in OBS_KEYS}
        counts = np.asarray(info["num_options"])[order]
        seats = np.asarray(info["to_play"])[order]
        sub["h_actions_"] = normalize_history_turn(sub["h_actions_"], sub["global_"])
        actions = np.zeros(len(ids), dtype=np.int64)
        rows = np.nonzero(seats == args.seat)[0]
        if rows.size:
            packed = pack({k: sub[k][rows] for k in OBS_KEYS}, counts[rows])
            logits, _ = net.infer(packed)
            logp, _ = segment_distribution(logits, packed["offsets"])
            off = packed["offsets"]
            for i, row in enumerate(rows):
                actions[row] = int(np.argmax(logp[off[i]:off[i + 1]]))
        for row, eid in enumerate(ids.tolist()):
            cards = sub["cards_"][row]
            field = cards[np.isin(cards[:, 2], (3, 4))]
            boards[eid] = (int(seats[row]), int(sub["global_"][row][0]) * 256 + int(sub["global_"][row][1]), int(sub["global_"][row][2]) * 256 + int(sub["global_"][row][3]),
                           [(metadata.get(int(c[0]) * 256 + int(c[1]), {}).get("name", "hidden"), "opp" if c[4] else "own", int(c[2]), int(c[5]), "neg" if c[11] else "") for c in field])
            if seats[row] != args.seat:
                actions[row] = heuristics[eid].choose(sub["cards_"][row], sub["global_"][row], sub["actions_"][row, :counts[row]])[0]
            a = sub["actions_"][row, actions[row]]
            cid = int(a[1]) * 256 + int(a[2])
            steps[eid] += 1
            logs[eid].append((steps[eid], int(seats[row]), int(a[3]), ACTS.get(int(a[4]), a[4]), metadata.get(cid, {}).get("name", "-"), int(a[6]), int(counts[row]),
                              int(sub["global_"][row][4]), int(sub["global_"][row][5]), int(sub["decision_"][row][1]), int(sub["decision_"][row][2])))
        obs, rewards, terminated, truncated, info = env.step(actions, ids)
        keep = []
        for row, eid in enumerate(np.asarray(info["env_id"]).tolist()):
            if terminated[row] or truncated[row]:
                finished[eid] = (int(info["winner"][row]), steps[eid], bool(truncated[row]))
            else:
                keep.append(row)
        if not keep:
            break
        obs = {k: obs[k][keep] for k in OBS_KEYS}
        info = {k: np.asarray(info[k])[keep] for k in ("env_id", "num_options", "to_play")}
    print("finished", finished)
    for eid in logs:
        if eid not in finished:
            print("LOOPING env", eid, "steps", steps[eid], "columns: step seat msg act card effect options turn phase hint stage")
            for row in logs[eid][-12:]:
                print(row)
            print("board at last decision: viewer seat, viewer LP, other LP, cards (name, side, zone, position, negated)", boards[eid])


if __name__ == "__main__":
    main()
