"""Independent protocol-based design, journal audit and recount. Never imports stage_a."""
import collections as C
import gzip, hashlib, json, os, sys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
os.chdir(ROOT)
OUT=ROOT/'sol-recount'
def read(p): return json.loads(Path(p).read_text())
def save(p,x): (OUT/p).write_text(json.dumps(x,indent=1)+'\n')
def sha(b): return hashlib.sha256(b).hexdigest()
def ydk(p):
    zones={'main':[], 'extra':[], 'side':[]}; z=None
    for s in Path(p).read_text().splitlines():
        s=s.strip()
        if s in ('#main','#extra','!side'): z=s.lstrip('#!')
        elif s.isdigit(): zones[z].append(int(s))
    return zones
def signature(z): return tuple(tuple(sorted(z[k])) for k in ('main','extra'))
def counts(z): return C.Counter(z['main']+z['extra'])
L=read('tools/edison/heldout/stage-a-lists.json'); M=read('tools/edison/heldout/m3a-manifest.json')
T=sorted(L['training_lists']); H=sorted(L['lists'],key=lambda r:r['name']); N=[r['name'] for r in H]
# Resolve training lists directly, applying the explicitly documented Gottoms rule.
def training_paths():
    d={'blackwing':ROOT/'assets/edison/decks/blackwing.ydk', 'frog_monarch':ROOT/'assets/edison/decks/frog_monarch.ydk','machina_gadget':ROOT/'assets/edison/agent_decks/machina_gadget.ydk'}
    for r in read('assets/edison/gauntlet_candidates/manifest.json')['decks']:
        n=r['name']; d['g_'+n]=ROOT/f'assets/edison/gauntlet_candidates/{n}.ydk'
    return d

def policy(p):
    if p=='heuristic': return {'kind':'heuristic'}
    return {'kind':'net','sha256':M['inputs']['checkpoints'][p],'greedy':True,'repeat_guard':True}

def jobs():
    for block,names,base,games in [('known',T,88180000,32),('held',N,88130000,4)]:
        for i,x in enumerate(names):
            for j,o in enumerate(T):
                for arm in ('pilot','face','mirror'):
                    for p in (('heuristic',) if arm=='mirror' else ('1702','1703','1704')):
                        for seat in (0,1):
                            mine,other=(o,x) if arm=='face' else (x,o)
                            yield dict(block=block,arm=arm,policy=p,list=x,opponent=o,deck0=mine if seat==0 else other,deck1=other if seat==0 else mine,seat=seat,seed=base+games*(21*i+j),games=games,policies=[policy(p),policy('heuristic')] if seat==0 else [policy('heuristic'),policy(p)])
# Serialization is the recorded format: sorted-key JSON, checkpoint paths omitted.
def jobtext(j): return json.dumps(j,sort_keys=True)
def differences(a,b,path=''):
    if isinstance(a,dict) and isinstance(b,dict):
        return sum((differences(a.get(k),b.get(k),path+'/'+str(k)) for k in a.keys()|b.keys()),[])
    if isinstance(a,list) and isinstance(b,list):
        if len(a)!=len(b): return [(path+'/length',len(a),len(b))]
        return sum((differences(x,y,path+'/'+str(i)) for i,(x,y) in enumerate(zip(a,b))),[])
    if isinstance(a,(float,int)) and isinstance(b,(float,int)) and not isinstance(a,bool) and not isinstance(b,bool):
        return [] if abs(a-b)<1e-12 else [(path,a,b)]
    return [] if a==b else [(path,a,b)]

def main():
    J=list(jobs()); jt=[jobtext(j) for j in J]
    assert len(J)==149646 and sum(j['games'] for j in J)==771456
    assert sha('\n'.join(jt).encode())==M['inputs']['jobs'], 'matrix hash'
    # Check source card copies, deployed copies, twins, exact duplicates and coverage.
    paths=training_paths(); train={n:ydk(p) for n,p in paths.items()}
    for z in train.values(): z['extra']=[c for c in z['extra'] if c!=52352005]
    keys={n:signature(z) for n,z in train.items()}; tc={n:counts(z) for n,z in train.items()}
    used=set().union(*(set(c) for c in tc.values())); all_training=set().union(*(set(sum(z.values(),[])) for z in train.values())); codes={int(s.split()[0]) for s in Path('tools/edison/learner/codes_v1.txt').read_text().splitlines()}
    seen={}; deck_errors=[]; removed=[]
    for r in H:
        name=r['name']; source=ydk(ROOT/'assets/edison/corpus'/r['source']); actual=ydk(ROOT/f'build/edison-heldout/decks/{name}.ydk')
        assert sha((ROOT/'assets/edison/corpus'/r['source']).read_bytes())==r['source_sha256']
        drop=source['extra'].count(52352005); source['extra']=[c for c in source['extra'] if c!=52352005]; source['side']=[]
        if source!=actual: deck_errors.append(name)
        k=signature(actual); assert k not in keys.values() and k not in seen, ('duplicate',name); seen[k]=name
        assert set(counts(actual))<=used|(codes-all_training)
        assert set(counts(actual))<=codes
        v=counts(actual); dist={n:sum((v-tc[n]).values())+sum((tc[n]-v).values()) for n in T}
        twin=min(T,key=lambda n:(dist[n],n)); assert twin==r['twin'] and dist[twin]==r['distance']
        assert r['far']==(dist[twin]>=20)
        if drop: removed.append(name)
    assert not deck_errors
    save('deck-audit.json',dict(builds=len(H),training_lists=T,exact_training_duplicates=0,duplicate_held_builds=0,source_env_disagreements=deck_errors,gottoms_removed=removed,untrained_in_env=sorted(set().union(*(set(counts(ydk(ROOT/f'build/edison-heldout/decks/{n}.ydk'))) for n in N))-used)))
    wins={'held':np.zeros((488,42,7),dtype=np.int64),'known':np.zeros((21,42,7),dtype=np.int64)}
    games={b:np.zeros_like(a) for b,a in wins.items()}; worst={b:np.zeros_like(a) for b,a in wins.items()}
    slots={('pilot','1702'):0,('pilot','1703'):1,('pilot','1704'):2,('face','1702'):3,('face','1703'):4,('face','1704'):5,('mirror','heuristic'):6}
    rng=__import__('random').Random(20261110)
    # One RNG-chosen cell in each block/arm/optimizer/seat stratum: 28, plus 14 independent picks.
    picks=[]
    for b in ('known','held'):
        for a in ('pilot','face','mirror'):
            for p in (('heuristic',) if a=='mirror' else ('1702','1703','1704')):
                for s in (0,1): picks.append(rng.choice([i for i,j in enumerate(J) if (j['block'],j['arm'],j['policy'],j['seat'])==(b,a,p,s)]))
    picks+=rng.sample([i for i in range(len(J)) if i not in picks],14); picks=sorted(picks); selected=[]
    totals=C.Counter(); reasons=C.Counter(); voids={}; guard_stats={}; reason_winners=C.Counter(); journal_fields=C.Counter(); replays=[]; cells_seen=[]
    allpaths=sorted(Path('build/edison-heldout/stage-a').glob('chunk-*.json.gz'));assert len(allpaths)==82
    for cp in allpaths:
        c=read_gz(cp); ci=c['chunk']; rows=c['rows']; ids=[r['cell'] for r in rows]
        assert cp.name==f'chunk-{ci:03}.json.gz'
        expected_ids=list(range(ci*294,(ci+1)*294)) if ci<21 else list(range(6174+(ci-21)*2352,6174+(ci-20)*2352))
        assert ids==expected_ids
        assert c['jobs_sha256']==sha('\n'.join(jt[i] for i in ids).encode())
        starts=C.Counter(); results={}
        raw=cp.with_suffix('').with_suffix('.jsonl').read_bytes();assert raw.endswith(b'\n')
        for line in raw.splitlines():
            e=json.loads(line);key=e['key']
            if e['event']=='start': assert key not in results; starts[key]+=1
            elif e['event']=='result': assert key not in results and starts[key]>0; results[key]=e['result']
            else: raise AssertionError(e)
        assert set(results)==set(starts)
        expected_keys={f'first:{i}' for i in ids}|{f'replay:{i}' for i in ids if i%127==0};assert set(results)==expected_keys
        for r in rows:
            i=r['cell'];j=J[i];cells_seen.append(i)
            assert r['job_sha256']==sha(jt[i].encode())
            for k,v in j.items():
                if k!='policies': assert r[k]==v,(i,k,r[k],v)
            first=results[f'first:{i}']; uf=starts[f'first:{i}']-1;ur=starts.get(f'replay:{i}',1)-1
            assert (r['unresolved_first'],r['unresolved_replay'],r['unresolved_starts'])==(uf,ur,uf+ur)
            for k in ('status','duels','reason','counters'): assert r[k]==first.get(k),(i,k)
            journal_fields.update(first.keys())
            if i in picks: selected.append(dict(job=j,record=r,journal=first))
            k=slots[r['arm'],r['policy']];b=r['block'];ix=(T if b=='known' else N).index(r['list']);cell=2*T.index(r['opponent'])+r['seat']
            key='/'.join((b,r['arm'],r['policy']));v=voids.setdefault(key,dict(void=0,games=0,share=0.))
            v['games']+=r['games'];totals['cells']+=1;totals['games']+=r['games'];totals['unresolved_first']+=uf;totals['unresolved_replay']+=ur
            if r['status']!='PASS': v['void']+=r['games'];reasons[r['reason']]+=r['games'];continue
            assert len(r['duels'])==r['games']
            w=0
            for eid,d in enumerate(r['duels']):
                assert d['env']==eid and d['seed']==r['seed']+eid and d['winner'] in (0,1,2)
                assert d['steps']>0 and len(d['trace'])==64
                w+=d['winner']==r['seat'];totals['draws']+=d['winner']==2;reason_winners[str(d['win_reason'])]+=1
            assert r['counters']['decisions']==sum(d['steps'] for d in r['duels'])
            assert r['counters']['net_decisions']+r['counters']['heuristic_decisions']==r['counters']['decisions']
            wins[b][ix,cell,k]=w; games[b][ix,cell,k]=r['games'];worst[b][ix,cell,k]=w if uf+ur==0 else 0
            totals['decisions']+=r['counters']['decisions'];totals['net_decisions']+=r['counters']['net_decisions'];totals['guard_fires']+=r['counters']['guard_fires']
            gs=guard_stats.setdefault(key,C.Counter());gs.update({k:r['counters'][k] for k in ('decisions','net_decisions','guard_fires')});gs['cells_guarded']+=r['counters']['guard_fires']>0;gs['wins']+=w;gs['games']+=r['games']
        assert [r['cell'] for r in c['replays']]==[i for i in ids if i%127==0]
        byid={r['cell']:r for r in rows}
        for r in c['replays']:
            i=r['cell'];f=byid[i];s=results[f'replay:{i}'];assert r['status']==f['status']==s['status'] and r['duels']==f['duels']==s['duels'];assert r['unresolved_starts']==starts[f'replay:{i}']-1; replays.append(i)
        print('chunk',ci,'checked',flush=True) if ci%10==0 else None
    assert cells_seen==list(range(len(J))) and replays==list(range(0,len(J),127))
    for v in voids.values():v['share']=v['void']/v['games']
    totals.update(chunks=len(allpaths),replays=len(replays),missing_chunks=0,invalid_chunks=0,replay_mismatches=0)
    empty=sum(np.count_nonzero(g.reshape(g.shape[0],21,2,7).sum(axis=1)==0) for g in games.values());totals['empty_list_arms']=int(empty)
    save('selected-cells.json',selected);save('integrity.json',dict(totals=totals,voids=voids,void_reasons=reasons,guard_by_arm=guard_stats,win_reasons=reason_winners,journal_result_fields=journal_fields))
    np.savez(OUT/'counts.npz',**{f'{b}_{q}':a for q,arr in [('wins',wins),('games',games),('worst',worst)] for b,a in arr.items()})
    recount(wins,games,worst,totals,voids,reasons)

def read_gz(p):
    with gzip.open(p,'rt') as f:return json.load(f)

def recount(wins,games,worst,totals,voids,reasons):
    # Every arm shares cell resampling, and each twin is drawn once per replicate.
    W=wins['held'];G=games['held'];K=wins['known'];KG=games['known'];tw=np.array([T.index(r['twin']) for r in H]);far=np.array([r['far'] for r in H])
    hr=W.sum(axis=1)/G.sum(axis=1);kr=K.sum(axis=1)/KG.sum(axis=1)
    raw=(hr[:,:3]-kr[tw,:3]).mean(axis=1);gap=raw-hr[:,6]+kr[tw,6]
    per={}
    for i,r in enumerate(H):per[r['name']]=dict(archetype=r['archetype'],variants=len(r['variants']),twin=r['twin'],distance=r['distance'],pilot=float(hr[i,:3].mean()),face=float(hr[i,3:6].mean()),mirror=float(hr[i,6]),gap=float(gap[i]),raw_gap=float(raw[i]))
    report={'per_list':per}
    for label,attr in [('by_band','band'),('by_archetype','archetype')]:
        report[label]={}
        for value in sorted({r[attr] for r in H}):
            ix=np.array([i for i,r in enumerate(H) if r[attr]==value]);report[label][value]=dict(lists=len(ix),pilot=float(hr[ix,:3].mean()),face=float(hr[ix,3:6].mean()),gap=float(gap[ix].mean()),raw_gap=float(raw[ix].mean()))
    report['pilot_each_archetype_equal']=float(np.mean([r['pilot'] for r in report['by_archetype'].values()]))
    for name,a in [('gap_spread',gap),('raw_gap_spread',raw)]:report[name]=dict(zip(('p10','p50','p90'),map(float,np.quantile(a,[.1,.5,.9]))))
    report['known_lists']={n:dict(pilot=float(kr[i,:3].mean()),face=float(kr[i,3:6].mean()),mirror=float(kr[i,6])) for i,n in enumerate(T)}
    for b in ('known','held'):
        report[b+'_pilot']={p:float(wins[b][:,:,i].sum()/games[b][:,:,i].sum()) for i,p in enumerate(('1702','1703','1704'))}
        # seat zero is absolute seat zero, not only mirror's counted seat zero.
        w=wins[b][:,:,6];g=games[b][:,:,6]
        report[b+'_mirror_seat0']=float((w[:,::2].sum()+g[:,1::2].sum()-w[:,1::2].sum())/g.sum())
    numbers=dict(lists=len(N),far_lists=int(far.sum()),far_gated=bool(far.sum()>=10))
    brng=np.random.default_rng(88810001)
    for label,ix in [('all',np.arange(len(N))),('far',np.flatnonzero(far))]:
        # Independent draw order: known cells, builds, held cells. 10k replicates.
        bs=np.empty((10000,7));undefined=0
        for draw in range(10000):
            kc=brng.integers(0,42,size=(21,42));kw=K[np.arange(21)[:,None],kc].sum(axis=1);kg=KG[np.arange(21)[:,None],kc].sum(axis=1)
            picked=brng.choice(ix,size=len(ix),replace=True);hc=brng.integers(0,42,size=(len(ix),42));hw=W[picked[:,None],hc].sum(axis=1);hg=G[picked[:,None],hc].sum(axis=1)
            rate=hw.sum(axis=0)/hg.sum(axis=0);bs[draw,:6]=rate[:6]
            with np.errstate(divide='ignore',invalid='ignore'):
                hrate=hw/hg;krate=kw/kg;sg=((hrate[:,:3]-hrate[:,6,None])-(krate[tw[picked],:3]-krate[tw[picked],6,None])).mean(axis=1)
            bad=~np.isfinite(sg);undefined+=bool(bad.any());sg[bad]=-2.;bs[draw,6]=sg.mean()
        s=dict(lists=len(ix),undefined_draws=undefined)
        for a,start in [('pilot',0),('face',3)]:
            ww=W[ix,:,start:start+3];gg=G[ix,:,start:start+3]
            opt={p:dict(games=int(gg[:,:,i].sum()),rate=float(ww[:,:,i].sum()/gg[:,:,i].sum()),low=float(np.quantile(bs[:,start+i],.025))) for i,p in enumerate(('1702','1703','1704'))}
            s[a]=dict(mean=float(np.mean([o['rate'] for o in opt.values()])),worst_case_mean=float((worst['held'][ix,:,start:start+3].sum(axis=(0,1))/np.full(3,len(ix)*42*4)).mean()),by_optimizer=opt,by_seat={str(seat):float(ww[:,seat::2].sum()/gg[:,seat::2].sum()) for seat in (0,1)})
        s['gap']=dict(mean=float(gap[ix].mean()),low=float(np.quantile(bs[:,6],.025)),high=float(np.quantile(bs[:,6],.975)))
        s['raw_gap']=dict(mean=float(raw[ix].mean()));numbers[label]=s; print(label,s,flush=True)
    numbers['voids']=voids;numbers['integrity']={k:totals[k] for k in ('missing_chunks','invalid_chunks','replay_mismatches','empty_list_arms')}
    broken=[]
    for label in ('all','far'):
        for a in ('pilot','face'):
            s=numbers[label][a]
            if s['mean']<.55 or min(v['low'] for v in s['by_optimizer'].values())<=.5 or min(s['by_seat'].values())<.5:broken.append(label+'/'+a)
            if s['worst_case_mean']<.55:broken.append(label+'/worst/'+a)
        if numbers[label]['gap']['low']<=-.1:broken.append(label+'/gap')
    if max(v['share'] for v in voids.values())>.01 or any(numbers['integrity'].values()):broken.append('integrity')
    result=dict(verdict='NOT_MET' if broken else 'STAGE_A_MET',broken=broken,numbers=numbers,reported=report,void_reasons=dict(reasons),unresolved_first=totals['unresolved_first'],unresolved_replay=totals['unresolved_replay'],draws=totals['draws'],decisions=totals['decisions'],guard_fires=totals['guard_fires'],manifest_sha256=sha(Path('tools/edison/heldout/m3a-manifest.json').read_bytes()))
    save('recomputed.json',result);diff=differences(read('tests/edison/reports/m3-stage-a-2026-11-09/stage-a-result.json'),result);save('differences.json',diff)
    print('differences',diff,flush=True)
    extras={'known_mean':kr.mean(axis=0).tolist(),'held_mean':hr.mean(axis=0).tolist(),'non_blackwing':{k:float(np.mean([r[k] for r in per.values() if r['archetype']!='blackwing'])) for k in ('pilot','face','mirror')},'lowest_gaps':sorted(per.items(),key=lambda kv:kv[1]['gap'])[:5], 'guard_share_all':totals['guard_fires']/totals['decisions'],'guard_share_net':totals['guard_fires']/totals['net_decisions']}
    save('extras.json',extras)
if __name__=='__main__':
    if '--statistics-only' in sys.argv:
        z=np.load(OUT/'counts.npz');q=read(OUT/'integrity.json');recount({b:z[b+'_wins'] for b in ('held','known')},{b:z[b+'_games'] for b in ('held','known')},{b:z[b+'_worst'] for b in ('held','known')},q['totals'],q['voids'],q['void_reasons'])
    else:main()
