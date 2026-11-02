#!/usr/bin/env python3
"""Predeclared 768 versus 3072 duel study, with separate validation and test seeds."""
import argparse
import gzip
import json
import os
from pathlib import Path
import subprocess
import sys
from agent_match import ROOT,digest
from evaluate_agents import evaluate
from replicate_context_rl import read,distribution,SEEDS
from train_context_rl import gate,schedule

VALIDATION_BASE=40000000
TEST_BASE=50000000
VALIDATION_COUNT=8
TEST_COUNT=16
CHECKPOINTS=(0,8,16,24,32)
REFERENCE=ROOT/"tests/edison/reports/rl-replication-2026-11-02"

def protocol():
    rows=schedule(32)
    training={r["seed"]+i for r in rows for i in range(r["games"])}
    validation=set(range(VALIDATION_BASE,VALIDATION_BASE+VALIDATION_COUNT))
    test=set(range(TEST_BASE,TEST_BASE+TEST_COUNT))
    old_test=set(range(22000000,22000016))
    assert len(training)==3072
    assert not training & (validation|test|old_test)
    assert not validation & (test|old_test) and not test & old_test
    assert rows[:96]==schedule()
    return {"optimizer_seeds":list(SEEDS),"training_games_per_run":3072,
        "rounds":32,"validation_checkpoints":list(CHECKPOINTS),
        "validation_seed":VALIDATION_BASE,"validation_seeds_per_cell":VALIDATION_COUNT,
        "test_seed":TEST_BASE,"test_seeds_per_cell":TEST_COUNT,
        "selection":"fixed round 32; compare round 8 on identical fresh test seeds",
        "execution_order":"all training, all validation, freeze manifest, then final test",
        "sampling":"stochastic legal-action sampling, fixed per-duel sampling seeds",
        "schedule":rows}

def model_path(root,seed,round_):
    return root/f"seed-{seed}"/("initial.json" if round_==0 else f"checkpoint-{round_}.json")

def seal_models(root):
    return {str(model_path(root,s,c).relative_to(root)):digest(model_path(root,s,c))
            for s in SEEDS for c in CHECKPOINTS}

def verify_seal(root,seal):
    for name,sha in seal.items():
        if digest(root/name)!=sha: raise ValueError("Frozen checkpoint changed: "+name)

def metric(ev):
    out={k:ev[k] for k in ("status","games","executions","wins","win_rate",
        "completion_rate","repeatability","max_options_seen","longest_step_seconds")}
    for i,label in enumerate(("serial","parallel")):
        runs=[c["runs"][i] for c in ev["cells"]]
        out[label+"_decisions_per_second"]=sum(r["decisions"] for r in runs)/sum(r["elapsed_seconds"] for r in runs)
    return out

def summarize(report):
    assert report["phase"]=="test_complete"
    assert len(report["training"])==3
    assert len(report["validation"])==30 and len(report["test"])==21
    result={"opponents":{},"validation_curve":{}}
    for opponent in ("random","heuristic"):
        by_round={c:[report["test"][f"{s}/round-{c}-vs-{opponent}"] for s in SEEDS] for c in (0,8,32)}
        result["opponents"][opponent]={
            "initial_win_rate":distribution([v["win_rate"] for v in by_round[0]]),
            "short_win_rate":distribution([v["win_rate"] for v in by_round[8]]),
            "long_win_rate":distribution([v["win_rate"] for v in by_round[32]]),
            "paired_gain":distribution([long["win_rate"]-short["win_rate"] for short,long in zip(by_round[8],by_round[32])]),
            "improved_runs":sum(long["win_rate"]>short["win_rate"] for short,long in zip(by_round[8],by_round[32])),
            "serial_decisions_per_second":distribution([v["serial_decisions_per_second"] for v in by_round[32]]),
            "parallel_decisions_per_second":distribution([v["parallel_decisions_per_second"] for v in by_round[32]])}
        result["validation_curve"][opponent]={str(c*96):distribution([
            report["validation"][f"{s}/round-{c}-vs-{opponent}"]["win_rate"] for s in SEEDS]) for c in CHECKPOINTS}
    evaluations=list(report["validation"].values())+list(report["test"].values())
    training=list(report["training"].values())
    assert all(e["status"]=="PASS" and e["completion_rate"]==1 and e["repeatability"] for e in evaluations)
    assert all(t["status"]=="PASS" and t["completion_rate"]==1 and t["optimizer_replay"] for t in training)
    result.update(training_games=sum(t["games"] for t in training),
        training_seconds=distribution([t["seconds"] for t in training]),
        evaluation_executions=sum(e["executions"] for e in evaluations),
        completion_rate=1.0,evaluation_replay=True,optimizer_replay=True)
    result["total_executions"]=result["training_games"]+result["evaluation_executions"]
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,default=ROOT/"build/edison-rl-budget")
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    path=output/"report.json"
    if path.exists(): raise RuntimeError("Use a fresh output directory; completed/failed evidence is not overwritten")
    spec=protocol();gates=gate(output/"certification/report.json")
    reference=read(REFERENCE/"seed-1702/training-report.json.gz")
    for source in ("ygoai/edison_rl.py","ygoai/edison_policy.py",
                   "tests/edison/agent_match.py","tests/edison/evaluate_agents.py"):
        assert digest(ROOT/source)==reference["gates"]["source_sha256"][source],source
    assert gates["production_binary_sha256"]==reference["gates"]["production_binary_sha256"]
    assert gates["assets_sha256"]==reference["gates"]["assets_sha256"]
    report={"status":"RUNNING","phase":"training","protocol":spec,"gates":gates,
            "training":{},"validation":{},"test":{}}
    def save(): path.write_text(json.dumps(report,indent=2)+"\n")
    save()
    try:
        env=dict(os.environ,OPENBLAS_NUM_THREADS="1",OMP_NUM_THREADS="1")
        for seed in SEEDS:
            trial=output/f"seed-{seed}";trial.mkdir()
            print(f"Training optimizer seed {seed}: fixed 3072 duels",flush=True)
            with (trial/"console.txt").open("w") as log:
                subprocess.run([sys.executable,str(Path(__file__).with_name("train_context_rl.py")),
                    "--optimizer-seed",str(seed),"--rounds","32","--skip-evaluation",
                    "--output",str(trial),"--certification",str(output/"certification/report.json")],
                    cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=1800)
            trained=read(trial/"report.json")
            assert trained["status"]=="PASS" and trained["training_games"]==3072
            assert trained["schedule"]==spec["schedule"] and not trained["evaluations"]
            # Preserve the entire short-budget training prefix, including all
            # intermediate parameters and optimizer state, not just win rates.
            old=read(REFERENCE/f"seed-{seed}/training-report.json.gz")
            for key in ("algorithm","feature_schema","features","hidden_units","initialization",
                        "reward","gamma","advantage","weighting","optimizer"):
                assert trained[key]==old[key],key
            for new,prior in zip(trained["rounds"][:8],old["rounds"]):
                assert new["model_sha256"]==prior["model_sha256"]
                assert new["optimizer_sha256"]==prior["optimizer_sha256"]
            for round_ in trained["rounds"]:
                assert round_["optimization_replay"]
                assert digest(trial/f"checkpoint-{round_['round']}.json")==round_["model_sha256"]
                assert digest(trial/f"optimizer-{round_['round']}.npz")==round_["optimizer_sha256"]
                for cell in round_["cells"]:
                    assert digest(ROOT/cell["rollout"])==cell["rollout_sha256"]
            report["training"][str(seed)]={"status":"PASS","games":trained["training_games"],
                "seconds":trained["training_seconds"],"learner_decisions":trained["training_learner_decisions"],
                "completion_rate":trained["training_completion_rate"],"optimizer_replay":True,
                "short_prefix_matches":True,"report_sha256":digest(trial/"report.json")}
            save()
            print(f"Seed {seed}: training PASS; original 768-duel prefix matches exactly",flush=True)
        report["phase"]="validation";save()
        for seed in SEEDS:
            for c in CHECKPOINTS:
                for opponent in ("random","heuristic"):
                    name=f"{seed}/round-{c}-vs-{opponent}"
                    ev=evaluate(output/"validation"/name,VALIDATION_COUNT,VALIDATION_BASE,
                                "rl",opponent,model_path(output,seed,c))
                    report["validation"][name]=metric(ev);save()
                    print("VALIDATION "+name+" "+str(ev["wins"])+"/"+str(ev["games"]),flush=True)
        seal=seal_models(output)
        report["frozen_models"]=seal;report["phase"]="final_test";save()
        (output/"pretest-manifest.json").write_text(json.dumps(report,indent=2)+"\n")
        report["pretest_manifest_sha256"]=digest(output/"pretest-manifest.json");save()
        # No further training or checkpoint selection is performed after this
        # manifest is written. The test compares fixed round 8 and round 32.
        for seed in SEEDS:
            for c in (0,8,32):
                for opponent in ("random","heuristic"):
                    verify_seal(output,seal)
                    name=f"{seed}/round-{c}-vs-{opponent}"
                    ev=evaluate(output/"test"/name,TEST_COUNT,TEST_BASE,
                                "rl",opponent,model_path(output,seed,c))
                    report["test"][name]=metric(ev);save()
                    print("TEST "+name+" "+str(ev["wins"])+"/"+str(ev["games"]),flush=True)
            name=f"{seed}/heuristic-vs-random"
            ev=evaluate(output/"test"/name,TEST_COUNT,TEST_BASE,"heuristic","random")
            report["test"][name]=metric(ev);save()
        # Uniform starting policies and the frozen benchmark have identical
        # trajectories across runs on the new test matrix.
        for label in ("round-0-vs-random","round-0-vs-heuristic","heuristic-vs-random"):
            first=read(output/"test"/str(SEEDS[0])/label/"report.json")
            for seed in SEEDS[1:]:
                other=read(output/"test"/str(seed)/label/"report.json")
                assert [c["runs"][0]["duels"] for c in first["cells"]]==[c["runs"][0]["duels"] for c in other["cells"]]
        verify_seal(output,seal)
        assert digest(output/"pretest-manifest.json")==report["pretest_manifest_sha256"]
        gate(output/"certification/report.json")
        report["phase"]="test_complete"
        report["summary"]=summarize(report)
        report["status"]="PASS";save()
        print(json.dumps(report["summary"],indent=2),flush=True)
    except Exception as error:
        report.update(status="FAIL",error=str(error));save();raise
if __name__=="__main__": main()
