"""Supplemental checks on saved evidence; no games played."""
from pathlib import Path
import collections as C,gzip,json,hashlib
import numpy as np
R=Path(__file__).resolve().parent;M=Path('/home/mada/ygo-agent');D=M/'build/edison-retrain-2026-11-04'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
out={}
for s in (1702,1703,1704):
 p=D/'runs'/f'gauntlet-{s}';run=json.loads((p/'run.json').read_text());rows=[json.loads(l) for l in (p/'rounds.jsonl').read_text().splitlines()]
 for n in (160,320,640,1280,2560):
  cp=p/f'checkpoint-{n:04d}';meta=json.loads(cp.with_suffix('.json').read_text());assert meta['round']==n and meta['params_sha256']==rows[n-1]['params_sha256']==sha(cp.with_suffix('.msgpack'))
 for cp in p.glob('checkpoint-*.json'):
  meta=json.loads(cp.read_text());assert meta['train_config']==run['config'];assert meta['codes_sha256']==run['provenance']['codes_sha256']
out['checkpoint_training_links']='all 15 evaluation checkpoints match round log hashes; every retained checkpoint metadata matches its run config'
meta=json.loads((R/'tools/edison/learner/codes_v1.json').read_text());assert all(sha(M/v['path'])==v['sha256'] for v in meta['decks'].values());out['decks_matching_hashes']=len(meta['decks'])
out['native_sha256']=sha(M/'ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so')
out['mirror_duplicates']={}
for block in ('control-fixtures','control-gauntlet'):
 rows=json.load(gzip.open(D/'final'/block/'cells.json.gz','rt'));by={(r['deck0'],r['deck1'],r['seat']):r for r in rows};n=0
 for r in rows:
  if r['seat']==0:assert r['duels']==by[r['deck0'],r['deck1'],1]['duels'];n+=len(r['duels'])
 out['mirror_duplicates'][block]=n
obs=dict(np.load(R/'reviewer_targets/observation-84002000.npz'));mut={k:v.copy() for k,v in obs.items()};row=int(np.flatnonzero(mut['h_actions_'][0,:,3])[0]);mut['h_actions_'][0,row,6]^=1
assert not np.array_equal(obs['h_actions_'],mut['h_actions_'])
def trace(o):
 h=hashlib.sha256()
 for k in ('cards_','selection_','global_','actions_','decision_'):h.update(o[k][0].tobytes())
 h.update((0).to_bytes(2,'little'));return h.hexdigest()
assert trace(obs)==trace(mut);out['history_mutation_trace_unchanged']={'seed':84002000,'row':row,'column':6,'before':int(obs['h_actions_'][0,row,6]),'after':int(mut['h_actions_'][0,row,6])}
# Actor hashes only offered actions; both observations above have identical actions and count,
# so this equality also holds when its exact offered-action slice is used.
g=C.defaultdict(list);pairs=C.defaultdict(list);endings=C.defaultdict(C.Counter)
for block in ('fixtures','gauntlet','gauntlet-no-guard'):
 for r in json.load(gzip.open(D/'final'/block/'cells.json.gz','rt')):
  for d in r.get('duels',[]):
   endings[block][str((d['winner']==r['seat'],d['win_reason']))]+=1
   if block=='fixtures':
    deck=r['deck0'] if r['seat']==0 else r['deck1'];opp=r['deck1'] if r['seat']==0 else r['deck0'];w=d['winner']==r['seat'];g[r['challenger'],deck,r['seat']].append(w);pairs[deck,opp].append(w)
out['model_deck_seat']={str(k):[sum(v),len(v)] for k,v in g.items()};out['matchups']={str(k):[sum(v),len(v)] for k,v in pairs.items()};out['endings']=dict(endings)
counts=C.Counter();fields=C.Counter();maxt=0
for p in (R/'reviewer_real_games').glob('audit-*.jsonl'):
 for line in p.open():
  a=json.loads(line)
  if a['type']=='duel':
   counts['duels']+=1;counts['decisions']+=a['decisions'];counts['mismatches']+=a['mismatch_total'];counts['extra_deck_rows']+=a['extra_visible_rows']
   for k,v in a['counts'].items():
    counts['options']+=v[1]
    for i in (3,4,5,6,8,9,10,12,13):fields[str(i)]+=v[i]
  elif a['type']=='decision':
   maxt=max(maxt,a['turn']);counts['later_decisions']+=a['turn']>1;counts['later_deck_prompts']+=(a['turn']>1 and any(x['spec_kind']=='deck' and x['controller']==a['player'] for x in a['options']))
out['real_duel_audit']={'counts':dict(counts),'fields':dict(fields),'max_turn':maxt}
out['metamorphic']={name:json.loads((R/f'{name}/results.json').read_text()) for name in ('reviewer_privacy','reviewer_hidden','reviewer_targets')}
(R/'reviewer_supplement_results.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({k:v for k,v in out.items() if k not in ('model_deck_seat','metamorphic')},indent=2))
