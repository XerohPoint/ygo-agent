#!/usr/bin/env python3
"""Test 078: production observation and terminal contract (isolated processes)."""
import argparse
import subprocess
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]

def probe(case):
    import numpy as np
    import ygoenv
    from ygoenv.ygopro import init_module
    init_module(str(ROOT/"assets/locale/en/cards.cdb"),
                str(ROOT/"assets/edison/smoke_codes.txt"),
                {p.stem: str(p) for p in (ROOT/"assets/edison/decks").glob("*.ydk")})
    env = ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=1,
        num_threads=1, seed=20100424, deck1="blackwing", deck2="frog_monarch",
        play_mode="self", player=-1, async_reset=False, greedy_reward=False,
        oppo_info=False, max_options=1 if case=="overflow" else 256,
        max_steps=1 if case=="limit" else 10000, max_episode_steps=1 if case=="episode_limit" else 10000, timeout=10)
    obs, info = env.reset()
    if case=="overflow":
        assert info["action_overflow"][0] == 1
        assert info["legal_options"][0] > info["num_options"][0] == 1
        return
    if case in ("limit","episode_limit"):
        obs, reward, terminated, truncated, info = env.step(np.array([0]))
        assert truncated[0] and not terminated[0], (terminated, truncated)
        assert reward[0] == 0 and info["winner"][0] == -1
        obs,info=env.reset()
        assert info["winner"][0]==-1 and info["num_options"][0]>0
        return
    cards = obs["cards_"][0]
    hand = cards[(cards[:,2]==2) & (cards[:,4]==0)]
    assert len(hand) >= 5 and np.all(hand[:,0].astype(int)*256+hand[:,1] > 0), "Visible hand identity missing"
    hidden = cards[(cards[:,2]==1) | ((cards[:,4]==1) & np.isin(cards[:,2], [2,7]))]
    assert not np.any(hidden[:,:2]) and not np.any(hidden[:,7:]), "Private card data leaked"
    n = int(info["num_options"][0])
    for a in obs["actions_"][0,:n]:
        if a[0]:
            c = cards[int(a[0])-1]
            assert tuple(a[1:3]) == tuple(c[:2]), "Action identity disagrees with referenced card"
    assert info["winner"][0] == -1 and info["action_overflow"][0] == 0

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--probe", choices=["visible", "overflow", "limit", "episode_limit"])
    parser.add_argument("--fail-fast", action="store_true")
    args=parser.parse_args()
    if args.probe:
        probe(args.probe)
        return 0
    failed=passed=0
    for label,case in zip("ABCD", ["visible","overflow","limit","episode_limit"]):
        proc=subprocess.run([sys.executable,__file__,"--probe",case],
            cwd=ROOT/"scripts",capture_output=True,text=True,timeout=20)
        print(f"EDISON RULE TEST 078{label} - {case.upper()}")
        if proc.returncode:
            print("RESULT: FAIL\n"+proc.stderr)
            failed+=1
            if args.fail_fast: break
        else:
            print("RESULT: PASS")
            passed+=1
    print(f"SUMMARY: {passed} PASS, {failed} FAIL")
    return bool(failed)
if __name__=="__main__":
    raise SystemExit(main())
