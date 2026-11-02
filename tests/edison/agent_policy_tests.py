#!/usr/bin/env python3
"""Test 079: heuristic decisions and third-archetype production regression."""
import argparse
import tempfile
from pathlib import Path
import numpy as np
from agent_match import ROOT, run_worker
from ygoai.edison_policy import Heuristic

def card(cid,atk,defense=0,loc=3,enemy=0,pos=1):
    c=np.zeros(41,dtype=np.uint8)
    c[0:2]=[cid//256,cid%256]
    c[2],c[4],c[5]=loc,enemy,pos
    c[12:16]=[atk//256,atk%256,defense//256,defense%256]
    return c
def action(index=0,cid=0,msg=1,act=0,phase=0):
    a=np.zeros(12,dtype=np.uint8)
    a[:5]=[index,cid//256,cid%256,msg,act]
    a[7]=phase
    return a
def decisions():
    g=np.zeros(23,dtype=np.uint8);g[7]=1
    h=Heuristic({1:{"name":"Heavy Storm"}})
    # A normal summon with better attack wins the choice.
    cards=np.array([card(2,1800,loc=2),card(3,500,loc=2)])
    acts=np.array([action(1,2,act=4),action(2,3,act=4),action(phase=3)])
    assert h.choose(cards,g,acts)[0]==0
    # Do not throw a 1000 ATK monster into a known 2400 ATK monster.
    cards=np.array([card(2,1000),card(3,2400,enemy=1)])
    acts=np.array([action(1,2,msg=8,act=6),action(msg=8,phase=2)])
    assert Heuristic({}).choose(cards,g,acts)[0]==1
    cards[1]=card(3,500,enemy=1)
    assert Heuristic({}).choose(cards,g,acts)[0]==0
    # Heavy Storm would destroy only our own back row.
    cards=np.array([card(1,0,loc=2),card(4,0,loc=4)])
    acts=np.array([action(1,1,act=8),action(phase=3)])
    assert Heuristic({1:{"name":"Heavy Storm"}}).choose(cards,g,acts)[0]==1
    # Remove an opposing threat rather than our own stronger monster.
    cards=np.array([card(2,3000),card(3,1800,enemy=1)])
    acts=np.array([action(1,2,msg=3),action(2,3,msg=3)])
    assert Heuristic({}).choose(cards,g,acts)[0]==1
    # Tribute the weaker body.
    cards=np.array([card(2,3000),card(3,500)])
    acts=np.array([action(1,2,msg=4),action(2,3,msg=4)])
    assert Heuristic({}).choose(cards,g,acts)[0]==1

def production():
    with tempfile.TemporaryDirectory(prefix="edison-agent-") as tmp:
        path=Path(tmp)
        one=run_worker(20110424,4,1,"machina_gadget","frog_monarch",path/"one",
                       policy0="heuristic",policy1="random")
        many=run_worker(20110424,4,4,"machina_gadget","frog_monarch",path/"many",
                        policy0="heuristic",policy1="random")
        assert one["duels"]==many["duels"]
        # Gearframe/Gadget summons exercise real search selection, not only
        # successful initialization of the third deck.
        import sqlite3
        with sqlite3.connect(ROOT/"assets/locale/en/cards.cdb") as db:
            code=db.execute("select id from texts where name='Machina Gearframe'").fetchone()[0]
        assert any(d["interactions"].get(f"{code}:6:8",0)>0 for d in one["duels"]), "Gearframe search not reached"

def relative_output():
    # Worker changes cwd to scripts; caller-relative outputs must still land at
    # the caller's location, never read stale reports under a different cwd.
    with tempfile.TemporaryDirectory(prefix="agent-relative-",dir=ROOT/"build") as tmp:
        path=Path(tmp)
        one=run_worker(20110424,1,1,"blackwing","machina_gadget",path.relative_to(Path.cwd()))
        assert (path/"worker-1.json").is_file() and len(one["duels"])==1

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--fail-fast",action="store_true")
    args=parser.parse_args()
    passed=failed=0
    for label,fn in (("A - HEURISTIC CHOICES",decisions),("B - MACHINA PRODUCTION",production),("C - RELATIVE WORKER OUTPUT",relative_output)):
        print("EDISON RULE TEST 079"+label)
        try:
            fn();passed+=1;print("RESULT: PASS")
        except Exception as error:
            failed+=1;print("RESULT: FAIL - "+str(error))
            if args.fail_fast: break
    print(f"SUMMARY: {passed} PASS, {failed} FAIL")
    return bool(failed)
if __name__=="__main__":
    raise SystemExit(main())
