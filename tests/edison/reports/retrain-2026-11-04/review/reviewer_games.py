"""Real-game audit using the unmodified actor and three actual final networks."""
import os,sys,json,hashlib,importlib.util
from pathlib import Path
R=Path(__file__).resolve().parent;M=Path('/home/mada/ygo-agent');OUT=R/'reviewer_real_games';OUT.mkdir(exist_ok=True)
sys.dont_write_bytecode=True
os.environ.update(EDISON_PROMPT_AUDIT=str(OUT),EDISON_PROMPT_AUDIT_TRACE='1',OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1',JAX_PLATFORMS='cpu',XLA_FLAGS='--xla_cpu_multi_thread_eigen=false intra_op_parallelism_threads=1')
sys.path[:0]=[str(R/'tools/edison/learner'),str(R),str(R/'ygoenv')]
so=M/'ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so';spec=importlib.util.spec_from_file_location('ygoenv.ygopro.ygopro_ygoenv',str(so));module=importlib.util.module_from_spec(spec);sys.modules[spec.name]=module;spec.loader.exec_module(module)
import decks
decks.DB=M/'assets/locale/en/cards.cdb';decks.GENERATED=OUT/'decks'
import actor
from ygoai import edison_policy as ep
orig=ep.Heuristic.choose
counters={};examples=[]
def choose(self,cards,glob,actions):
 answer=orig(self,cards,glob,actions);picked=actions[answer[0]]
 for a in actions:
  cid=int(a[1])*256+int(a[2]);name=self.metadata.get(cid,{}).get('name','');act=int(a[4]);msg=int(a[3]);k=(name,act,msg)
  if act==8:
   b=counters.setdefault(str(k),[0,0]);b[0]+=1;b[1]+=bool((a==picked).all())
 return answer
ep.Heuristic.choose=choose
os.chdir(M/'scripts');runtime=actor.Runtime();names=sorted(decks.registry());results=[]
for opt in (1702,1703,1704):
 stem=M/f'build/edison-retrain-2026-11-04/runs/gauntlet-{opt}/checkpoint-2560';meta=json.loads(stem.with_suffix('.json').read_text())
 net={'kind':'net','checkpoint':str(stem),'sha256':meta['params_sha256'],'greedy':True,'repeat_guard':True}
 for i,name in enumerate(names):
  other=names[(i+1)%len(names)]
  for seat in (0,1):
   job={'seed':84010000,'games':8,'deck0':name,'deck1':other,'policies':[net,{'kind':'heuristic'}] if seat==0 else [{'kind':'heuristic'},net]}
   try:out=runtime.run_cell(job)
   except Exception as e:out={'status':'VOID','reason':repr(e)}
   results.append({'opt':opt,'seat':seat,'deck0':name,'deck1':other,**out})
 print(opt,'cells',len(results),'void',sum(x['status']!='PASS' for x in results),flush=True)
(OUT/'cells.json').write_text(json.dumps(results));(OUT/'heuristic.json').write_text(json.dumps(counters,indent=2))
print('DONE',len(results),sum(len(x.get('duels',[])) for x in results),flush=True)
os._exit(0)
