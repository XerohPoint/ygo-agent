#!/usr/bin/env python3
"""Play a batch of corpus duels through the certified production module.

Same duel loop and observation contract as tools/edison/pool_audit/soak_worker.py, with two
changes: a third policy, the frozen rule-based Heuristic from ygoai/edison_policy.py (no
network), and a flushed record of every decision, so a native abort leaves the last card
chosen on disk. Duel boundaries also go to stderr, so each diagnostic line can be tied to
its duel. Runs with cwd=scripts because script paths are relative.
"""
import argparse, hashlib, json, os, random, sqlite3, sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "ygoenv"))
sys.path.insert(0, str(ROOT / "tools/edison/pool_audit"))
import soak_worker as audit_worker  # noqa: E402  check_contracts and the eager rule

MAX_STEPS, MAX_OPTIONS = audit_worker.MAX_STEPS, audit_worker.MAX_OPTIONS
ACTS = audit_worker.ACTS


def play(env_make, duel, log, id_to_code, metadata):
    import numpy as np
    from ygoai.edison_policy import Heuristic
    sys.stderr.write(f"@@begin {duel['id']}\n"); sys.stderr.flush()
    log.write(json.dumps({"begin": duel["id"]}) + "\n"); log.flush()
    env = env_make(duel)
    rng = random.Random(duel["seed"] ^ 0xED150)
    heuristics = [Heuristic(metadata, duel["seed"] * 2 + p) for p in range(2)]
    trace = hashlib.sha256()
    chosen, prompts, violations, seen = Counter(), Counter(), Counter(), Counter()
    steps, max_options = 0, 0
    obs, info = env.reset()
    status = "ok"; winner = reason = None
    while True:
        count = int(info["num_options"][0]); legal = int(info["legal_options"][0])
        max_options = max(max_options, legal)
        if info["action_overflow"][0] or legal > MAX_OPTIONS:
            status = "action_overflow"; break
        if count <= 0:
            status = "no_options"; break
        offered = obs["actions_"][0, :count]
        prompts[int(offered[0, 3])] += 1
        problem = audit_worker.check_contracts(obs, 0, count, id_to_code)
        if problem: violations[problem] += 1
        player = int(info["to_play"][0])
        if duel["policy"] == "heuristic":
            action = heuristics[player].choose(obs["cards_"][0], obs["global_"][0], offered)[0]
        else:
            action = audit_worker.choose(duel["policy"], rng, offered, count)
        a = offered[action]
        cid = int(a[1]) * 256 + int(a[2])
        act = ACTS.get(int(a[4]))
        code = id_to_code.get(cid, 0)
        if cid and act: chosen[(code, act)] += 1
        raw = np.ascontiguousarray(obs["global_"][0]).tobytes() + np.ascontiguousarray(offered).tobytes()
        seen[hashlib.sha1(raw).digest()] += 1
        trace.update(raw); trace.update(bytes([action & 255]))
        log.write(json.dumps({"s": steps, "p": player, "c": code, "m": int(a[3]), "a": int(a[4])}) + "\n"); log.flush()
        steps += 1
        if steps >= MAX_STEPS + 2:
            status = "decision_limit"; break
        obs, rewards, terminated, truncated, info = env.step(np.asarray([action]), np.asarray(info["env_id"]))
        if truncated[0]:
            status = "truncated" if steps >= MAX_STEPS else "core_timeout"
            break
        if terminated[0]:
            winner = int(info["winner"][0]); reason = int(info["win_reason"][0])
            if winner not in (0, 1, 2): status = "bad_terminal"
            break
    result = {"id": duel["id"], "status": status, "steps": steps, "max_options": max_options,
              "max_repeat": max(seen.values()) if seen else 0, "trace": trace.hexdigest(),
              "chosen": [[c, a, n] for (c, a), n in chosen.items()], "violations": dict(violations),
              "prompts": {str(k): v for k, v in prompts.items()}, "terminal": winner is not None}
    log.write(json.dumps({"end": result}) + "\n"); log.flush()
    sys.stderr.write(f"@@end {duel['id']}\n"); sys.stderr.flush()
    return status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--codes", type=Path, required=True)
    args = parser.parse_args()
    import ygoenv
    from ygoenv.ygopro import init_module
    spec = json.loads(args.spec.read_text())
    init_module(str(ROOT / "assets/locale/en/cards.cdb"), str(args.codes), spec["decks"])
    lines = args.codes.read_text().splitlines()
    id_to_code = {i + 1: int(line.split()[0]) for i, line in enumerate(lines)}
    db = sqlite3.connect(ROOT / "assets/locale/en/cards.cdb")
    metadata = {}
    for cid, code in id_to_code.items():
        name, atk, defense, level = db.execute(
            "select name,atk,def,level from datas join texts using(id) where id=?", (code,)).fetchone()
        metadata[cid] = {"code": code, "name": name, "atk": atk, "def": defense, "level": level & 255}

    def make(duel):
        return ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=1, num_threads=1, seed=duel["seed"],
                           deck1=duel["deck0"], deck2=duel["deck1"], play_mode="self", player=-1,
                           async_reset=False, greedy_reward=False, oppo_info=False,
                           max_options=MAX_OPTIONS, max_steps=MAX_STEPS, max_episode_steps=MAX_STEPS,
                           timeout=10, verbose=False)
    with args.log.open("a") as log:
        for duel in spec["duels"]:
            status = play(make, duel, log, id_to_code, metadata)
            if status in ("core_timeout", "no_options"):
                log.flush(); sys.stdout.flush(); os._exit(3)
    sys.stdout.flush()
    os._exit(0)


if __name__ == "__main__":
    main()
