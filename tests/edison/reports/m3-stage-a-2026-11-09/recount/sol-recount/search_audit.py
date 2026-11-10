"""Independently inspect search records and rerun 12 frozen deck pairs in both seats."""
import sys,os,json,random,collections as C,subprocess,concurrent.futures as cf,hashlib
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'sol-recount';os.chdir(ROOT);sys.path.insert(0,str(OUT))
from audit import read,save,sha,differences,L,M,T,N,H,ydk,signature
BASE=ROOT/'build/edison-heldout/search-check'

def clean(x):
    if isinstance(x,dict):return {k:clean(v) for k,v in x.items() if not k.endswith('_cpu')}
    if isinstance(x,list):return [clean(v) for v in x]
    return x

def inspect():
    r=random.Random(88810002);draw=r.sample(N,60);pairs=[(h,o) for h in draw for o in r.sample(T,4)]
    lines={};records={};group_for={};paths={};all_hashes=[]
    for gi in range(11):
        tag=f'{gi:02}';reg={}
        for line in (BASE/f'registry-{tag}.txt').read_text().splitlines():
            n,p=line.split(' ',1);q=ROOT/Path(p).relative_to('/home/mada/ygo-agent/.claude/worktrees/m3-corpus');assert q.is_file();reg[n]=q
        assert set(reg)==set(M['search']['groups'][gi]) and len(reg)==21
        (OUT/f'registry-{tag}.txt').write_text(''.join(f'{n} {p}\n' for n,p in reg.items()))
        inputs=read(BASE/f'group-{tag}.inputs');d=hashlib.sha256()
        for n,p in sorted(reg.items()):d.update(n.encode()+b'\0'+p.read_bytes())
        assert d.hexdigest()==inputs['lists'];all_hashes.append(d.hexdigest())
        for line in (BASE/f'group-{tag}.tsv').read_text().splitlines():
            f=line.split('\t');key=f[0];assert key not in lines;lines[key]=f;group_for[key]=gi
            typ=key[0];pair_id,seat=map(int,key[1:].split('_'));h,o=pairs[pair_id];net=f'n{1702+pair_id%3}';d0,d1=(h,o) if seat==0 else (o,h)
            expected=[key,d0,d1,str(88800000+pair_id),str(seat if typ=='g' else -1),net,*(['64','48','4','1.0','1',str(5000+2*pair_id+seat),'0',net] if typ=='g' else ['0','0','0','0.0','1','0','0',net])]
            assert f==expected,(key,f,expected)
            p=BASE/f'group-{tag}'/f'{key}.json';g=read(p);records[key]=g
            assert g['id']==key and g['job']==' '.join(f) and g['binary']=='d35e8512ba2cce6f'
            if g['status']=='OK':assert g['winner'] in (0,1,2)
            paths[key]=str(p.relative_to(ROOT))
        a=[json.loads(x) for x in (BASE/f'group-{tag}.attempts').read_text().splitlines()];assert len(a)==2 and a[0]['event']=='start' and a[1]['event']=='end' and a[1]['returncode']==0 and a[1]['counts']['worker_failures']==0
    assert len(lines)==960
    # Frozen search job hash serializes typed arguments before optional defaults
    # are expanded into the TSV: 13 arguments for search and 6 for plain.
    frozen=[]
    for typ in ('g','p'):
        arm=[]
        for i in range(240):
            for seat in (0,1):
                f=lines[f'{typ}{i}_{seat}'];v=[f[0],f[1],f[2],int(f[3]),int(f[4]),f[5]]
                if typ=='g':v += [int(f[6]),int(f[7]),int(f[8]),float(f[9]),int(f[10]),int(f[11]),int(f[12])]
                arm.append(v)
        frozen.append(arm)
    jobs_hash=sha(json.dumps(frozen).encode());assert jobs_hash==M['search']['jobs']
    save('search-design.json',dict(jobs_sha256=jobs_hash,tsv_jobs=len(lines),pairs=len(pairs),groups=11,registry_hashes=all_hashes))
    kept=[];missing=C.Counter();void=C.Counter();reasons=C.Counter()
    far={h['name'] for h in H if h['far']}
    for i,(h,o) in enumerate(pairs):
        for seat in (0,1):
            key=f'{i}_{seat}';gs=records.get('g'+key);gp=records.get('p'+key)
            for name,g in [('search',gs),('plain',gp)]:
                if g is None:missing[name]+=1
                elif g['status']!='OK':void[name]+=1;reasons[name+'/'+g.get('reason','')]+=1
            if gs and gp and gs['status']==gp['status']=='OK':
                score=lambda g:1 if g['winner']==seat else (.5 if g['winner']==2 else 0)
                kept.append(dict(build=h,seat=seat,far=h in far,search=score(gs),plain=score(gp)))
    rng=np.random.default_rng(88810003)
    def summary(rows):
        names=sorted({r['build'] for r in rows});groups={n:[r for r in rows if r['build']==n] for n in names};n=len(rows)
        a=np.array([[sum(r['search'] for r in groups[x]),sum(r['plain'] for r in groups[x]),len(groups[x])] for x in names]);pick=rng.integers(0,len(names),size=(10000,len(names)));s=a[pick].sum(axis=1);bd=(s[:,0]-s[:,1])/s[:,2]
        return dict(comparisons=n,builds=len(names),search=sum(r['search'] for r in rows)/n,plain=sum(r['plain'] for r in rows)/n,difference=sum(r['search']-r['plain'] for r in rows)/n,low=float(np.quantile(bd,.025)),high=float(np.quantile(bd,.975)))
    summary_all=dict(pairs=240,comparisons_planned=480,missing=dict(missing),void=dict(void),void_reasons=dict(reasons),all=summary(kept),by_seat={str(seat):summary([r for r in kept if r['seat']==seat]) for seat in (0,1)},far=summary([r for r in kept if r['far']]))
    native=[v for k,v in records.items() if k[0]=='g'];stats={k:sum(g.get(k,0) for g in native) for k in ('searched','departed','fell_back','deals_ok','deals_refused','rollouts','worlds_void','truncated','net_errors','net_calls','option_mismatch','step_overruns','rollout_guard_fires')};stats.update(draws=sum(g['winner']==2 for g in native),cpu_per_game=sum(g['total_cpu'] for g in native)/480,departed_share=stats['departed']/stats['searched'],fallback_share=stats['fell_back']/stats['searched'])
    n=len(native); score=sum(1 if g['winner']==g['search_seat'] else (.5 if g['winner']==2 else 0) for g in native)/n
    z=1.96; den=1+z*z/n; center=(score+z*z/(2*n))/den; radius=z*((score*(1-score)/n+z*z/(4*n*n))**.5)/den
    sg=dict(games=n,pairs=len(kept),void=sum(g['status']!='OK' for g in native),draws=stats['draws'],search_score=round(score*100,2),low=round(100*(center-radius),2),high=round(100*(center+radius),2))
    for k in ('searched','departed','fell_back','deals_ok','deals_refused','rollouts','worlds_void','truncated','net_errors','net_calls','option_mismatch','step_overruns','rollout_guard_fires'):sg[k]=stats[k]
    sg['departed_share']=round(stats['departed_share'],4);sg['cpu_per_game']=round(stats['cpu_per_game'],1);sg['cpu_per_search_ms']=round(1000*sum(g['search_cpu'] for g in native)/stats['searched'],1)
    assert all(g['deals_refused']==8*g['fell_back'] for g in native)
    summary_all['search_games']=sg
    summary_all['native_totals']=stats;summary_all['world_void_games']={k:v for k,v in records.items() if k[0]=='g' and v.get('worlds_void')}
    save('search-recomputed.json',summary_all)
    reported=read('tests/edison/reports/m3-stage-a-2026-11-09/search-check-result.json');delta=differences({k:v for k,v in reported.items() if k!='note'},{k:v for k,v in summary_all.items() if k not in ('native_totals','world_void_games')});save('search-differences.json',delta)
    rng=random.Random(20261111);selected=[]
    for gi in range(11):
        ids=sorted({int(k[1:].split('_')[0]) for k in lines if k[0]=='g' and group_for[k]==gi});selected.append(rng.choice(ids))
    if 31 not in selected:selected.append(31) # diagnostic with the 24 voided sampled worlds
    else:selected.append(rng.choice([i for i in range(240) if i not in selected]))
    save('selected-search.json',dict(pair_ids=selected,records={k:paths[k] for i in selected for s in (0,1) for k in (f'g{i}_{s}',f'p{i}_{s}')}))
    return lines,records,group_for,selected

def run_group(gi,ids,lines,records):
    keys=[f'{a}{i}_{s}' for i in ids for s in (0,1) for a in ('g','p')];tag=f'{gi:02}';target=OUT/f'search-group-{tag}';target.mkdir(exist_ok=True);tsv=OUT/f'search-group-{tag}.tsv';tsv.write_text(''.join('\t'.join(lines[k])+'\n' for k in keys))
    args=[str(ROOT/'build/forward_model/edison_search'),'play','--root',str(ROOT),'--registry',str(OUT/f'registry-{tag}.txt')]
    for opt in ('1702','1703','1704'):args+=['--net',f'n{opt}={ROOT}/build/edison-search/nets/gauntlet-{opt}.ednet={M["inputs"]["checkpoints"][opt]}']
    args+=['--codes-sha',sha((ROOT/'tools/edison/learner/codes_v1.txt').read_bytes()),'--jobs',str(tsv),'--out',str(target),'--workers','1','--timeout','3600']
    with (OUT/f'search-group-{tag}.log').open('w') as log:done=subprocess.run(args,cwd=ROOT/'scripts',stdout=log,stderr=subprocess.STDOUT)
    assert done.returncode==0,(gi,done.returncode)
    results=[]
    for k in keys:
        actual=read(target/f'{k}.json');results.append(dict(id=k,group=gi,differences=differences(clean(records[k]),clean(actual))))
    print('search group',gi,'games',len(keys),'differed',sum(bool(r['differences']) for r in results),flush=True);return results
if __name__=='__main__':
    lines,records,group_for,ids=inspect()
    if '--inspect-only' not in sys.argv:
        results=[]
        # Three binaries with one worker each: at most 7 Python/native processes.
        with cf.ThreadPoolExecutor(max_workers=3) as pool:
            fs=[pool.submit(run_group,g,[i for i in ids if group_for[f'g{i}_0']==g],lines,records) for g in range(11)]
            for f in cf.as_completed(fs):results+=f.result();save('search-rerun-progress.json',results)
        results.sort(key=lambda r:(r['group'],r['id']));save('search-reruns.json',dict(deck_pairs=len(ids),seat_comparisons=2*len(ids),games=len(results),different_games=sum(bool(r['differences']) for r in results),results=results))
