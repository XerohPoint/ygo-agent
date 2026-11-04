#!/usr/bin/env python3
"""Run a batch of single-env duels through the audit or production env module.

Duels and decks come from a JSON spec. Every duel is written with flushes, so a native
abort leaves its id on disk. Runs with cwd=scripts because script paths are relative.
"""
import argparse, hashlib, importlib.util, json, os, random, sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "ygoenv"))
MAX_STEPS, MAX_OPTIONS = 10000, 256
ACTS = {1: "set", 2: "repo", 3: "spsummon", 4: "summon", 5: "mset", 6: "attack", 7: "direct", 8: "activate"}

def load_module(args):
    if args.so or args.module == "audit":
        so = args.so or ROOT / "build/edison-pool-audit-2026-11-04/audit_module/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so"
        spec = importlib.util.spec_from_file_location("ygoenv.ygopro.ygopro_ygoenv", so)
        module = importlib.util.module_from_spec(spec)
        sys.modules["ygoenv.ygopro.ygopro_ygoenv"] = module
        spec.loader.exec_module(module)

def check_contracts(obs, row, count, id_to_code):
    """Same fail-closed observation contract as tests/edison/agent_match.py; returns text or None."""
    import numpy as np
    cards = obs["cards_"][row]
    offered = obs["actions_"][row, :count]
    identities = cards[:, 0].astype(np.int32) * 256 + cards[:, 1]
    for cid in identities:
        if cid and int(cid) not in id_to_code: return "unknown visible identity"
    if not np.all(identities[(cards[:, 2] == 2) & (cards[:, 4] == 0)] > 0): return "own hand identity missing"
    if np.any(cards[(identities == 0) & (cards[:, 2] > 0), 7:]): return "hidden statistics leaked"
    for a in offered:
        if a[0]:
            if int(a[0]) > len(cards): return "action row out of range"
            if tuple(a[1:3]) != tuple(cards[int(a[0]) - 1, :2]): return "action/card identity mismatch"
    context, selection = obs["decision_"][row], obs["selection_"][row]
    if context[0] != 1: return "decision context not active"
    if int(context[6]) != int(selection[:, 1].sum()) or int(context[7]) != int(selection[:, 2].sum()): return "selection counters"
    if np.any(selection[:, 2] > selection[:, 1]): return "mandatory not selected"
    if np.any(selection[cards[:, 2] == 0]): return "selection points to padding"
    if context[11]:
        if not (context[1] == 0 or 500 <= context[1] <= 575): return f"hint payload {int(context[1])}"
        if context[2] not in (0, 1, 2, 3): return "hint kind"
        if context[4] < 0 or context[5] < context[4]: return "selection bounds"
        if context[3] and identities[int(context[3]) - 1] <= 0: return "source reveals private row"
        expected = np.zeros(len(cards), dtype=np.uint8)
        for a in offered:
            if a[0]: expected[int(a[0]) - 1] = 1
        if not np.array_equal(selection[:, 0], expected): return "candidate rows differ from offered rows"
        if bool(context[8]) != bool(np.any(offered[:, 5])): return "finish flag"
        if bool(context[9]) != bool(np.any(offered[:, 4] == 9)): return "cancel flag"
    elif np.any(context[1:]) or np.any(selection): return "stale selection context"
    return None

def choose(policy, rng, offered, count):
    if policy == "eager" and rng.random() < 0.8:
        wanted = [i for i in range(count) if offered[i, 4] in (3, 4, 8)]
        if wanted: return rng.choice(wanted)
    return rng.randrange(count)

def play(env_make, duel, log, audit_fd, id_to_code):
    import numpy as np
    if audit_fd is not None: os.write(audit_fd, f"D {duel['id']}\n".encode())
    log.write(json.dumps({"begin": duel["id"]}) + "\n"); log.flush()
    env = env_make(duel)
    rng = random.Random(duel["seed"] ^ 0xED150)
    trace = hashlib.sha256()
    chosen, offered_count, prompts = Counter(), Counter(), Counter()
    violations = Counter()
    seen = Counter()
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
        problem = check_contracts(obs, 0, count, id_to_code)
        if problem: violations[problem] += 1
        for a in offered:
            cid = int(a[1]) * 256 + int(a[2])
            if cid:
                code = id_to_code.get(cid, -cid)
                offered_count[code] += 1
        action = choose(duel["policy"], rng, offered, count)
        a = offered[action]
        cid = int(a[1]) * 256 + int(a[2])
        act = ACTS.get(int(a[4]))
        if cid and act: chosen[(id_to_code.get(cid, -cid), act)] += 1
        raw = np.ascontiguousarray(obs["global_"][0]).tobytes() + np.ascontiguousarray(offered).tobytes()
        seen[hashlib.sha1(raw).digest()] += 1
        trace.update(raw); trace.update(bytes([action & 255]))
        last = {"step": steps, "player": int(info["to_play"][0]), "action": a.tolist(), "code": id_to_code.get(cid, 0)}
        if steps % 16 == 0 or act in ("activate",):
            log.write(json.dumps({"last": last}) + "\n"); log.flush()
        steps += 1
        if steps >= MAX_STEPS + 2:
            status = "decision_limit"; break
        obs, rewards, terminated, truncated, info = env.step(np.asarray([action]), np.asarray(info["env_id"]))
        if truncated[0]:
            legal = int(info["legal_options"][0])
            status = "truncated" if steps >= MAX_STEPS else "core_timeout"
            break
        if terminated[0]:
            winner = int(info["winner"][0]); reason = int(info["win_reason"][0])
            if winner not in (0, 1, 2): status = "bad_terminal"
            break
    result = {"id": duel["id"], "status": status, "steps": steps, "winner": winner, "reason": reason,
              "max_options": max_options, "max_repeat": max(seen.values()) if seen else 0,
              "trace": trace.hexdigest(),
              "chosen": [[c, a, n] for (c, a), n in chosen.items()],
              "offered": list(offered_count.items()), "violations": dict(violations),
              "prompts": {str(k): v for k, v in prompts.items()}}
    log.write(json.dumps({"end": result}) + "\n"); log.flush()
    return status

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--audit-log", type=Path, required=True)
    parser.add_argument("--codes", type=Path, required=True)
    parser.add_argument("--module", choices=["audit", "production"], default="audit")
    parser.add_argument("--so", type=Path, help="Load this module file instead, to compare builds")
    args = parser.parse_args()
    os.environ["EDISON_AUDIT_LOG"] = str(args.audit_log)
    load_module(args)
    import ygoenv
    from ygoenv.ygopro import init_module
    spec = json.loads(args.spec.read_text())
    decks = {name: str(path) for name, path in spec["decks"].items()}
    init_module(str(ROOT / "assets/locale/en/cards.cdb"), str(args.codes), decks)
    id_to_code = {i + 1: int(line.split()[0]) for i, line in enumerate(args.codes.read_text().splitlines())}
    def make(duel):
        return ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=1, num_threads=1, seed=duel["seed"],
                           deck1=duel["deck0"], deck2=duel["deck1"], play_mode="self", player=-1,
                           async_reset=False, greedy_reward=False, oppo_info=False,
                           max_options=MAX_OPTIONS, max_steps=MAX_STEPS, max_episode_steps=MAX_STEPS,
                           timeout=10, verbose=False)
    audit_fd = os.open(args.audit_log, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o644) if args.module == "audit" else None
    with args.log.open("a") as log:
        for duel in spec["duels"]:
            status = play(make, duel, log, audit_fd, id_to_code)
            if status in ("core_timeout", "no_options"):
                log.flush(); sys.stdout.flush(); os._exit(3)
    sys.stdout.flush()
    os._exit(0)

if __name__ == "__main__":
    main()
