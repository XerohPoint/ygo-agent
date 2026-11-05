"""Metamorphic private hand, face-down card, and Deck checks through turn eight."""
import os,sys,json,subprocess,sqlite3,importlib.util,hashlib
from pathlib import Path
import numpy as np
R=Path(__file__).resolve().parent;M=Path('/home/mada/ygo-agent');OUT=R/'reviewer_hidden';OUT.mkdir(exist_ok=True);sys.dont_write_bytecode=True
sys.path[:0]=[str(R/'tests/edison'),str(R),str(R/'ygoenv'),str(R/'tools/edison/pool_audit')]
import production_prompt_tests as pp
pp.DB=M/'assets/locale/en/cards.cdb';pp.ROOT=M
import production_deck_label_tests as dl
dl.DB=pp.DB;dl.ROOT=M
import production_deck_order_tests as t
t.DB=pp.DB;t.ROOT=M
codes=OUT/'codes.txt';pp.code_list(codes);db=sqlite3.connect(pp.DB);filler=t.filler(db,80,set());probe=OUT/'probe.ydk';t.write_list(probe,filler[:40]);decks={};plans=[]
for start in (84001000,84001100,84001200,84001300):
 done=subprocess.run([sys.executable,str(R/'reviewer_late_order.py'),'probe',str(codes),str(probe),str(start)],cwd=M/'scripts',capture_output=True,text=True,timeout=30);assert done.returncode==0,done.stderr
 layout=json.loads(done.stdout.split('RESULT',1)[1]);seed=layout['seed'];prot=layout['protect']
 for viewer in (0,1):
  for mode in ('own-deck','opponent-deck','opponent-hand-and-set'):
   a=[filler[:40],filler[40:]];b=[list(x) for x in a];who=viewer if mode=='own-deck' else 1-viewer
   deep=[i for i in range(40) if i not in prot[str(who)]['hand']+prot[str(who)]['top']]
   if mode=='opponent-hand-and-set':
    change=prot[str(who)]['hand']+deep
   else:change=deep
   vals=[b[who][i] for i in change]
   for i,val in zip(change,reversed(vals)):b[who][i]=val
   tag=f'{seed}-{viewer}-{mode}'
   for variant,lists in (('a',a),('b',b)):
    for seat in (0,1):
     name=f'{tag}-{variant}-{seat}';path=OUT/f'{name}.ydk';t.write_list(path,lists[seat]);decks[name]=str(path)
   plans.append((tag,seed,viewer,mode))
t.load_module(M/'ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so')
from ygoenv.ygopro import init_module
os.chdir(M/'scripts');init_module(str(pp.DB),str(codes),decks)
keys=('cards_','global_','actions_','h_actions_','decision_','selection_');results=[]
for tag,seed,viewer,mode in plans:
 envs=[t.make_env(seed,f'{tag}-{v}-0',f'{tag}-{v}-1',oppo_info='--privileged' in sys.argv) for v in 'ab'];states=[e.reset() for e in envs];compared=late=private_diffs=hidden_sets=0
 for step in range(300):
  (a,ai),(b,bi)=states;sa=int(ai['to_play'][0]);sb=int(bi['to_play'][0]);assert sa==sb
  turn=int(a['global_'][0,4]);assert turn==int(b['global_'][0,4])
  if turn>8:break
  if sa==viewer:
   assert all(np.array_equal(a[k],b[k]) for k in keys),(tag,turn,[k for k in keys if not np.array_equal(a[k],b[k])])
   compared+=1;late+=turn>1
   hidden_sets=max(hidden_sets,int(((a['cards_'][0,:,2]==3)&(a['cards_'][0,:,4]==1)).sum()))
  else:private_diffs+=not np.array_equal(a['cards_'],b['cards_'])
  actions=[]
  for obs,info in states:
   offers=obs['actions_'][0,:int(info['num_options'][0])];pick=len(offers)-1
   if sa!=viewer:
    sets=[i for i,x in enumerate(offers) if int(x[4])==5]
    if sets:pick=sets[0]
   actions.append(pick)
  states=[]
  for e,pick in zip(envs,actions):
   obs,rew,end,trunc,info=e.step(np.array([pick]));assert not end[0] and not trunc[0];states.append((obs,info))
 else:raise AssertionError('did not reach stop')
 assert late>0
 if mode=='opponent-hand-and-set':assert private_diffs>0 and hidden_sets>=3
 result=dict(seed=seed,viewer=viewer,mode=mode,compared=compared,later=late,private_differences=private_diffs,hidden_set_cards=hidden_sets);results.append(result);print(result,flush=True)
(OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n');sys.stdout.flush();os._exit(0)
