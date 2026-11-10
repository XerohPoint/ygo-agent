"""Supplementary evidence for written claims and possible setup artifacts."""
import os,sys,gzip,json,collections as C
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];os.chdir(ROOT);sys.path.insert(0,str(ROOT/'sol-recount'))
from audit import read,save
elapsed=C.Counter(); maxsteps=0;maxwhere=None;win_reasons=C.Counter();bad=[];logfiles=list(Path('build/edison-heldout/stage-a/logs').glob('*.log'))
markers=('timeout','Script not found','Card not found','Spec not found','Spec2index','cannot find id','MSG_RETRY','Error','stack traceback','terminate called')
for p in logfiles:
 for i,line in enumerate(p.read_text(errors='replace').splitlines(),1):
  if any(m in line for m in markers):bad.append([str(p),i,line])
for p in sorted(Path('build/edison-heldout/stage-a').glob('chunk-*.json.gz')):
 with gzip.open(p,'rt') as f:x=json.load(f)
 for r in x['rows']:
  for d in r['duels']:
   win_reasons[str(d['win_reason'])]+=1
   if d['steps']>maxsteps:maxsteps=d['steps'];maxwhere=[r['cell'],d['seed']]
 for line in p.with_suffix('').with_suffix('.jsonl').read_text().splitlines():
  e=json.loads(line)
  if e['event']=='result':elapsed[e['key'].split(':')[0]]+=e['result']['elapsed']
assert not bad
integrity=read('sol-recount/integrity.json');tot=integrity['totals'];guard={}
for b in ('known','held'):
 for arm in ('pilot','face'):
  rows=[v for k,v in integrity['guard_by_arm'].items() if k.startswith(b+'/'+arm+'/')];g=sum(v['guard_fires'] for v in rows);n=sum(v['net_decisions'] for v in rows);guard[b+'/'+arm]=dict(guard_fires=g,net_decisions=n,share=g/n)
save('claim-evidence.json',dict(network_decisions=tot['net_decisions'],heuristic_decisions=tot['decisions']-tot['net_decisions'],all_decisions=tot['decisions'],guard_fires=tot['guard_fires'],guard_share_network=tot['guard_fires']/tot['net_decisions'],guard_share_all=tot['guard_fires']/tot['decisions'],guard_by_block_arm=guard,worker_logs=len(logfiles),diagnostic_markers=bad,max_duel_steps=maxsteps,max_duel_cell_seed=maxwhere,win_reasons=win_reasons,cell_elapsed_seconds=elapsed,first_cell_hours=elapsed['first']/3600,first_cell_hours_per15=elapsed['first']/3600/15,all_cell_hours_per15=sum(elapsed.values())/3600/15,wall_clock_timestamps_in_journals=False))
print((ROOT/'sol-recount/claim-evidence.json').read_text())
