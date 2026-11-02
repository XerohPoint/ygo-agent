#!/usr/bin/env python3
"""Bounded, outcome-reward PPO pilot on the certified Edison environment."""
import argparse
import copy
import itertools
import json
from pathlib import Path
import time
import numpy as np
from agent_match import ROOT,digest,run_worker
from evaluate_agents import DECKS,evaluate
from ygoai.edison_rl import RLModel,Adam,distribution,ppo_loss_grad

TRAIN_BASE=21000000
TEST_BASE=22000000
ROUNDS=8
GAMES_PER_CELL=8
EVAL_COUNT=16
OPTIMIZER_SEED=1701

def schedule():
    rows=[]
    for iteration in range(ROUNDS):
        for cell,(decks,seat) in enumerate(itertools.product(itertools.permutations(DECKS,2),(0,1))):
            rows.append({"round":iteration,"cell":cell,"deck0":decks[0],"deck1":decks[1],
                "seat":seat,"opponent":"random" if iteration%2==0 else "heuristic",
                "seed":TRAIN_BASE+iteration*100000+cell*1000,"games":GAMES_PER_CELL})
    training={row["seed"]+i for row in rows for i in range(row["games"])}
    held_out=set(range(TEST_BASE,TEST_BASE+EVAL_COUNT))
    assert not training&held_out and len(training)==768
    return rows

def gate(path):
    report=json.loads(path.read_text())
    assert report["status"]=="PASS" and report["deterministic_replay"]
    binary=next((ROOT/"ygoenv/ygoenv/ygopro").glob("ygopro_ygoenv*.so"))
    assert digest(binary)==report["production_binary_sha256"]
    for relative,expected in report["source_sha256"].items():
        assert digest(ROOT/relative)==expected, ("Source changed",relative)
    lock=json.loads((ROOT/"tests/edison/agent_assets.lock.json").read_text())
    for relative,expected in lock.items():
        assert digest(ROOT/relative)==expected, ("Asset changed",relative)
    return {"report_sha256":digest(path),"production_binary_sha256":digest(binary),
            "source_sha256":report["source_sha256"],"assets_sha256":lock}

def load_rollouts(paths,model):
    features=[];sizes=[];actions=[];logp=[];values=[];returns=[];games=[];game_offset=0
    for path in paths:
        with np.load(path) as d:
            features.append(d["features"]);sizes.extend(np.diff(d["offsets"]))
            actions.extend(d["actions"]);logp.extend(d["logp"]);values.extend(d["values"])
            returns.extend(d["returns"]);games.extend(d["game_ids"]+game_offset)
            game_offset+=GAMES_PER_CELL
    x=np.concatenate(features);sizes=np.asarray(sizes)
    offsets=np.r_[0,np.cumsum(sizes)]
    actions=np.asarray(actions);logp=np.asarray(logp)
    returns=np.asarray(returns);games=np.asarray(games)
    weights=1/np.bincount(games)[games]
    logits,_=model.forward(x);_,actual_lp,_=distribution(logits,offsets)
    assert np.allclose(logp,actual_lp[offsets[:-1]+actions],atol=1e-7), "Rollout was not on-policy"
    advantages=returns-np.asarray(values)
    mean=np.average(advantages,weights=weights)
    std=np.sqrt(np.average((advantages-mean)**2,weights=weights))
    advantages=(advantages-mean)/max(std,1e-8)
    assert np.isfinite(x).all() and np.isfinite(advantages).all()
    return x,offsets,actions,logp,advantages,returns,weights

def update(model,optimizer,data,seed):
    x,offsets,actions,logp,advantages,returns,weights=data
    rng=np.random.default_rng(seed)
    history=[]
    for epoch in range(4):
        for start in range(0,len(actions),256):
            # Permute complete decision segments, never individual action rows.
            if start==0: order=rng.permutation(len(actions))
            selected=order[start:start+256]
            chunks=[x[offsets[i]:offsets[i+1]] for i in selected]
            batch_x=np.concatenate(chunks)
            batch_offsets=np.r_[0,np.cumsum([len(c) for c in chunks])]
            _,grad,_=ppo_loss_grad(model,batch_x,batch_offsets,actions[selected],
                logp[selected],advantages[selected],returns[selected],weights[selected])
            assert all(np.isfinite(g).all() for g in grad.values())
            optimizer.step(model,grad)
        _,_,stats=ppo_loss_grad(model,*data)
        stats["epoch"]=epoch+1
        history.append(stats)
        if stats["approx_kl"]>0.03: break
    assert all(np.isfinite(p).all() for p in model.p.values())
    return history

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,default=ROOT/"build/edison-rl")
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    gates=gate(output/"certification/report.json")
    rows=schedule()
    report={"status":"RUNNING","algorithm":"episodic clipped PPO",
        "feature_schema":"edison_context_ppo_v1","features":128,"hidden_units":32,
        "initialization":"uniform actor, random hidden layer, zero critic; no imitation warm start",
        "reward":"natural outcome from learner seat: win +1, loss -1, draw 0",
        "gamma":1.0,"advantage":"Monte Carlo outcome minus frozen rollout critic, weighted normalization",
        "weighting":"inverse learner-decision count per game",
        "optimizer":{"name":"Adam","learning_rate":0.002,"minibatch_decisions":256,
                     "epochs_max":4,"clip":0.2,"entropy_coefficient":0.01,
                     "value_coefficient":0.5,"gradient_norm":0.5,"epoch_kl_stop":0.03},
        "optimizer_seed":OPTIMIZER_SEED,"schedule":rows,"held_out_seed":TEST_BASE,
        "held_out_seeds_per_cell":EVAL_COUNT,"checkpoint_selection":"fixed final round; no evaluation-based selection",
        "gates":gates,"rounds":[],"evaluations":{}}
    path=output/"report.json"
    def save(): path.write_text(json.dumps(report,indent=2)+"\n")
    save()
    started=time.perf_counter()
    try:
        model=RLModel(OPTIMIZER_SEED);optimizer=Adam(model)
        model.save(output/"initial.json",{"round":0,"seed":OPTIMIZER_SEED})
        current=output/"initial.json"
        for iteration in range(ROUNDS):
            paths=[];cells=[];round_start=time.perf_counter()
            for row in [r for r in rows if r["round"]==iteration]:
                cell=output/f"training/round-{iteration+1}/cell-{row['cell']:02d}"
                policies=("rl",row["opponent"]) if row["seat"]==0 else (row["opponent"],"rl")
                result=run_worker(row["seed"],row["games"],4,row["deck0"],row["deck1"],cell,
                    policy0=policies[0],policy1=policies[1],model=current,rl_rollout=True)
                rollout=cell/"rollout-1.npz";paths.append(rollout)
                cells.append({"schedule":row,"report":result,"rollout_sha256":digest(rollout),
                              "rollout":str(rollout.relative_to(ROOT))})
            data=load_rollouts(paths,model)
            duplicate_model,duplicate_optimizer=copy.deepcopy(model),copy.deepcopy(optimizer)
            before=time.perf_counter()
            history=update(model,optimizer,data,OPTIMIZER_SEED+iteration)
            repeat=update(duplicate_model,duplicate_optimizer,data,OPTIMIZER_SEED+iteration)
            assert history==repeat and all(np.array_equal(model.p[k],duplicate_model.p[k]) for k in model.p)
            assert optimizer.t==duplicate_optimizer.t
            assert all(np.array_equal(optimizer.m[k],duplicate_optimizer.m[k]) and
                       np.array_equal(optimizer.v[k],duplicate_optimizer.v[k]) for k in model.p)
            update_seconds=time.perf_counter()-before
            current=output/f"checkpoint-{iteration+1}.json"
            model.save(current,{"round":iteration+1,"optimizer_seed":OPTIMIZER_SEED,"gamma":1})
            opt_path=output/f"optimizer-{iteration+1}.npz"
            np.savez_compressed(opt_path,t=np.asarray(optimizer.t),
                **{"m_"+k:v for k,v in optimizer.m.items()},
                **{"v_"+k:v for k,v in optimizer.v.items()})
            wins=sum(d["winner"]==c["schedule"]["seat"] for c in cells for d in c["report"]["duels"])
            decisions=sum(c["report"]["decisions"] for c in cells)
            rollout_seconds=sum(c["report"]["elapsed_seconds"] for c in cells)
            round_report={"round":iteration+1,"opponent":cells[0]["schedule"]["opponent"],
                "games":96,"wins":wins,"completion_rate":1.0,"learner_decisions":len(data[2]),
                "decisions":decisions,"rollout_seconds":rollout_seconds,
                "decisions_per_second":decisions/rollout_seconds,"optimization_seconds_including_replay":update_seconds,
                "wall_seconds":time.perf_counter()-round_start,"optimization_replay":True,
                "model_sha256":digest(current),"optimizer_sha256":digest(opt_path),
                "history":history,"cells":cells}
            report["rounds"].append(round_report);save()
            print(f"Round {iteration+1}/8: {wins}/96 wins vs {round_report['opponent']}; "
                  f"{len(data[2])} learner decisions; {decisions/rollout_seconds:.0f} decisions/s; update replay PASS",flush=True)
        report["training_seconds"]=time.perf_counter()-started
        report["final_checkpoint"]=str(current.relative_to(ROOT))
        report["final_model_sha256"]=digest(current)
        report["training_games"]=sum(v["games"] for v in report["rounds"])
        report["training_learner_decisions"]=sum(v["learner_decisions"] for v in report["rounds"])
        report["training_completion_rate"]=1.0
        save()
        # Evaluation happens only after all training. Both checkpoints and the
        # frozen benchmark use identical held-out seed/deck/seat matrices.
        for name,challenger,opponent,checkpoint in (
            ("initial-vs-random","rl","random",output/"initial.json"),
            ("initial-vs-heuristic","rl","heuristic",output/"initial.json"),
            ("final-vs-random","rl","random",current),
            ("final-vs-heuristic","rl","heuristic",current),
            ("heuristic-vs-random","heuristic","random",None)):
            result=evaluate(output/name,EVAL_COUNT,TEST_BASE,challenger,opponent,checkpoint)
            report["evaluations"][name]={k:result[k] for k in
                ("status","games","executions","wins","win_rate","completion_rate",
                 "repeatability","decisions_per_second")}
            save()
            print(name+": "+json.dumps(report["evaluations"][name]),flush=True)
        report["status"]="PASS"
    except Exception as error:
        report.update(status="FAIL",error=str(error));raise
    finally: save()
if __name__=="__main__": main()
