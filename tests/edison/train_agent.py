#!/usr/bin/env python3
"""Gated CPU behavior-cloning pilot; retains random and heuristic benchmarks.

This learns a 64-feature linear legal-action scorer from heuristic decisions.
It is an imitation-learning smoke experiment, not reinforcement learning.
"""
import argparse
import itertools
import json
from pathlib import Path
import time
import numpy as np
from agent_match import ROOT, digest, run_worker
from evaluate_agents import DECKS, evaluate

def check_gate(root):
    binary=digest(next((ROOT/"ygoenv/ygoenv/ygopro").glob("ygopro_ygoenv*.so")))
    policy=digest(ROOT/"ygoai/edison_policy.py")
    lock=json.loads((ROOT/"tests/edison/agent_assets.lock.json").read_text())
    for relative,expected in lock.items():
        assert digest(ROOT/relative)==expected, ("Asset changed",relative)
    gates={}
    for name in ("certification","evaluation","stress-random","stress-heuristic"):
        path=root/name/"report.json"
        report=json.loads(path.read_text())
        assert report["status"]=="PASS", (name,"not passed")
        assert report["production_binary_sha256"]==binary, (name,"different binary")
        assert report["source_sha256"]["ygoai/edison_policy.py"]==policy, (name,"different policy")
        for relative,expected in report["source_sha256"].items():
            assert digest(ROOT/relative)==expected, (name,"source changed",relative)
        if name!="certification":
            assert report["completion_rate"]==1 and report["repeatability"]
        gates[name]=digest(path)
    return gates

def load_samples(paths):
    xs=[];ys=[];sizes=[];validation=[]
    for path in paths:
        with np.load(path) as d:
            xs.append(d["features"])
            ys.extend(d["labels"])
            sizes.extend(np.diff(d["offsets"]))
            # Hold out complete games, not neighboring decisions from training.
            validation.extend(d["game_ids"]>=12)
    return np.concatenate(xs),np.asarray(ys),np.asarray(sizes),np.asarray(validation)

def optimize(x,y,sizes,val,epochs=120):
    offsets=np.r_[0,np.cumsum(sizes)]
    groups=np.repeat(np.arange(len(y)),sizes)
    train=(~val).astype(np.float32)
    w=np.zeros(x.shape[1],dtype=np.float32)
    target=offsets[:-1]+y
    history=[]
    def measure(w,mask):
        logits=x@w
        maxima=np.maximum.reduceat(logits,offsets[:-1])
        exp=np.exp(logits-maxima[groups])
        sums=np.add.reduceat(exp,offsets[:-1])
        loss=-logits[target]+maxima+np.log(sums)
        choices=np.array([np.argmax(logits[a:b]) for a,b in zip(offsets[:-1],offsets[1:])])
        return {"loss":float(loss[mask].mean()),"accuracy":float((choices[mask]==y[mask]).mean())}
    for epoch in range(epochs):
        logits=x@w
        maxima=np.maximum.reduceat(logits,offsets[:-1])
        exp=np.exp(logits-maxima[groups])
        probs=exp/np.add.reduceat(exp,offsets[:-1])[groups]
        probs[target]-=1
        gradient=x.T@(probs*train[groups])/train.sum()+1e-4*w
        w-=np.float32(0.4)*gradient
        if epoch in (0,19,59,epochs-1):
            history.append({"epoch":epoch+1,"train":measure(w,~val),"validation":measure(w,val)})
    return w,history

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root",type=Path,default=ROOT/"build/edison-agents")
    p.add_argument("--eval-count",type=int,default=16)
    args=p.parse_args()
    root=args.root.resolve()
    gates=check_gate(root)  # No collection/optimization before all gates pass.
    output=root/"training";output.mkdir(parents=True,exist_ok=True)
    report={"status":"RUNNING","method":"linear masked-softmax behavior cloning",
            "gates_sha256":gates,"training_seed":20200424,
            "evaluation_seed":20110424,"epochs":120,"learning_rate":0.4,"l2":1e-4,
            "features":64,"validation":"env IDs 12-15; whole held-out games",
            "numpy_version":np.__version__,"source_sha256":digest(Path(__file__))}
    path=output/"report.json"
    def save(): path.write_text(json.dumps(report,indent=2)+"\n")
    save()
    try:
        datasets=[]
        for d0,d1 in itertools.permutations(DECKS,2):
            cell=output/f"{d0}-{d1}"
            result=run_worker(20200424,16,4,d0,d1,cell,policy0="heuristic",
                              policy1="heuristic",collect=True)
            datasets.append(cell/"samples-1.npz")
            print(f"Collected {d0}/{d1}: 16 complete games, {result['decisions']} decisions",flush=True)
        x,y,sizes,val=load_samples(datasets)
        before=time.perf_counter()
        w,history=optimize(x,y,sizes,val)
        # Repeat optimizer from zero to check reproducible parameter updates.
        repeated,repeated_history=optimize(x,y,sizes,val)
        assert np.array_equal(w,repeated) and history==repeated_history
        report.update(training_decisions=int((~val).sum()),validation_decisions=int(val.sum()),
            training_games=72,validation_games=24,history=history,
            optimization_seconds=time.perf_counter()-before,optimizer_repeatability=True,
            dataset_sha256={str(p.relative_to(ROOT)):digest(p) for p in datasets})
        model=output/"linear_policy.json"
        model.write_text(json.dumps({"format":1,"feature_count":64,
            "method":report["method"],"weights":w.tolist()},indent=2)+"\n")
        report["model_sha256"]=digest(model)
        save()
        # Same held-out seed range and seat/deck matrix as the fixed heuristic
        # benchmark (a 16-seed subset); no evaluation games were training data.
        for opponent in ("random","heuristic"):
            result=evaluate(output/f"vs-{opponent}",args.eval_count,20110424,
                            "learned",opponent,model)
            report[f"vs_{opponent}"]={k:result[k] for k in
                ("games","executions","wins","win_rate","completion_rate",
                 "repeatability","decisions_per_second")}
            save()
        report["status"]="PASS"
        print(json.dumps({k:v for k,v in report.items() if k in
              ("training_decisions","validation_decisions","vs_random","vs_heuristic")},indent=2),flush=True)
    except Exception as error:
        report.update(status="FAIL",error=str(error))
        raise
    finally:
        save()
if __name__=="__main__":
    main()
