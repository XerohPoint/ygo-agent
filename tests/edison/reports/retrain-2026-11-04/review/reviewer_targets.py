"""Compare actual MST target prompts on differently identified opposing set cards."""
import sys,os,json,subprocess,sqlite3
from pathlib import Path
import numpy as np
R=Path(__file__).resolve().parent;M=Path('/home/mada/ygo-agent');OUT=R/'reviewer_targets';OUT.mkdir(exist_ok=True);sys.dont_write_bytecode=True
sys.path[:0]=[str(R/'tests/edison'),str(R),str(R/'ygoenv'),str(R/'tools/edison/pool_audit')]
import production_prompt_tests as pp
pp.DB=M/'assets/locale/en/cards.cdb';pp.ROOT=M
import production_deck_label_tests as dl
dl.DB=pp.DB;dl.ROOT=M
import production_deck_order_tests as t
t.DB=pp.DB;t.ROOT=M
codes=OUT/'codes.txt';pp.code_list(codes);db=sqlite3.connect(pp.DB);idcode={i+1:int(l.split()[0]) for i,l in enumerate(codes.read_text().splitlines())};mst=pp.code_of(db,'Mystical Space Typhoon');traps=[[pp.code_of(db,n) for n in names] for names in [('Mirror Force','Dimensional Prison'),('Bottomless Trap Hole','Compulsory Evacuation Device')]];f=t.filler(db,80,{mst,*sum(traps,[])});probe=OUT/'probe.ydk';t.write_list(probe,f[:40]);decks={};plans=[]
for start in (84002000,84002100,84002200,84002300):
 done=subprocess.run([sys.executable,str(R/'reviewer_late_order.py'),'probe',str(codes),str(probe),str(start)],cwd=M/'scripts',capture_output=True,text=True,timeout=30);assert done.returncode==0
 layout=json.loads(done.stdout.split('RESULT',1)[1]);seed=layout['seed'];plans.append(seed)
 for v,ts in zip('ab',traps):
  lists=[list(f[:40]),list(f[40:])];lists[0][layout['protect']['0']['hand'][0]]=mst
  for i,c in zip(layout['protect']['1']['hand'],ts):lists[1][i]=c
  for seat in (0,1):
   name=f'{seed}{v}{seat}';p=OUT/f'{name}.ydk';t.write_list(p,lists[seat]);decks[name]=str(p)
t.load_module(M/'ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so')
from ygoenv.ygopro import init_module
os.chdir(M/'scripts');init_module(str(pp.DB),str(codes),decks);results=[]
for seed in plans:
 envs=[t.make_env(seed,f'{seed}{v}0',f'{seed}{v}1') for v in 'ab'];states=[e.reset() for e in envs];compared=0
 for step in range(100):
  (a,ai),(b,bi)=states;seat=int(ai['to_play'][0]);turn=int(a['global_'][0,4]);assert seat==int(bi['to_play'][0]);n=int(ai['num_options'][0]);offers=a['actions_'][0,:n]
  if seat==0:
   assert all(np.array_equal(a[k],b[k]) for k in t.SEQUENCES+('actions_',)),(seed,turn)
   compared+=1
   targets=[x for x in offers if x[0] and a['cards_'][0,int(x[0])-1,4]==1 and a['cards_'][0,int(x[0])-1,2]==4]
   if int(offers[0,3])==3 and len(targets)==2:
    assert all(x[1]==0 and x[2]==0 for x in targets)
    np.savez(OUT/f'observation-{seed}.npz',**a)
    results.append(dict(seed=seed,turn=turn,decisions_compared=compared,hidden_targets=2));break
  picks=[]
  for obs,info in states:
   off=obs['actions_'][0,:int(info['num_options'][0])];pick=len(off)-1
   wanted=[i for i,x in enumerate(off) if int(x[4])==(8 if seat==0 else 1) and (seat==1 or idcode.get(int(x[1])*256+int(x[2]))==mst)]
   if wanted and (seat==1 or turn>=3):pick=wanted[0]
   picks.append(pick)
  states=[]
  for e,pick in zip(envs,picks):
   obs,_,end,trunc,info=e.step(np.array([pick]));assert not end[0] and not trunc[0];states.append((obs,info))
 else:raise AssertionError('target prompt not reached')
 print(results[-1],flush=True)
(OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n');sys.stdout.flush();os._exit(0)
