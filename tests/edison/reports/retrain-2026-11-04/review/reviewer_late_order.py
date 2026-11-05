"""Extend actual production 087 scenarios to turn three; no modified Lua scripts."""
import os,sys,subprocess,json,sqlite3,importlib.util
from pathlib import Path
R=Path(__file__).resolve().parent;M=Path('/home/mada/ygo-agent');OUT=R/'reviewer_privacy';SO=M/'ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so'
sys.dont_write_bytecode=True
sys.path[:0]=[str(R/'tests/edison'),str(R),str(R/'ygoenv'),str(R/'tools/edison/pool_audit')]
import production_prompt_tests as pp
pp.DB=M/'assets/locale/en/cards.cdb';pp.ROOT=M
import production_deck_label_tests as dl
dl.DB=pp.DB;dl.ROOT=M
import production_deck_order_tests as t
t.DB=pp.DB;t.ROOT=M
if len(sys.argv)>1:
 mode=sys.argv[1];codes=Path(sys.argv[2]);deck=Path(sys.argv[3]);seed=int(sys.argv[4])
 if mode=='probe':t.probe(codes,deck,seed,SO)
 else:
  import inspect
  src=inspect.getsource(t.worker).replace('if int(glob[4]) > 1:', 'if int(glob[4]) > 4:').replace('if wanted:', 'if wanted and int(glob[4]) >= 3:')
  exec(src,t.__dict__)
  t.worker(mode,codes,deck,seed,SO)
 sys.exit()
OUT.mkdir(exist_ok=True);codes=OUT/'codes.txt';pp.code_list(codes);db=sqlite3.connect(pp.DB);probe=OUT/'probe.ydk';t.write_list(probe,t.filler(db,40,set()))
results=[]
for start in (84000000,84000100,84000200,84000300):
 command=[sys.executable,str(Path(__file__).resolve()),'probe',str(codes),str(probe),str(start)]
 done=subprocess.run(command,cwd=M/'scripts',capture_output=True,text=True,timeout=30);assert done.returncode==0,done.stderr
 layout=json.loads(done.stdout.split('RESULT',1)[1]);seed=layout['seed']
 for case in ('A','B','C'):
  d=OUT/f'{case}-{seed}';d.mkdir(exist_ok=True);audit=d/'audit';audit.mkdir(exist_ok=True)
  lists=t.build(db,case,layout['protect'])
  for seat in (0,1):
   for j,tag in enumerate('ab'):t.write_list(d/f'{tag}{seat}.ydk',lists[seat][j])
  env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1','EDISON_PROMPT_AUDIT':str(audit),'EDISON_PROMPT_AUDIT_TRACE':'1','OPENBLAS_NUM_THREADS':'1','OMP_NUM_THREADS':'1'}
  done=subprocess.run([sys.executable,str(Path(__file__).resolve()),case,str(codes),str(d),str(seed)],cwd=M/'scripts',env=env,capture_output=True,text=True,timeout=60)
  assert done.returncode==0,(done.stdout[-1000:],done.stderr[-1000:]);r=json.loads(done.stdout.split('RESULT',1)[1]);(d/'observations.json').write_text(json.dumps(r))
  x=t.compare(case,r['a'],r['b']);late=[a for a in r['a'] if a['turn']>1];late_deck=[a for a in late if ['deck',True] in a['options']]
  assert late_deck
  x.update(seed=seed,case=case,later_decisions=len(late),later_deck_prompts=len(late_deck),max_turn=max(a['turn'] for a in r['a']))
  results.append(x);print(x,flush=True)
(OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n')
