#!/usr/bin/env python3
"""Test 083: legal-action PPO math, context features and outcome attribution."""
import argparse
import tempfile
from pathlib import Path
import numpy as np
from agent_match import ROOT, run_worker
from agent_policy_tests import card, action
from ygoai.edison_rl import RLModel, RLAgent, ContextEncoder, distribution, ppo_loss_grad, FEATURES

def gradients():
    rng=np.random.default_rng(42)
    model=RLModel(5)
    model.p["out"][:]=rng.normal(0,0.2,32)
    x=rng.normal(0,0.3,(7,FEATURES))
    offsets=np.array([0,2,7]);actions=np.array([1,3])
    logits,_=model.forward(x)
    _,lp,_=distribution(logits,offsets)
    old=lp[offsets[:-1]+actions]
    advantages=np.array([0.7,-0.4]);returns=np.array([1.,-1.])
    weights=np.array([0.3,0.7])
    def objective(): return ppo_loss_grad(model,x,offsets,actions,old,advantages,returns,weights)
    _,grad,_=objective()
    for key,index in (("w",(3,2)),("b",(2,)),("out",(2,)),("v",(4,)),("vb",(0,))):
        original=model.p[key][index];eps=1e-5
        model.p[key][index]=original+eps;plus=objective()[0]
        model.p[key][index]=original-eps;minus=objective()[0]
        model.p[key][index]=original
        assert np.isclose((plus-minus)/(2*eps),grad[key][index],atol=2e-7,rtol=2e-4),key
    # A positive outcome must increase the chosen action's probability.
    model=RLModel(5);x=x[:2];offsets=np.array([0,2]);actions=np.array([1])
    _,lp,_=distribution(model.forward(x)[0],offsets)
    _,grad,_=ppo_loss_grad(model,x,offsets,actions,np.array([lp[1]]),
                          np.array([1.]),np.array([1.]),np.array([1.]),entropy_coef=0)
    for key in model.p: model.p[key]-=0.01*grad[key]
    _,new_lp,_=distribution(model.forward(x)[0],offsets)
    assert new_lp[1]>lp[1]
    # Clipped positive ratio has no actor gradient beyond the upper clip.
    _,grad,_=ppo_loss_grad(model,x,offsets,actions,np.array([new_lp[1]-np.log(2)]),
                          np.array([1.]),np.array([0.]),np.array([1.]),entropy_coef=0)
    assert np.allclose(grad["out"],0) and np.allclose(grad["w"],0)

def schema():
    cards=np.array([card(1,2400),card(0,0,enemy=1,pos=6)])
    glob=np.zeros(23,dtype=np.uint8)
    actions=np.array([action(2,0,msg=3),action(1,1,msg=3)])
    context=np.zeros(12,dtype=np.int32);context[0]=1;context[1]=549;context[3]=1;context[11]=1
    selection=np.zeros((2,3),dtype=np.uint8);selection[:,0]=1
    encoder=ContextEncoder({})
    x=encoder.encode(cards,glob,actions,context,selection)
    assert x.shape==(2,128) and np.isfinite(x).all()
    changed=context.copy();changed[1]=502;changed[2]=2
    assert not np.array_equal(x,encoder.encode(cards,glob,actions,changed,selection))
    selection[0,1:]=1;changed[6:8]=1
    assert not np.array_equal(x,encoder.encode(cards,glob,actions,changed,selection))
    with tempfile.TemporaryDirectory() as tmp:
        path=Path(tmp)/"model.json";model=RLModel();model.save(path)
        loaded=RLModel.load(path)
        assert all(np.array_equal(v,loaded.p[k]) for k,v in model.p.items())
        one,two=RLAgent({},13,model),RLAgent({},13,loaded)
        for _ in range(20):
            a=one.choose(cards,glob,actions,context,selection)[0]
            b=two.choose(cards,glob,actions,context,selection)[0]
            assert a==b and 0<=a<len(actions)
        # Private source references must fail closed, even if caller skips audit.
        context[3]=2
        try: encoder.encode(cards,glob,actions,context,selection)
        except ValueError: pass
        else: raise AssertionError("Private source accepted")

def production():
    with tempfile.TemporaryDirectory(prefix="edison-rl-test-") as tmp:
        path=Path(tmp);model=path/"model.json";RLModel(17).save(model)
        for seat in (0,1):
            policies=("rl","heuristic") if seat==0 else ("heuristic","rl")
            one=run_worker(20300424,4,1,"blackwing","frog_monarch",path/f"s{seat}-one",
                           policy0=policies[0],policy1=policies[1],model=model,rl_rollout=True)
            many=run_worker(20300424,4,4,"blackwing","frog_monarch",path/f"s{seat}-many",
                            policy0=policies[0],policy1=policies[1],model=model,rl_rollout=True)
            assert one["duels"]==many["duels"]
            with np.load(path/f"s{seat}-one/rollout-1.npz") as data:
                assert len(data["actions"])>0
                for i,eid in enumerate(data["game_ids"]):
                    winner=one["duels"][int(eid)]["winner"]
                    expected=0 if winner==2 else 1 if winner==seat else -1
                    assert data["returns"][i]==expected and data["players"][i]==seat
                assert np.all(data["actions"]<np.diff(data["offsets"]))
                # Fresh uniform actor: likelihoods must match offered counts.
                assert np.allclose(data["logp"],-np.log(np.diff(data["offsets"])))
        print("Observed: legal stochastic sampling, frozen-opponent outcomes, both seats, identical parallel replays.")

def seed_partition():
    from train_context_rl import schedule, TEST_BASE, EVAL_COUNT
    rows=schedule()
    seeds=[row["seed"]+eid for row in rows for eid in range(row["games"])]
    assert len(seeds)==len(set(seeds))==768
    assert set(seeds).isdisjoint(range(TEST_BASE,TEST_BASE+EVAL_COUNT))
    assert sum(row["games"] for row in rows if row["opponent"]=="random")==384
    assert sum(row["games"] for row in rows if row["opponent"]=="heuristic")==384
    assert sum(row["games"] for row in rows if row["seat"]==0)==384

def replication_config():
    from train_context_rl import parse_args
    from replicate_context_rl import aggregate, SEEDS
    assert parse_args([]).optimizer_seed==1701
    previous=None
    for seed in SEEDS:
        args=parse_args(["--optimizer-seed",str(seed),"--certification","current.json"])
        assert args.optimizer_seed==seed and args.certification==Path("current.json")
        model=RLModel(args.optimizer_seed)
        if previous is not None: assert not np.array_equal(model.p["w"],previous)
        previous=model.p["w"].copy()
        # Different initial hidden parameters still define the same uniform policy.
        x=np.arange(3*FEATURES,dtype=np.float32).reshape(3,FEATURES)/100
        probabilities,_,_=distribution(model.forward(x)[0],np.array([0,3]))
        assert np.allclose(probabilities,1/3)
    # Run-level variability must not treat repeated held-out games as new
    # independent runs, or silently drop failed replications.
    trials=[]
    for seed,rate in zip(SEEDS,(0.5,0.6,0.7)):
        evaluations={name:{"win_rate":value,"executions":384,"completion_rate":1,
                          "repeatability":True} for name,value in (
            ("final-vs-random",rate),("initial-vs-random",0.4),
            ("final-vs-heuristic",rate-0.4),("initial-vs-heuristic",0.05),
            ("heuristic-vs-random",0.95))}
        trials.append({"optimizer_seed":seed,"status":"PASS","evaluations":evaluations,
            "training_games":768,"training_learner_decisions":100,"training_seconds":60,
            "training_completion_rate":1,"rounds":[{"optimization_replay":True}]})
    result=aggregate(trials,trials[0])
    metric=result["opponents"]["random"]
    assert np.isclose(metric["win_rate"]["mean"],0.6)
    assert np.isclose(metric["win_rate"]["sample_stddev"],0.1)
    assert metric["improved_runs"]==3 and result["total_executions"]==8064
    trials[-1]["status"]="FAIL"
    try: aggregate(trials,trials[0])
    except ValueError: pass
    else: raise AssertionError("Failed replication silently included")

def budget_protocol():
    from train_context_rl import schedule,parse_args
    from budget_context_rl import protocol,seal_models,verify_seal,SEEDS,CHECKPOINTS
    spec=protocol();rows=spec["schedule"]
    assert parse_args([]).rounds==8 and not parse_args([]).skip_evaluation
    args=parse_args(["--rounds","32","--skip-evaluation"])
    assert args.rounds==32 and args.skip_evaluation
    assert rows[:96]==schedule() and sum(r["games"] for r in rows)==3072
    for opponent in ("random","heuristic"):
        assert sum(r["games"] for r in rows if r["opponent"]==opponent)==1536
    for seat in (0,1):
        assert sum(r["games"] for r in rows if r["seat"]==seat)==1536
    assert spec["validation_seed"]!=spec["test_seed"]
    # Test leakage guard: modifying a frozen checkpoint must fail closed.
    with tempfile.TemporaryDirectory() as tmp:
        root=Path(tmp)
        for seed in SEEDS:
            (root/f"seed-{seed}").mkdir()
            for c in CHECKPOINTS:
                name="initial.json" if c==0 else f"checkpoint-{c}.json"
                (root/f"seed-{seed}"/name).write_text(str((seed,c)))
        seal=seal_models(root);verify_seal(root,seal)
        (root/f"seed-{SEEDS[0]}"/"checkpoint-32.json").write_text("changed")
        try: verify_seal(root,seal)
        except ValueError: pass
        else: raise AssertionError("Frozen checkpoint mutation accepted")

def main():
    parser=argparse.ArgumentParser();parser.add_argument("--fail-fast",action="store_true")
    args=parser.parse_args();passed=failed=0
    for label,fn in (("A - PPO GRADIENTS",gradients),("B - CONTEXT SCHEMA AND CHECKPOINT",schema),
                     ("C - PRODUCTION RL ROLLOUT",production),
                     ("D - DISJOINT BALANCED SEED SCHEDULE",seed_partition),
                     ("E - REPLICATION CONFIGURATION AND STATISTICS",replication_config),
                     ("F - BUDGET STUDY PARTITIONS AND CHECKPOINT FREEZE",budget_protocol)):
        print("EDISON RULE TEST 083"+label)
        try: fn();passed+=1;print("RESULT: PASS")
        except Exception as error:
            failed+=1;print("RESULT: FAIL - "+str(error))
            if args.fail_fast: break
    print(f"SUMMARY: {passed} PASS, {failed} FAIL")
    return bool(failed)
if __name__=="__main__": raise SystemExit(main())
