import collections as C,gzip,hashlib,itertools,json,math
from pathlib import Path
R=Path(__file__).resolve().parent; D=Path('/home/mada/ygo-agent/build/edison-retrain-2026-11-04'); O=(1702,1703,1704); F=('blackwing','frog_monarch','machina_gadget')
N=sorted((*F,*('g_'+x['name'] for x in json.loads((R/'assets/edison/gauntlet_candidates/manifest.json').read_text())['decks'])))
FP=list(itertools.permutations(F,2)); GP=[(x,N[(i+j)%21]) for i,x in enumerate(N) for j in range(1,5)]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def load(p):return json.load(gzip.open(p/'cells.json.gz','rt'))
def avg(v):return sum(v)/len(v)
def pct(t):return 100*avg(list(t.values()))
def table(rows,label,op='heuristic'):
 t={};void=0
 for r in rows:
  if r['challenger']!=label or r['opponent']!=op:continue
  if r['status']!='PASS':void+=r['games'];continue
  for d in r['duels']:
   k=(r['deck0'],r['deck1'],r['seat'],d['seed']);assert k not in t;t[k]=int(d['winner']==r['seat'])
 return t,void
def per(rows,prefix):return [table(rows,f'{prefix}-{s}')[0] for s in O]
def rates(ts):return [pct(t) for t in ts]
def gain(a,b):return [100*avg([x[k]-y[k] for k in x.keys()&y.keys()]) for x,y in zip(a,b)]
def split(ts,f):
 g=C.defaultdict(list)
 for t in ts:
  for k,v in t.items():g[str(f(k))].append(v)
 return {k:100*avg(v) for k,v in sorted(g.items())}
def low(t):
 n=len(t);p=avg(list(t.values()));z=1.96
 return 100*(p+z*z/(2*n)-z*math.sqrt(p*(1-p)/n+z*z/(4*n*n)))/(1+z*z/n)
res={'matrix':{},'training':{},'eval':{},'final':{}}
for phase in ('eval','final'):
 for p in sorted((D/phase).iterdir()):
  if not (p/'cells.json.gz').exists():continue
  rows=load(p);sm=json.loads((p/'summary.json').read_text());fix=sm['decks']=='fixtures';pairs=FP if fix else GP
  seed=((82000000 if fix else 82010000) if phase=='eval' else (83000000 if fix else 83010000));n=((64 if fix else 8) if phase=='eval' else (256 if fix else 32))
  expected=set(itertools.product({r['challenger'] for r in rows},{r['opponent'] for r in rows},pairs,(0,1)));seen=set();why=C.Counter();out=C.Counter();void=C.Counter();hs={};groups=C.defaultdict(C.Counter)
  for r in rows:
   k=(r['challenger'],r['opponent'],(r['deck0'],r['deck1']),r['seat']);assert k not in seen;seen.add(k);assert r['seed']==seed and r['games']==n
   guard=not fix and not p.name.startswith(('control','numpy')) and p.name!='gauntlet-no-guard'
   for seat,spec in enumerate(r['policies']):
    if spec['kind']=='net':
     assert spec['greedy'] is True and bool(spec.get('repeat_guard'))==guard
     cp=Path(spec['checkpoint']);assert sha(cp.with_suffix('.msgpack'))==spec['sha256'];assert json.loads(cp.with_suffix('.json').read_text())['params_sha256']==spec['sha256'];hs[str(cp)]=spec['sha256']
    if seat!=r['seat'] and r['opponent']=='heuristic':assert spec=={'kind':'heuristic'}
   g=groups[r['challenger']+'|'+r['opponent']]
   if r['status']!='PASS':void[str(r.get('reason'))]+=n;g['void_games']+=n;continue
   assert len(r['duels'])==n and [d['seed'] for d in r['duels']]==list(range(seed,seed+n)) and [d['env'] for d in r['duels']]==list(range(n))
   for d in r['duels']:
    assert d['winner'] in (0,1,2) and 0<d['steps']<10000
    why[str(d['win_reason'])]+=1;out['draw' if d['winner']==2 else 'win' if d['winner']==r['seat'] else 'loss']+=1
    g['games']+=1;g['decisions']+=d['steps'];g['wins']+=d['winner']==r['seat'];g['draws']+=d['winner']==2;g['losses']+=d['winner']==1-r['seat']
  assert expected==seen and sm['seed']==seed and sm['seeds_per_cell']==n and sm['greedy']
  assert sm['replayed_cells']==len(range(0,len(rows),24)) and not sm['replay_mismatches']
  for name,h in sm['native_sha256'].items():assert sha(Path('/home/mada/ygo-agent/ygoenv/ygoenv/ygopro')/name)==h
  for k,g in groups.items():
   for field in ('games','decisions','wins','draws','losses','void_games'):assert g[field]==sm['total'][k][field]
  res['matrix'][phase+'/'+p.name]={'cells':len(rows),'outcomes':dict(out),'void':dict(void),'reasons':dict(why),'max_steps':max(d['steps'] for r in rows if r['status']=='PASS' for d in r['duels']),'replays':sm['replayed_cells'],'guard_fires':sm['guard_fires'],'hashes':hs}
for s in O:
 p=D/'runs'/f'gauntlet-{s}';run=json.loads((p/'run.json').read_text());rr=[json.loads(l) for l in (p/'rounds.jsonl').read_text().splitlines()]
 assert [r['round'] for r in rr]==list(range(1,2561)) and rr[-1]['duels']+sum(r['void_games'] for r in rr)==245760 and rr[-1]['params_sha256']==sha(p/'checkpoint-2560.msgpack')
 for name,h in run['provenance']['learner_sources'].items():assert sha(R/'tools/edison/learner'/name)==h
 assert sha(R/'ygoai/edison_policy.py')==run['provenance']['edison_policy']
 res['training'][str(s)]={'rounds':len(rr),'duels':rr[-1]['duels'],'void':sum(r['void_games'] for r in rr),'run':run,'first':rr[0],'last':rr[-1]}
for block,suffix in (('fixtures','pilot'),('gauntlet','on-gauntlet')):
 ts={r:per(load(D/'eval'/f'gauntlet{r}-{suffix}'),f'gauntlet{r}') for r in (160,320,640,1280,2560)};rs={r:rates(t) for r,t in ts.items()};g1=[a-b for a,b in zip(rs[2560],rs[160])];g2=avg(rs[2560])-avg(rs[1280]);bt=per(load(D/'eval'/('numpy-pilot' if block=='fixtures' else 'numpy-on-gauntlet')),'numpy')
 res['eval'][block]={'rates':rs,'G1_gains':g1,'G1':'MORE_GAMES_HELP' if avg(g1)>=5 and min(g1)>0 else 'NOT_SHOWN','G2_gain':g2,'G2':'STILL_RISING' if g2>=2 else 'FLAT_OR_NOISE','G3_pass':avg(rs[2560])>=55 and min(rs[2560])>=50,'baseline_rates':rates(bt),'G4_gains':gain(ts[2560],bt)}
res['eval']['h2h']=[pct(table(load(D/'eval'/f'gauntlet2560-vs-numpy-pilot-{s}'),f'gauntlet2560-{s}',f'numpy-{s}')[0]) for s in O]
for block in ('fixtures','gauntlet','gauntlet-no-guard'):
 rows=load(D/'final'/block);ts=per(rows,'net');rs=rates(ts);bd=split(ts,lambda k:k[k[2]]);bs=split(ts,lambda k:k[2]);ls=[low(t) for t in ts];vs=[table(rows,f'net-{s}')[1] for s in O]
 checks={'mean':avg(rs)>=55,'lower_bounds':min(ls)>50,'seats':min(bs.values())>=50,'valid':all(v/(v+len(t))<=.01 for v,t in zip(vs,ts))}
 if block=='fixtures':checks['decks']=min(bd.values())>=50
 res['final'][block]={'rates':rs,'mean':avg(rs),'wins':[sum(t.values()) for t in ts],'games':[len(t) for t in ts],'lows':ls,'by_deck':bd,'by_seat':bs,'void':vs,'checks':checks,'pass':all(checks.values())}
 if block=='gauntlet':guarded=ts
 if block=='gauntlet-no-guard':res['final']['guard_gain']=gain(guarded,ts)
res['verdict']='M1_EXIT_MET' if all(res['final'][b]['pass'] for b in ('fixtures','gauntlet')) else 'NOT_MET'
(R/'reviewer_audit_results.json').write_text(json.dumps(res,indent=2)+'\n')
print(json.dumps({k:v for k,v in res.items() if k not in ('matrix','training')},indent=2));print('blocks',len(res['matrix']),'replays',sum(x['replays'] for x in res['matrix'].values()),'void',[(k,v['void']) for k,v in res['matrix'].items() if v['void']])
