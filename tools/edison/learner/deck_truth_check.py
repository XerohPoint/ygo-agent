"""Does the card named at a Deck prompt actually leave the Deck? Truth comes from later public state, not labels or audit code.

After a player picks an option pointing at an own Deck row, that card's count in their own hand, field, Graveyard or banished zone must rise within their next decisions.
Must fail on the old build and pass on the new one for this check to mean anything.
"""
import collections
import json
import os
from pathlib import Path
import random
import sys

W = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(W / "tools/edison/learner"))
import decks as reg  # noqa: E402
from actor import MAX_OPTIONS, MAX_STEPS, configure_child_environment  # noqa: E402
os.environ.update(configure_child_environment())
sys.path[:0] = [str(W), str(W / "ygoenv")]
os.chdir(W / "scripts")
import numpy as np  # noqa: E402
import ygoenv  # noqa: E402
from ygoenv.ygopro import init_module  # noqa: E402
from obs import OBS_KEYS  # noqa: E402

init_module(str(reg.DB), str(reg.CODES), {k: str(v) for k, v in reg.registry().items()})
meta = reg.card_metadata()
names = {i: v["name"] for i, v in meta.items()}
OUT = (2, 3, 4, 5, 6)
WINDOW = 8
c = collections.Counter()
misses = collections.Counter()
decks = sorted(reg.registry())
seed = int(sys.argv[2])
rng = random.Random(seed)


def public(cards):
    rows = cards[(cards[:, 4] == 0) & np.isin(cards[:, 2], OUT)]
    return collections.Counter((rows[:, 0].astype(int) * 256 + rows[:, 1].astype(int)).tolist())


for i, deck in enumerate(decks):
    other = decks[(i + 3) % len(decks)]
    games = 16
    env = ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=games, num_threads=1, seed=seed + i * 100, deck1=deck, deck2=other, play_mode="self", player=-1, async_reset=False,
                      greedy_reward=False, oppo_info=False, max_options=MAX_OPTIONS, max_steps=MAX_STEPS, max_episode_steps=MAX_STEPS, timeout=10, verbose=False)
    obs, info = env.reset()
    pending = {}
    done = 0
    while done < games:
        ids = np.asarray(info["env_id"])
        counts = np.asarray(info["num_options"])
        seats = np.asarray(info["to_play"])
        actions = np.zeros(len(ids), dtype=np.int64)
        for row in range(len(ids)):
            eid, seat = int(ids[row]), int(seats[row])
            n = int(counts[row])
            actions[row] = rng.randrange(n)
            cards = obs["cards_"][row]
            now = public(cards)
            for key in [k for k in pending if k[0] == eid and k[1] == seat]:
                item = pending[key]
                item["left"] -= 1
                if now[item["label"]] > item["before"][item["label"]]:
                    c["found"] += 1
                    del pending[key]
                elif item["left"] <= 0:
                    c["not_found"] += 1
                    misses[names.get(item["label"], str(item["label"]))] += 1
                    del pending[key]
            o = obs["actions_"][row, int(actions[row])]
            pointer = int(o[0])
            if int(o[3]) in (3, 9) and pointer and int(cards[pointer - 1][2]) == 1 and int(cards[pointer - 1][4]) == 0:
                label = int(o[1]) * 256 + int(o[2])
                c["picks"] += 1
                c["chance_x1000"] += 1000 // n
                pending[(eid, seat, c["picks"])] = {"label": label, "before": now, "left": WINDOW}
        obs, _, term, trunc, info = env.step(actions, ids)
        keep = []
        for row, eid in enumerate(np.asarray(info["env_id"]).tolist()):
            if term[row] or trunc[row]:
                done += 1
                c["games"] += 1
                for key in [k for k in pending if k[0] == eid]:
                    c["game_ended_first"] += 1
                    del pending[key]
            else:
                keep.append(row)
        if keep and done < games:
            obs = {k: obs[k][keep] for k in OBS_KEYS}
            info = {k: np.asarray(info[k])[keep] for k in ("env_id", "num_options", "to_play")}
c["chance_x1000"] //= 1
judged = c["found"] + c["not_found"]
print(json.dumps(dict(c)))
print(f"found {100 * c['found'] / max(judged, 1):.2f}% of {judged}")
print("not found", misses.most_common(12))
