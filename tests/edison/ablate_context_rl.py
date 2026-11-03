#!/usr/bin/env python3
"""Paired decision-context ablation with fixed budget and fresh final tests."""
import argparse
import gzip
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import numpy as np
from agent_match import ROOT,digest
from evaluate_agents import evaluate
from replicate_context_rl import read,distribution,SEEDS
from budget_context_rl import metric,verify_seal
from train_context_rl import gate,schedule
from ygoai.edison_rl import RLModel,CONTEXT_COLUMNS

REFERENCE=ROOT/"tests/edison/reports/rl-budget-2026-11-02"
CHECKPOINTS=(8,16,24,32)
MODES=("full","context_masked_v1")
VALIDATION_BASE=40000000
TEST_BASE=60000000
VALIDATION_COUNT=8
TEST_COUNT=16

def protocol():
    rows=schedule(32)
    training={r["seed"]+i for r in rows for i in range(r["games"])}
    validation=set(range(VALIDATION_BASE,VALIDATION_BASE+VALIDATION_COUNT))
    test=set(range(TEST_BASE,TEST_BASE+TEST_COUNT))
    previous_tests=set(range(22000000,22000016))|set(range(50000000,50000016))
    assert len(training)==3072 and not training & (validation|test|previous_tests)
    assert not test & (validation|previous_tests)
    assert len(CONTEXT_COLUMNS)==52
    return {"optimizer_seeds":list(SEEDS),"training_games_per_run":3072,
        "rounds":32,"mask_columns":list(CONTEXT_COLUMNS),"modes":list(MODES),
        "mask_definition":"Zero only decision/selection-derived inputs; retain 76 independent inputs",
        "control":"reuse immutable full-context training checkpoints; rerun validation and fresh test",
        "validation_checkpoints":list(CHECKPOINTS),"validation_seed":VALIDATION_BASE,
        "validation_seeds_per_cell":VALIDATION_COUNT,"test_seed":TEST_BASE,
        "test_seeds_per_cell":TEST_COUNT,"checkpoint_selection":"fixed round 32 in both arms",
        "execution_order":"all masked training, paired validation, freeze manifest, paired final tests",
        "schedule":rows}

def model_path(root,mode,seed,c):
    return root/mode/f"seed-{seed}"/("initial.json" if c==0 else f"checkpoint-{c}.json")

def seal_models(root):
    return {str(model_path(root,m,s,c).relative_to(root)):digest(model_path(root,m,s,c))
            for m in MODES for s in SEEDS for c in (0,)+CHECKPOINTS}

def summarize(report):
    assert report["phase"]=="test_complete"
    assert len(report["training"])==3 and len(report["validation"])==48 and len(report["test"])==13
    result={"opponents":{},"validation_curve":{}}
    for op in ("random","heuristic"):
        arms={m:[report["test"][f"{s}/{m}-vs-{op}"] for s in SEEDS] for m in MODES}
        result["opponents"][op]={m:{
            "win_rate":distribution([e["win_rate"] for e in rows]),
            "serial_decisions_per_second":distribution([e["serial_decisions_per_second"] for e in rows]),
            "parallel_decisions_per_second":distribution([e["parallel_decisions_per_second"] for e in rows])
            } for m,rows in arms.items()}
        gains=[a["win_rate"]-b["win_rate"] for a,b in zip(arms["full"],arms["context_masked_v1"])]
        result["opponents"][op].update(full_minus_masked=distribution(gains),
            full_better_runs=sum(v>0 for v in gains),tied_runs=sum(v==0 for v in gains))
        result["validation_curve"][op]={m:{str(c*96):distribution([
            report["validation"][f"{s}/{m}/round-{c}-vs-{op}"]["win_rate"] for s in SEEDS])
            for c in CHECKPOINTS} for m in MODES}
    ev=list(report["validation"].values())+list(report["test"].values())
    assert all(e["status"]=="PASS" and e["completion_rate"]==1 and e["repeatability"] for e in ev)
    assert all(t["status"]=="PASS" and t["completion_rate"]==1 and t["optimizer_replay"] for t in report["training"].values())
    result.update(new_training_games=9216,reused_full_training_games=9216,
        masked_training_seconds=distribution([t["seconds"] for t in report["training"].values()]),
        evaluation_executions=sum(e["executions"] for e in ev),completion_rate=1.0,
        evaluation_replay=True,optimizer_replay=True,full_validation_matches_archive=True)
    result["new_total_executions"]=result["new_training_games"]+result["evaluation_executions"]
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,default=ROOT/"build/edison-rl-ablation")
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    path=output/"report.json"
    if path.exists(): raise RuntimeError("Use a fresh output directory; study evidence is not overwritten")
    gates=gate(output/"certification/report.json");spec=protocol()
    snapshot=read(REFERENCE/"snapshot.json")
    for relative,sha in snapshot["artifacts_sha256"].items():
        assert digest(REFERENCE/relative)==sha,relative
    for source in ("ygoai/edison_policy.py","tests/edison/agent_match.py","tests/edison/evaluate_agents.py"):
        assert digest(ROOT/source)==snapshot["source_sha256"][source],source
    assert gates["production_binary_sha256"]==snapshot["production_binary_sha256"]
    assert gates["assets_sha256"]==snapshot["assets_sha256"]
    report={"status":"RUNNING","phase":"training","protocol":spec,"gates":gates,
        "baseline_snapshot_sha256":digest(REFERENCE/"snapshot.json"),"training":{},
        "full_training_reference":{},"validation":{},"test":{}}
    def save(): path.write_text(json.dumps(report,indent=2)+"\n")
    save()
    try:
        env=dict(os.environ,OPENBLAS_NUM_THREADS="1",OMP_NUM_THREADS="1")
        for seed in SEEDS:
            old=read(REFERENCE/f"seed-{seed}/training-report.json.gz")
            full=output/"full"/f"seed-{seed}";full.mkdir(parents=True)
            for c in (0,)+CHECKPOINTS:
                target=model_path(output,"full",seed,c)
                if c==0: shutil.copyfile(REFERENCE/f"seed-{seed}/initial-policy.json",target)
                elif c==32: shutil.copyfile(REFERENCE/f"seed-{seed}/final-policy.json",target)
                else: target.write_bytes(gzip.decompress((REFERENCE/f"seed-{seed}/checkpoint-{c}.json.gz").read_bytes()))
                assert RLModel.load(target).input_mode=="full"
                if c: assert digest(target)==old["rounds"][c-1]["model_sha256"]
            report["full_training_reference"][str(seed)]={
                "report_sha256":digest(REFERENCE/f"seed-{seed}/training-report.json.gz"),
                "games":old["training_games"],"seconds":old["training_seconds"],
                "final_model_sha256":old["final_model_sha256"]}
            trial=output/"context_masked_v1"/f"seed-{seed}";trial.mkdir(parents=True)
            print(f"Training masked seed {seed}: 3072 duels; unchanged paired initialization",flush=True)
            with (trial/"console.txt").open("w") as log:
                subprocess.run([sys.executable,str(Path(__file__).with_name("train_context_rl.py")),
                    "--optimizer-seed",str(seed),"--rounds","32","--skip-evaluation",
                    "--input-mode","context_masked_v1","--output",str(trial),
                    "--certification",str(output/"certification/report.json")],
                    cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=1800)
            trained=read(trial/"report.json")
            assert trained["status"]=="PASS" and trained["training_games"]==3072
            assert trained["schedule"]==spec["schedule"] and not trained["evaluations"]
            assert trained["input_mode"]=="context_masked_v1"
            for key in ("algorithm","feature_schema","features","hidden_units","initialization",
                        "reward","gamma","advantage","weighting","optimizer","schedule","optimizer_seed"):
                assert trained[key]==old[key],key
            initial=RLModel.load(trial/"initial.json");control=RLModel.load(full/"initial.json")
            assert all(np.array_equal(initial.p[k],control.p[k]) for k in initial.p)
            for row in trained["rounds"]:
                c=row["round"];model=RLModel.load(trial/f"checkpoint-{c}.json")
                assert model.input_mode=="context_masked_v1" and row["optimization_replay"]
                assert digest(trial/f"checkpoint-{c}.json")==row["model_sha256"]
                assert digest(trial/f"optimizer-{c}.npz")==row["optimizer_sha256"]
                assert np.array_equal(model.p["w"][list(CONTEXT_COLUMNS)],initial.p["w"][list(CONTEXT_COLUMNS)])
                assert not np.any(model.p["v"][list(CONTEXT_COLUMNS)])
                for cell in row["cells"]:
                    assert digest(ROOT/cell["rollout"])==cell["rollout_sha256"]
                    with np.load(ROOT/cell["rollout"]) as rollout:
                        assert not np.any(rollout["features"][:,list(CONTEXT_COLUMNS)])
            report["training"][str(seed)]={"status":"PASS","games":trained["training_games"],
                "seconds":trained["training_seconds"],"learner_decisions":trained["training_learner_decisions"],
                "completion_rate":1.0,"optimizer_replay":True,"identical_initial_parameters":True,
                "masked_rollouts_verified":True,"report_sha256":digest(trial/"report.json")}
            save();print(f"Masked seed {seed}: training PASS, mask and optimization replays verified",flush=True)
        report["phase"]="validation";save()
        for seed in SEEDS:
            for c in CHECKPOINTS:
                for op in ("random","heuristic"):
                    for mode in MODES:
                        name=f"{seed}/{mode}/round-{c}-vs-{op}"
                        ev=evaluate(output/"validation"/name,VALIDATION_COUNT,VALIDATION_BASE,
                                    "rl",op,model_path(output,mode,seed,c))
                        if mode=="full":
                            old=read(REFERENCE/"validation"/str(seed)/f"round-{c}-vs-{op}.json.gz")
                            assert ev["model_sha256"]==old["model_sha256"]
                            assert [v["runs"][0]["duels"] for v in ev["cells"]]==[v["runs"][0]["duels"] for v in old["cells"]]
                        report["validation"][name]=metric(ev);save()
                        print(f"VALIDATION {name}: {ev['wins']}/{ev['games']}",flush=True)
        seal=seal_models(output);report["frozen_models"]=seal;report["phase"]="final_test";save()
        (output/"pretest-manifest.json").write_text(json.dumps(report,indent=2)+"\n")
        report["pretest_manifest_sha256"]=digest(output/"pretest-manifest.json");save()
        for seed in SEEDS:
            for op in ("random","heuristic"):
                for mode in MODES:
                    verify_seal(output,seal)
                    name=f"{seed}/{mode}-vs-{op}"
                    ev=evaluate(output/"test"/name,TEST_COUNT,TEST_BASE,"rl",op,model_path(output,mode,seed,32))
                    report["test"][name]=metric(ev);save()
                    print(f"TEST {name}: {ev['wins']}/{ev['games']}",flush=True)
        ev=evaluate(output/"test/heuristic-vs-random",TEST_COUNT,TEST_BASE,"heuristic","random")
        report["test"]["heuristic-vs-random"]=metric(ev)
        verify_seal(output,seal)
        assert digest(output/"pretest-manifest.json")==report["pretest_manifest_sha256"]
        gate(output/"certification/report.json")
        report["phase"]="test_complete";report["summary"]=summarize(report)
        report["status"]="PASS";save();print(json.dumps(report["summary"],indent=2),flush=True)
    except Exception as error:
        report.update(status="FAIL",error=str(error));save();raise
if __name__=="__main__": main()
