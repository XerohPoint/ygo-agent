"""Post-hoc artwork-alias sensitivity; no games are played."""
from pathlib import Path
import os,sys,sqlite3,collections as C,numpy as np,gzip
ROOT=Path(__file__).resolve().parents[1];os.chdir(ROOT);sys.path.insert(0,str(ROOT/'sol-recount'))
import audit as a
aliases={r[0]:r[1] for r in sqlite3.connect('assets/locale/en/cards.cdb').execute('select id,alias from datas')}
def norm(z):return {k:[aliases[c] if aliases.get(c) and abs(aliases[c]-c)<20 else c for c in cards if c!=52352005] for k,cards in z.items()}
train={n:norm(a.ydk(p)) for n,p in a.training_paths().items()};tc={n:a.counts(z) for n,z in train.items()};trainkeys={a.signature(z):n for n,z in train.items()}
seen={};duplicates=[];changes=[];held=[];indices=[];aliased=[]
# Canonical representative is the lowest numeric list id, as in the protocol.
for i,row in sorted(enumerate(a.H),key=lambda item:int(item[1]['list_id'][2:])):
 n=row['name'];z=a.ydk(ROOT/f'build/edison-heldout/decks/{n}.ydk');zn=norm(z);key=a.signature(zn)
 if a.signature(z)!=key:aliased.append(n)
 assert key not in trainkeys,(n,trainkeys.get(key))
 if key in seen:duplicates.append([seen[key],n])
 else:seen[key]=n;indices.append(i)
 c=a.counts(zn);d={n:sum((c-tc[n]).values())+sum((tc[n]-c).values()) for n in a.T};t=min(d,key=lambda n:(d[n],n));distance=d[t];far=distance>=20;new=dict(row,twin=t,distance=distance,far=far,band='1-9' if distance<10 else ('10-19' if distance<20 else '20+'))
 if (t,distance,far)!=(row['twin'],row['distance'],row['far']):changes.append(dict(name=n,old_twin=row['twin'],new_twin=t,old_distance=row['distance'],new_distance=distance,old_far=row['far'],new_far=far))
 held.append((i,new))
a.save('alias-evidence.json',dict(artwork_passcodes_used=aliased,canonical_training_duplicates=[],canonical_held_duplicate_pairs=duplicates,changed_metadata=changes,changed_metadata_count=len(changes),far_flag_changes=[r for r in changes if r['old_far']!=r['new_far']],canonical_unique_builds=len(seen)))
kept={i:r for i,r in held if i in indices};ix=sorted(kept);newH=[kept[i] for i in ix]
# Reuse only our own independently written bootstrap, keeping its outputs separate.
a.H=newH;a.N=[r['name'] for r in newH];a.OUT=ROOT/'sol-recount/alias-sensitivity';a.OUT.mkdir(exist_ok=True)
z=np.load(ROOT/'sol-recount/counts.npz');q=a.read(ROOT/'sol-recount/integrity.json')
# Keep the subset's bookkeeping consistent; the retained games themselves are unchanged.
keep_names=set(a.N);subset_totals=C.Counter()
for p in sorted((ROOT/'build/edison-heldout/stage-a').glob('chunk-*.json.gz')):
    with gzip.open(p,'rt') as f:chunk=__import__('json').load(f)
    for row in chunk['rows']:
        if row['block']=='held' and row['list'] not in keep_names:continue
        subset_totals['cells']+=1;subset_totals['games']+=row['games']
        for key in ('decisions','net_decisions','guard_fires'):subset_totals[key]+=row['counters'][key]
for key in ('draws','unresolved_first','unresolved_replay','missing_chunks','invalid_chunks','replay_mismatches','empty_list_arms'):subset_totals[key]=0
q['totals']=subset_totals
for key in q['voids']:
    if key.startswith('held/'):q['voids'][key]['games']=len(ix)*168
a.recount({b:z[b+'_wins'][ix] if b=='held' else z[b+'_wins'] for b in ('known','held')},{b:z[b+'_games'][ix] if b=='held' else z[b+'_games'] for b in ('known','held')},{b:z[b+'_worst'][ix] if b=='held' else z[b+'_worst'] for b in ('known','held')},q['totals'],q['voids'],q['void_reasons'])
print('alias sensitivity complete',len(newH),sum(r['far'] for r in newH),len(changes),'metadata changes',len(duplicates),'duplicate pairs')
