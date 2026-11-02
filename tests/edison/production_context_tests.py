#!/usr/bin/env python3
"""Test 082: production context, hidden selections and a context-aware consumer."""
import argparse
import gzip
import json
import tempfile
from pathlib import Path
import numpy as np
from agent_match import ROOT, run_worker
from agent_policy_tests import card, action
from ygoai.edison_policy import ContextHeuristic

def policy_choices():
    glob=np.zeros(23,dtype=np.uint8);glob[7]=1
    cards=np.array([card(1,2500),card(2,1800,enemy=1),card(3,500,enemy=1)])
    actions=np.array([action(2,2,msg=3),action(3,3,msg=3)])
    context=np.zeros(12,dtype=np.int32);context[0]=1;context[11]=1
    selection=np.zeros((3,3),dtype=np.uint8);selection[1:,0]=1
    agent=ContextHeuristic({})
    # Same offered cards: choose the easier attack target, but the stronger
    # destruction target. No remembered attacker/action is required.
    context[1]=549;context[3]=1
    assert agent.choose(cards,glob,actions,context,selection)[0]==1
    context[1]=502;context[2]=2;context[3]=0
    assert agent.choose(cards,glob,actions,context,selection)[0]==0
    # Same hand candidates: discard the smaller body as cost, search the larger.
    cards=np.array([card(1,2400,loc=2),card(2,500,loc=2)])
    actions=np.array([action(1,1,msg=3),action(2,2,msg=3)])
    selection=np.zeros((2,3),dtype=np.uint8);selection[:,0]=1
    context[1]=501;context[2]=1
    assert agent.choose(cards,glob,actions,context,selection)[0]==1
    context[1]=506;context[2]=3
    assert agent.choose(cards,glob,actions,context,selection)[0]==0
    selection[0,1]=1
    assert agent.choose(cards,glob,actions,context,selection)[0]==1

def seeded_context():
    with tempfile.TemporaryDirectory(prefix="edison-context-") as tmp:
        path=Path(tmp)
        one=run_worker(20110424,16,1,"blackwing","frog_monarch",path/"one",
                       policy0="random",policy1="random")
        many=run_worker(20110424,16,8,"blackwing","frog_monarch",path/"many",
                        policy0="random",policy1="random")
        assert one["duels"]==many["duels"]
        assert one["context_counts"]==many["context_counts"]
        # These are required coverage witnesses from real prompts, not merely
        # assertions conditional on the rare situation happening.
        for key in ("549:0:3","501:1:3","502:2:3","506:3:3","512:2:12"):
            assert one["context_counts"].get(key,0)>0, ("Missing witness",key)
        assert one["selected_prompts"]>0 and one["mandatory_prompts"]>0
        assert one["hidden_selected_prompts"]>0
        previous={}
        continuations=0
        for line in (path/"one/trace-1.jsonl").read_text().splitlines():
            row=json.loads(line)
            if "terminal" in row: continue
            context=row["decision"]
            if context[1]==549:
                assert context[3]>0, "Attack-target selection lost its visible attacker"
            old=previous.get(row["env"])
            if old and context[11] and int(row["offered"][0][3]) in (3,4,12):
                before=old["decision"]
                chosen=old["offered"][old["action"]]
                if (chosen[0] and context[1:3]==before[1:3] and context[4:6]==before[4:6]
                        and context[6]==before[6]+1):
                    assert chosen[0] in row["selected_rows"], "Previous choice missing from selection"
                    assert set(old["selected_rows"])<=set(row["selected_rows"]), "Earlier choices forgotten"
                    continuations+=1
            previous[row["env"]]=row
        assert continuations>0, "No multi-step selection-retention witness"
        print("Observed: attack, cost, removal, search, mandatory Synchro material, and hidden selected cards; replay identical.")

def context_consumer():
    with tempfile.TemporaryDirectory(prefix="edison-context-policy-") as tmp:
        path=Path(tmp)
        one=run_worker(20110424,8,1,"machina_gadget","blackwing",path/"one",
                       policy0="context",policy1="heuristic")
        many=run_worker(20110424,8,8,"machina_gadget","blackwing",path/"many",
                        policy0="context",policy1="heuristic")
        assert one["duels"]==many["duels"]
        print("Observed: context policy completes eight legal seeded games against the frozen heuristic.")

def legacy_compatibility():
    baseline=ROOT/"tests/edison/reports/agents-2026-11-02"
    with tempfile.TemporaryDirectory(prefix="edison-context-compat-") as tmp:
        for policy,report in (("heuristic","heuristic_vs_random.json.gz"),
                              ("learned","learned_vs_random.json.gz")):
            saved=json.loads(gzip.decompress((baseline/report).read_bytes()))
            cell=next(c for c in saved["cells"] if c["deck0"]=="blackwing" and
                      c["deck1"]=="frog_monarch" and c["seat"]==0)
            current=run_worker(20110424,4,1,"blackwing","frog_monarch",Path(tmp)/policy,
                policy0=policy,policy1="random",
                model=baseline/"linear_policy.json" if policy=="learned" else None)
            for old,new in zip(cell["runs"][0]["duels"][:4],current["duels"]):
                for key in ("seed","steps","winner","win_reason","interactions"):
                    assert old[key]==new[key], ("Frozen policy behavior changed",policy,key)
        print("Observed: frozen heuristic and saved 64-feature model retain seeded step counts, interaction counts, and outcomes.")

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--fail-fast",action="store_true")
    args=p.parse_args()
    passed=failed=0
    for label,fn in (("A - CONTEXT-SENSITIVE CHOICES",policy_choices),
                     ("B - PRODUCTION SELECTION CONTEXT",seeded_context),
                     ("C - CONTEXT CONSUMER REPLAY",context_consumer),
                     ("D - FROZEN POLICY COMPATIBILITY",legacy_compatibility)):
        print("EDISON RULE TEST 082"+label)
        try:
            fn();passed+=1;print("RESULT: PASS")
        except Exception as error:
            failed+=1;print("RESULT: FAIL - "+str(error))
            if args.fail_fast: break
    print(f"SUMMARY: {passed} PASS, {failed} FAIL")
    return bool(failed)
if __name__=="__main__":
    raise SystemExit(main())
