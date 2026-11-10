"""Read-only provenance and setup checks, with outputs confined to sol-recount."""
import os,sys,json,hashlib,collections as C,sqlite3,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];os.chdir(ROOT);sys.path.insert(0,str(ROOT/'sol-recount'))
from audit import read,save,sha,ydk,signature,counts,training_paths,L,M,H,N,T

def main():
    checked={};bad=[]
    for p,wanted in M['inputs']['files'].items():
        actual=sha(Path(p).read_bytes());checked[p]=actual
        if actual!=wanted:bad.append(p)
    paths={'native':ROOT/'ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so','database':ROOT/'assets/locale/en/cards.cdb'}
    for k,p in paths.items():
        checked[k]=sha(p.read_bytes())
        if checked[k]!=M['inputs'][k]:bad.append(k)
    d=hashlib.sha256()
    for p in sorted((ROOT/'scripts/script').resolve().glob('*.lua')):d.update(p.name.encode()+b'\0'+p.read_bytes())
    checked['lua_scripts']=d.hexdigest()
    if checked['lua_scripts']!=M['inputs']['lua_scripts']:bad.append('lua_scripts')
    for opt in ('1702','1703','1704'):
        for kind,p,wanted in [('checkpoint',ROOT/f'build/edison-retrain-2026-11-04/runs/gauntlet-{opt}/checkpoint-2560.msgpack',M['inputs']['checkpoints'][opt]),('native_net',ROOT/f'build/edison-search/nets/gauntlet-{opt}.ednet',M['search']['nets'][opt])]:
            k=f'{kind}_{opt}';checked[k]=sha(p.read_bytes())
            if checked[k]!=wanted:bad.append(k)
    checked['search_binary']=sha((ROOT/'build/forward_model/edison_search').read_bytes());assert checked['search_binary']==M['search']['binary']
    registry=training_paths()
    for n in ('g_gigavise','g_machina_control'):registry[n]=ROOT/f'build/edison-learner-2026-11-04/decks/{n}.ydk'
    registry.update({n:ROOT/f'build/edison-heldout/decks/{n}.ydk' for n in N})
    for n,p in registry.items():
        checked['deck/'+n]=sha(p.read_bytes())
        if checked['deck/'+n]!=M['inputs']['decks'][n]:bad.append('deck/'+n)
    assert not bad,bad
    save('provenance.json',dict(checked=len(checked),mismatches=bad,hashes=checked,head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),manifest_commit_parent=subprocess.check_output(['git','rev-parse','86ddb40^'],text=True).strip(),manifest_only_diff=subprocess.check_output(['git','diff-tree','--no-commit-id','--name-only','-r','86ddb40'],text=True).splitlines()))
    # Recompute corpus eligibility independently. Trust the frozen production-legal flag,
    # but recompute card coverage, build identity and representative/variant grouping.
    train={n:ydk(p) for n,p in training_paths().items()}
    all_training=set().union(*(set(sum(z.values(),[])) for z in train.values()))
    for z in train.values():z['extra']=[c for c in z['extra'] if c!=52352005]
    played=set().union(*(set(counts(z)) for z in train.values()))
    keys={signature(z) for z in train.values()};codes={int(s.split()[0]) for s in Path('tools/edison/learner/codes_v1.txt').read_text().splitlines()}
    closure=codes-all_training;allowed=played|closure;eligible=[];excluded=C.Counter();groups={};removed=[]
    for row in sorted(read('assets/edison/corpus/manifest.json')['lists'],key=lambda r:int(r['list_id'][2:])):
        z=ydk(ROOT/'assets/edison/corpus'/row['ydk']);drops=z['extra'].count(52352005);z['extra']=[c for c in z['extra'] if c!=52352005];used=set(counts(z));key=signature(z)
        if not row['production_legal']:reason='not production legal'
        elif row['in_training']:reason='a training list'
        elif used-codes:reason='a Main or Extra card outside code list v1'
        elif used-allowed:reason='a Main or Extra card that no training list plays'
        elif key in keys:reason="a training list's exact Main and Extra"
        else:
            eligible.append(row['list_id']);groups.setdefault(key,[]).append(row['list_id'])
            if drops:removed.append(row['list_id'])
            continue
        excluded[reason]+=1
    excluded['the same Main and Extra as a lower-numbered held-out list']=len(eligible)-len(groups)
    actual={tuple(v):'h_'+v[0] for v in groups.values()};listed={tuple(r['variants']):r['name'] for r in H};assert actual==listed
    assert excluded==L['excluded'],(excluded,L['excluded'])
    closure_used={c:[n for n in N if c in counts(ydk(ROOT/f'build/edison-heldout/decks/{n}.ydk'))] for c in closure if any(c in counts(ydk(ROOT/f'build/edison-heldout/decks/{n}.ydk')) for n in N)}
    db=sqlite3.connect('assets/locale/en/cards.cdb');named={str(c):dict(name=db.execute('select name from texts where id=?',(c,)).fetchone()[0],builds=names) for c,names in closure_used.items()}
    save('selection.json',dict(eligible_lists=len(eligible),distinct_builds=len(groups),excluded=excluded,gottoms_removed_lists=removed,closure_only_cards=named,band_archetypes={b:dict(C.Counter(r['archetype'] for r in H if r['band']==b)) for b in ('1-9','10-19','20+')}))
    # Quantify absolute seat zero versus the counted-side conditional rate, and guard counts.
    z=__import__('numpy').load(ROOT/'sol-recount/counts.npz');out={}
    for b in ('known','held'):
        w=z[b+'_wins'][:,:,6];g=z[b+'_games'][:,:,6]
        out[b]=dict(counted_seat0_wins=int(w[:,::2].sum()),counted_seat0_games=int(g[:,::2].sum()),absolute_seat0_wins=int(w[:,::2].sum()+g[:,1::2].sum()-w[:,1::2].sum()),absolute_seat0_games=int(g.sum()))
    save('seat-evidence.json',out)
    print('provenance and selection match;',len(checked),'hashes;',len(eligible),'eligible lists;',len(groups),'builds;',len(removed),'Gottoms removals; closure-only',named,flush=True)
if __name__=='__main__':main()
