import gzip, json, glob, pickle, collections, sys
from multiprocessing import Pool
D='/home/mada/ygo-agent/build/edison-stage-b-2026-11-12/stage-b-91b27ba9e1d934bc/'
OUT='/home/mada/ygo-agent/build/edison-stage-b-2026-11-12/recount/'
def one(f):
    d=json.load(gzip.open(f))
    rows=[]; rowkey={}
    for r in d['rows']:
        du=r['duels']
        rows.append(dict(chunk=d['chunk'],cell=r['cell'],block=r['block'],arm=r['arm'],policy=r['policy'],X=r['list'],O=r['opponent'],
            deck0=r['deck0'],deck1=r['deck1'],seat=r['seat'],seed=r['seed'],games=r['games'],players=tuple(r['players']),status=r['status'],
            reason=r['reason'],ndu=len(du),seeds=[x['seed'] for x in du],winners=[x['winner'] for x in du],reasons=[x['win_reason'] for x in du],
            traces=[x['trace'] for x in du],usage=r['counters'].get('usage'),guard=r['counters'].get('guard_fires'),
            wide=r['counters'].get('wide_decisions'),
            unres=(r['unresolved_first'],r['unresolved_replay'],r['unresolved_starts']),hidden=r['hidden_rows']))
    # replay compare
    rowby={}
    for r in d['rows']: rowby.setdefault(r['cell'],r)
    rep=dict(n=len(d['replays']),mismatch=0,missing_cell=0,extra=[])
    for p in d['replays']:
        r=rowby.get(p['cell'])
        if r is None: rep['missing_cell']+=1; continue
        a=[(x['seed'],x['winner'],x['trace']) for x in p['duels']]
        b=[(x['seed'],x['winner'],x['trace']) for x in r['duels']]
        if a!=b or p['status']!=r['status']: rep['mismatch']+=1
    rep['keys']=sorted(d['replays'][0].keys()) if d['replays'] else []
    return rows,rep,d['chunk'],len(d['rows'])
if __name__=='__main__':
    fs=sorted(glob.glob(D+'chunk-*.json.gz'))
    with Pool(8) as p: res=p.map(one,fs)
    rows=[r for x in res for r in x[0]]
    pickle.dump(dict(rows=rows,reps=[x[1] for x in res],chunks=[x[2] for x in res],nrows=[x[3] for x in res],files=fs),open(OUT+'rows.pkl','wb'))
    print(len(fs),len(rows))
