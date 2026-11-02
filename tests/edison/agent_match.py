#!/usr/bin/env python3
"""Audited fixed-deck random/heuristic/learned production evaluation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
DECKS = ROOT / "assets/edison/decks"
sys.path.append(str(ROOT))
sys.path.insert(0, str(ROOT/"ygoenv"))
PROFILE = ROOT / "assets/edison/agent_codes.txt"
MAX_STEPS = 10000
MAX_OPTIONS = 256

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def worker(args):
    import numpy as np
    import ygoenv
    import sqlite3
    from ygoai.edison_policy import Heuristic, ContextHeuristic
    from ygoenv.ygopro import init_module
    init_module(str(ROOT / "assets/locale/en/cards.cdb"),
                str(PROFILE),
                {p.stem: str(p) for p in sorted(DECKS.glob("*.ydk")) +
                 sorted((ROOT/"assets/edison/agent_decks").glob("*.ydk"))})
    env = ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=args.num_envs,
                     num_threads=args.threads or args.num_envs, seed=args.seed,
                     deck1=args.deck1, deck2=args.deck2, play_mode="self",
                     player=-1, async_reset=False, greedy_reward=False, oppo_info=False,
                     max_options=MAX_OPTIONS, max_steps=MAX_STEPS,
                     max_episode_steps=MAX_STEPS, timeout=10, verbose=args.verbose)
    db=sqlite3.connect(ROOT/"assets/locale/en/cards.cdb")
    metadata={}
    for cid,line in enumerate(PROFILE.read_text().splitlines(),1):
        code=int(line.split()[0])
        name,atk,defense,level=db.execute(
            "select name,atk,def,level from datas join texts using(id) where id=?",
            (code,)).fetchone()
        metadata[cid]={"code":code,"name":name,"atk":atk,"def":defense,"level":level&255}
    from ygoai.edison_rl import RLModel, RLAgent
    has_rl="rl" in (args.policy0,args.policy1)
    if has_rl and args.model is None: raise ValueError("RL policy requires a checkpoint")
    if has_rl and args.collect: raise ValueError("Use RL rollouts, not imitation labels")
    if args.rl_rollout and (args.policy0,args.policy1).count("rl")!=1:
        raise ValueError("RL rollout requires exactly one learner seat")
    rl_model=RLModel.load(args.model) if has_rl else None
    weights=np.asarray(json.loads(args.model.read_text())["weights"],dtype=np.float32) if args.model and not has_rl else None
    if args.collect and "context" in (args.policy0,args.policy1):
        raise ValueError("Context policy collection needs a new learned feature schema")
    agents=[[(RLAgent(metadata,args.seed+eid*2+p,rl_model)
              if (args.policy0,args.policy1)[p]=="rl" else
              (ContextHeuristic if (args.policy0,args.policy1)[p]=="context" else Heuristic)(
                  metadata,args.seed+eid*2+p,weights if (args.policy0,args.policy1)[p]=="learned" else None))
             for p in range(2)] for eid in range(args.num_envs)]
    rl_x,rl_offsets,rl_actions,rl_logp,rl_values,rl_games,rl_players=[],[0],[],[],[],[],[]
    samples,labels,offsets,game_ids=[],[],[0],[]
    rng = [random.Random((args.seed+i) ^ 0xED150) for i in range(args.num_envs)]
    hashes = [hashlib.sha256() for _ in rng]
    steps = [0 for _ in rng]
    results = {}
    card_ids = {i+1: int(line.split()[0]) for i, line in enumerate(
        PROFILE.read_text().splitlines())}
    interactions = [{} for _ in rng]
    prompt_counts = {}
    context_counts = {}
    selected_seen=mandatory_seen=hidden_selected_seen=0
    max_choices = 0
    longest_step = 0.0
    start = time.perf_counter()
    obs, info = env.reset()
    with (args.output / f"trace-{args.worker}.jsonl").open("w", buffering=1) as log:
        while len(results) < args.num_envs:
            ids = np.asarray(info["env_id"], dtype=np.int32)
            actions, previous = [], {}
            for row, eid in enumerate(ids.tolist()):
                count = int(info["num_options"][row])
                assert 0 < count <= MAX_OPTIONS and not info["action_overflow"][row], ("action overflow", eid, count)
                assert int(info["legal_options"][row])==count
                cards=obs["cards_"][row]
                offered=obs["actions_"][row,:count]
                # The policy and data collector share this fail-closed contract.
                identities=cards[:,0].astype(np.int32)*256+cards[:,1]
                assert all(int(cid) in metadata for cid in identities if cid)
                hand=(cards[:,2]==2)&(cards[:,4]==0)
                assert np.all(identities[hand]>0), "Own hand identities missing"
                hidden=(identities==0)&(cards[:,2]>0)
                assert not np.any(cards[hidden,7:]), "Hidden statistics leaked"
                for a in offered:
                    if a[0]:
                        assert int(a[0])<=len(cards)
                        assert tuple(a[1:3])==tuple(cards[int(a[0])-1,:2]), "Action/card identity mismatch"
                context=obs["decision_"][row]
                selection=obs["selection_"][row]
                assert context[0]==1 and context.shape==(12,)
                assert selection.shape==(len(cards),3) and np.all(selection<=1)
                assert int(context[6])==int(selection[:,1].sum())
                assert int(context[7])==int(selection[:,2].sum())
                assert np.all(selection[:,2]<=selection[:,1])
                assert not np.any(selection[cards[:,2]==0]), "Selection points to padding"
                if context[11]:
                    assert context[1]==0 or 500<=context[1]<=575, "Private hint payload"
                    assert context[2] in (0,1,2,3)
                    assert context[4]>=0 and context[5]>=context[4]
                    if context[3]:
                        assert identities[int(context[3])-1]>0, "Source reveals a private row"
                    expected_candidates=np.zeros(len(cards),dtype=np.uint8)
                    for a in offered:
                        if a[0]: expected_candidates[int(a[0])-1]=1
                    assert np.array_equal(selection[:,0],expected_candidates)
                    assert context[8]==bool(np.any(offered[:,5]))
                    assert context[9]==bool(np.any(offered[:,4]==9))
                    key=f"{int(context[1])}:{int(context[2])}:{int(offered[0,3])}"
                    context_counts[key]=context_counts.get(key,0)+1
                    selected_seen+=int(context[6]>0)
                    mandatory_seen+=int(context[7]>0)
                    hidden_selected_seen+=int(np.any(selection[identities==0,1]))
                else:
                    assert not np.any(context[1:]) and not np.any(selection), "Stale selection context"
                max_choices = max(max_choices, count)
                player = int(info["to_play"][row])
                assert player in (0, 1)
                policy=(args.policy0,args.policy1)[player]
                if policy=="random":
                    action=rng[eid].randrange(count)
                elif policy=="rl":
                    action,features,old_logp,value=agents[eid][player].choose(
                        cards,obs["global_"][row],offered,context,selection)
                    if args.rl_rollout:
                        rl_x.extend(features);rl_offsets.append(len(rl_x))
                        rl_actions.append(action);rl_logp.append(old_logp);rl_values.append(value)
                        rl_games.append(eid);rl_players.append(player)
                else:
                    context_args=(context,selection) if policy=="context" else ()
                    action,features,label=agents[eid][player].choose(
                        cards,obs["global_"][row],offered,*context_args)
                    if args.collect:
                        samples.extend(features)
                        labels.append(label)
                        offsets.append(len(samples))
                        game_ids.append(eid)
                previous[eid] = player
                actions.append(action)
                # Ignore padding/history buffers. Hash all visible card features,
                # global game features, and the exact offered action rows.
                state_hash = hashlib.sha256()
                for key in ("cards_", "global_", "decision_", "selection_"):
                    state_hash.update(np.ascontiguousarray(obs[key][row]).tobytes())
                offered = np.ascontiguousarray(obs["actions_"][row, :count])
                state_hash.update(offered.tobytes())
                chosen = offered[action]
                code = card_ids.get(int(chosen[1])*256+int(chosen[2]), 0)
                interaction = f"{code}:{int(chosen[3])}:{int(chosen[4])}"
                interactions[eid][interaction] = interactions[eid].get(interaction, 0)+1
                record = {"env": eid, "step": steps[eid], "to_play": player,
                          "options": count, "action": action,
                          "offered": offered.tolist(), "state": state_hash.hexdigest(),
                          "decision": context.tolist(),
                          "selected_rows": (np.flatnonzero(selection[:,1])+1).tolist(),
                          "mandatory_rows": (np.flatnonzero(selection[:,2])+1).tolist()}
                line = json.dumps(record, sort_keys=True, separators=(",", ":"))
                hashes[eid].update(line.encode())
                log.write(line + "\n")
                msg = str(int(offered[0, 3]))
                prompt_counts[msg] = prompt_counts.get(msg, 0) + 1
                steps[eid] += 1
                assert steps[eid] < MAX_STEPS, ("decision limit; not a completed duel", eid)
            before = time.perf_counter()
            obs, rewards, terminated, truncated, info = env.step(np.asarray(actions), ids)
            longest_step = max(longest_step, time.perf_counter()-before)
            assert not np.any(truncated), "Environment truncation is not a natural win"
            keep = []
            for row, eid in enumerate(np.asarray(info["env_id"]).tolist()):
                if terminated[row]:
                    assert not np.any(obs["decision_"][row]) and not np.any(obs["selection_"][row])
                    reward = float(rewards[row])
                    winner=int(info["winner"][row])
                    assert winner in (0,1,2)
                    expected=0 if winner==2 else 1 if winner==previous[eid] else -1
                    assert reward==expected, ("terminal outcome mismatch", reward,winner)
                    results[eid] = {"env": eid, "seed": args.seed+eid, "steps": steps[eid],
                                    "winner": winner,
                                    "win_reason": int(info["win_reason"][row]),
                                    "trace_sha256": hashes[eid].hexdigest(),
                                    "interactions": dict(sorted(interactions[eid].items()))}
                    log.write(json.dumps({"terminal": results[eid]}, sort_keys=True)+"\n")
                else:
                    assert float(rewards[row]) == 0
                    keep.append(row)
            if keep:
                obs = {k: v[keep] for k, v in obs.items()}
                info = {k: info[k][keep] for k in ("env_id", "num_options", "to_play", "win_reason", "winner", "legal_options", "action_overflow")}
    elapsed = time.perf_counter()-start
    if args.collect:
        np.savez_compressed(args.output/f"samples-{args.worker}.npz",
            features=np.asarray(samples,dtype=np.float32),labels=np.asarray(labels),
            offsets=np.asarray(offsets),game_ids=np.asarray(game_ids))
    if args.rl_rollout:
        returns=[0 if results[eid]["winner"]==2 else
                 1 if results[eid]["winner"]==player else -1
                 for eid,player in zip(rl_games,rl_players)]
        np.savez_compressed(args.output/f"rollout-{args.worker}.npz",
            features=np.asarray(rl_x,dtype=np.float32),offsets=np.asarray(rl_offsets),
            actions=np.asarray(rl_actions),logp=np.asarray(rl_logp),
            values=np.asarray(rl_values),returns=np.asarray(returns,dtype=np.float64),
            game_ids=np.asarray(rl_games),players=np.asarray(rl_players))
    report = {"status": "PASS",
              "rl_model_sha256":digest(args.model) if has_rl else None,
              "learner_decisions":len(rl_actions),
              "configuration":{"seed":args.seed,"num_envs":args.num_envs,"threads":args.threads,
                               "deck0":args.deck1,"deck1":args.deck2,
                               "policy0":args.policy0,"policy1":args.policy1},
              "duels": [results[i] for i in sorted(results)],
              "decisions": sum(steps), "elapsed_seconds": elapsed,
              "decisions_per_second": sum(steps)/elapsed,
              "duels_per_second": len(results)/elapsed,
              "longest_step_seconds": longest_step, "max_options_seen": max_choices,
              "prompt_counts": prompt_counts,
              "context_counts":dict(sorted(context_counts.items())),
              "selected_prompts":selected_seen, "mandatory_prompts":mandatory_seen,
              "hidden_selected_prompts":hidden_selected_seen}
    (args.output / f"worker-{args.worker}.json").write_text(json.dumps(report, indent=2)+"\n")

def run_worker(seed, num_envs, threads, deck1, deck2, output, run=1,
               policy0="heuristic", policy1="random", model=None, collect=False, rl_rollout=False):
    output=output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cmd = [sys.executable, str(Path(__file__).resolve()), "--worker", str(run),
           "--seed", str(seed), "--num-envs", str(num_envs), "--threads", str(threads),
           "--deck1", deck1, "--deck2", deck2, "--output", str(output)]
    cmd += ["--policy0",policy0,"--policy1",policy1]
    if model: cmd += ["--model",str(Path(model).resolve())]
    if collect: cmd.append("--collect")
    if rl_rollout: cmd.append("--rl-rollout")
    process = subprocess.run(cmd, cwd=ROOT/"scripts", capture_output=True,
                             text=True, timeout=180)
    (output/f"stdout-{run}.txt").write_text(process.stdout)
    (output/f"stderr-{run}.txt").write_text(process.stderr)
    if process.returncode:
        raise RuntimeError(f"Worker {run} exited {process.returncode}: "+
                           process.stderr[-2500:]+process.stdout[-2500:])
    bad = ("timeout", "Script not found", "Card not found", "Spec not found",
           "cannot find id", "MSG_RETRY", "Error", "stack traceback")
    if any(marker.lower() in (process.stdout+process.stderr).lower() for marker in bad):
        raise RuntimeError("Native diagnostic detected; inspect stdout/stderr")
    result=json.loads((output/f"worker-{run}.json").read_text())
    assert result["configuration"]=={"seed":seed,"num_envs":num_envs,"threads":threads,
        "deck0":deck1,"deck1":deck2,"policy0":policy0,"policy1":policy1}, "Worker configuration mismatch"
    if "rl" in (policy0,policy1):
        assert result["rl_model_sha256"]==digest(Path(model)), "Worker used a different RL checkpoint"
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker",type=int,default=1)
    parser.add_argument("--seed",type=int,default=20110424)
    parser.add_argument("--num-envs",type=int,default=16)
    parser.add_argument("--threads",type=int,default=4)
    parser.add_argument("--deck1",default="blackwing")
    parser.add_argument("--deck2",default="frog_monarch")
    parser.add_argument("--policy0",choices=["random","heuristic","learned","context","rl"],default="heuristic")
    parser.add_argument("--policy1",choices=["random","heuristic","learned","context","rl"],default="random")
    parser.add_argument("--model",type=Path)
    parser.add_argument("--collect",action="store_true")
    parser.add_argument("--rl-rollout",action="store_true")
    parser.add_argument("--verbose",action="store_true")
    parser.add_argument("--output",type=Path,default=ROOT/"build/edison-agents/probe")
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    worker(args)
    return 0

if __name__=="__main__":
    raise SystemExit(main())
