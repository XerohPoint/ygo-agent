#!/usr/bin/env python3
"""Three fixed-budget optimizer-seed replications; no model or budget tuning."""
import argparse
import gzip
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
from agent_match import ROOT,digest
from train_context_rl import gate,schedule

SEEDS=(1702,1703,1704)
PILOT=ROOT/"tests/edison/reports/rl-2026-11-02"

def read(path):
    return json.loads(gzip.decompress(path.read_bytes()) if path.suffix==".gz" else path.read_text())

def distribution(values):
    return {"mean":statistics.mean(values),"sample_stddev":statistics.stdev(values),
            "minimum":min(values),"maximum":max(values)}

def aggregate(reports,pilot):
    if len(reports)!=3 or len({r["optimizer_seed"] for r in reports})!=3:
        raise ValueError("Require all three distinct replications; never average only successful runs")
    if any(r["status"]!="PASS" for r in reports):
        raise ValueError("A replication failed")
    result={"replications":3,"optimizer_seeds":[r["optimizer_seed"] for r in reports],
            "opponents":{}}
    for opponent in ("random","heuristic"):
        name="final-vs-"+opponent
        initial="initial-vs-"+opponent
        rates=[r["evaluations"][name]["win_rate"] for r in reports]
        gains=[r["evaluations"][name]["win_rate"]-r["evaluations"][initial]["win_rate"] for r in reports]
        result["opponents"][opponent]={
            "games_per_run":192,"win_rate":distribution(rates),
            "gain_over_initial":distribution(gains),"improved_runs":sum(g>0 for g in gains),
            "pilot_win_rate":pilot["evaluations"][name]["win_rate"],
            "including_pilot_win_rate":distribution([pilot["evaluations"][name]["win_rate"]]+rates)}
    result["training_games"]=sum(r["training_games"] for r in reports)
    result["learner_decisions"]=sum(r["training_learner_decisions"] for r in reports)
    result["training_seconds"]=distribution([r["training_seconds"] for r in reports])
    result["evaluation_executions"]=sum(e["executions"] for r in reports for e in r["evaluations"].values())
    result["total_executions"]=result["training_games"]+result["evaluation_executions"]
    result["completion_rate"]=min([r["training_completion_rate"] for r in reports]+
        [e["completion_rate"] for r in reports for e in r["evaluations"].values()])
    result["evaluation_replay"]=all(e["repeatability"] for r in reports for e in r["evaluations"].values())
    result["optimizer_replay"]=all(round_["optimization_replay"] for r in reports for round_ in r["rounds"])
    return result

def validate_trial(report,output,pilot):
    for key in ("algorithm","feature_schema","features","hidden_units","initialization","reward",
                "gamma","advantage","weighting","optimizer","schedule","held_out_seed",
                "held_out_seeds_per_cell","checkpoint_selection","training_games"):
        assert report[key]==pilot[key], ("Protocol drift",key)
    assert report["status"]=="PASS" and report["schedule"]==schedule()
    assert report["gates"]["production_binary_sha256"]==pilot["gates"]["production_binary_sha256"]
    assert report["gates"]["assets_sha256"]==pilot["gates"]["assets_sha256"]
    initial=read(output/"initial.json")
    assert initial["metadata"]["seed"]==report["optimizer_seed"]
    for i,round_ in enumerate(report["rounds"],1):
        model=read(output/f"checkpoint-{i}.json")
        assert model["metadata"]["optimizer_seed"]==report["optimizer_seed"]
        assert digest(output/f"checkpoint-{i}.json")==round_["model_sha256"]
        assert digest(output/f"optimizer-{i}.npz")==round_["optimizer_sha256"]
        for cell in round_["cells"]:
            assert digest(ROOT/cell["rollout"])==cell["rollout_sha256"]
    # The uniform initial actor and frozen benchmark must reproduce the pilot's
    # actual seeded decisions/results, not merely similar aggregate win rates.
    for name in ("initial-vs-random","initial-vs-heuristic","heuristic-vs-random"):
        current=read(output/name/"report.json");reference=read(PILOT/(name+".json.gz"))
        assert len(current["cells"])==len(reference["cells"])
        for new,old in zip(current["cells"],reference["cells"]):
            assert (new["deck0"],new["deck1"],new["seat"])==(old["deck0"],old["deck1"],old["seat"])
            assert new["runs"][0]["duels"]==old["runs"][0]["duels"], ("Benchmark drift",name)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,default=ROOT/"build/edison-rl-replication")
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    certification=output/"certification/report.json"
    gates=gate(certification)
    pilot=read(PILOT/"training-report.json.gz")
    assert pilot["optimizer_seed"] not in SEEDS
    # Only orchestration is changed for replication; the learner and benchmarks
    # must still match the checkpointed pilot exactly.
    for source in ("ygoai/edison_rl.py","ygoai/edison_policy.py","tests/edison/agent_match.py",
                   "tests/edison/evaluate_agents.py"):
        assert digest(ROOT/source)==pilot["gates"]["source_sha256"][source],source
    report={"status":"RUNNING","seeds":list(SEEDS),"pilot_optimizer_seed":pilot["optimizer_seed"],
            "protocol":"sequential runs; vary initialization/shuffle seed only; fixed duel/sampling/evaluation seeds",
            "gates":gates,"trials":[]}
    path=output/"report.json"
    def save(): path.write_text(json.dumps(report,indent=2)+"\n")
    save()
    try:
        reports=[]
        env=dict(os.environ,OPENBLAS_NUM_THREADS="1",OMP_NUM_THREADS="1")
        for seed in SEEDS:
            trial=output/f"seed-{seed}"
            trial.mkdir(parents=True,exist_ok=True)
            print(f"Starting optimizer seed {seed}: 768 training games; unchanged held-out evaluation.",flush=True)
            with (trial/"console.txt").open("w") as log:
                process=subprocess.run([sys.executable,str(Path(__file__).with_name("train_context_rl.py")),
                    "--optimizer-seed",str(seed),"--output",str(trial),"--certification",str(certification)],
                    cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=1800)
            if process.returncode:
                raise RuntimeError(f"Seed {seed} failed; see {trial/'console.txt'}")
            result=read(trial/"report.json")
            validate_trial(result,trial,pilot)
            reports.append(result)
            report["trials"].append({"optimizer_seed":seed,"report_sha256":digest(trial/"report.json"),
                "report":str((trial/"report.json").relative_to(ROOT)),
                "final_model_sha256":result["final_model_sha256"],
                "training_seconds":result["training_seconds"],"evaluations":result["evaluations"]})
            save()
            print(f"Seed {seed} PASS: random {result['evaluations']['final-vs-random']['wins']}/192; "
                  f"heuristic {result['evaluations']['final-vs-heuristic']['wins']}/192; controls identical.",flush=True)
        report["summary"]=aggregate(reports,pilot)
        report["status"]="PASS"
        print(json.dumps(report["summary"],indent=2),flush=True)
    except Exception as error:
        report.update(status="FAIL",error=str(error));raise
    finally: save()
if __name__=="__main__": main()
