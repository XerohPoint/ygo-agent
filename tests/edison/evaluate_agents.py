#!/usr/bin/env python3
"""Reproducible seat/deck-balanced production policy evaluation."""
import argparse
import itertools
import json
import math
from pathlib import Path
from agent_match import ROOT, digest, run_worker

DECKS=("blackwing","frog_monarch","machina_gadget")

def wilson(wins,total):
    z=1.96
    p=wins/total
    d=1+z*z/total
    center=(p+z*z/(2*total))/d
    radius=z*math.sqrt(p*(1-p)/total+z*z/(4*total*total))/d
    return [center-radius,center+radius]

def evaluate(output,count=32,seed=20110424,challenger="heuristic",opponent="random",model=None):
    output=output.resolve()
    output.mkdir(parents=True,exist_ok=True)
    lock=json.loads((ROOT/"tests/edison/agent_assets.lock.json").read_text())
    for path,expected in lock.items():
        assert digest(ROOT/path)==expected, ("Asset mismatch",path)
    report={"status":"RUNNING","seed":seed,"seeds_per_cell":count,
            "challenger":challenger,"opponent":opponent,"cells":[],
            "assets_sha256":lock,
            "production_binary_sha256":digest(next((ROOT/"ygoenv/ygoenv/ygopro").glob("ygopro_ygoenv*.so"))),
            "source_sha256":{str(p.relative_to(ROOT)):digest(p) for p in [
                ROOT/"ygoai/edison_policy.py",Path(__file__),Path(__file__).with_name("agent_match.py"),
                ROOT/"ygoenv/ygoenv/ygopro/ygopro.h",
                ROOT/"ygoenv/ygoenv/ygopro/observation_visibility.h"]}}
    if model: report["model_sha256"]=digest(model)
    path=output/"report.json"
    def save(): path.write_text(json.dumps(report,indent=2)+"\n")
    save()
    try:
        pairs=itertools.product(DECKS,repeat=2) if challenger==opponent else itertools.permutations(DECKS,2)
        for d0,d1 in pairs:
            for seat in ((0,) if challenger==opponent else (0,1)):
                p0,p1=(challenger,opponent) if seat==0 else (opponent,challenger)
                cell=output/f"{d0}-{d1}-seat{seat}"
                one=run_worker(seed,count,1,d0,d1,cell,1,p0,p1,model)
                many=run_worker(seed,count,min(count,8),d0,d1,cell,2,p0,p1,model)
                assert one["duels"]==many["duels"], "Fresh-process/thread-count replay differs"
                wins=sum(d["winner"]==seat for d in one["duels"])
                report["cells"].append({"deck0":d0,"deck1":d1,"seat":seat,"wins":wins,
                    "games":count,"runs":[one,many]})
                save()
                print(f"{d0}/{d1} seat {seat}: {wins}/{count}; "
                      f"{many['decisions_per_second']:.0f} decisions/s; replay PASS",flush=True)
        games=sum(c["games"] for c in report["cells"])
        wins=sum(c["wins"] for c in report["cells"])
        runs=[run for c in report["cells"] for run in c["runs"]]
        report.update(status="PASS",games=games,executions=games*2,wins=wins,
            win_rate=wins/games,win_rate_wilson_95=wilson(wins,games),
            completion_rate=1.0,repeatability=True,
            decisions_per_second=sum(run["decisions"] for run in runs)/sum(run["elapsed_seconds"] for run in runs),
            max_options_seen=max(run["max_options_seen"] for run in runs),
            longest_step_seconds=max(run["longest_step_seconds"] for run in runs))
        # Paired games share seeds, so the Wilson interval is descriptive rather
        # than a claim of independent tournament samples.
    except Exception as error:
        report.update(status="FAIL",error=str(error))
        raise
    finally: save()
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output",type=Path,default=ROOT/"build/edison-agents/evaluation")
    p.add_argument("--count",type=int,default=32)
    p.add_argument("--seed",type=int,default=20110424)
    p.add_argument("--challenger",default="heuristic",choices=["random","heuristic","learned"])
    p.add_argument("--opponent",default="random",choices=["random","heuristic"])
    p.add_argument("--model",type=Path)
    a=p.parse_args()
    report=evaluate(a.output,a.count,a.seed,a.challenger,a.opponent,a.model)
    print(f"PASS: {report['wins']}/{report['games']} wins; "
          f"{report['executions']} complete executions; "
          f"{report['decisions_per_second']:.0f} decisions/s")
if __name__=="__main__":
    main()
