import pickle, json, collections, numpy as np, sys
R='/home/mada/ygo-agent/build/edison-stage-b-2026-11-12/recount/'
L=json.load(open('/home/mada/ygo-agent/.claude/worktrees/edison-tech-lead-handoff-a8ba76/tools/edison/heldout/stage-b-lists.json'))
P=pickle.load(open(R+'rows.pkl','rb')); rows=P['rows']
out={}; log=[]
def pr(*a):
    s=' '.join(str(x) for x in a); print(s); log.append(s)
lists=L['lists']; train=sorted(L['training_lists'])
builds=sorted(b['name'] for b in lists)
assert len(builds)==747 and len(train)==21
bi={n:i for i,n in enumerate(builds)}; ti={n:i for i,n in enumerate(train)}
meta={b['name']:b for b in lists}
twin=np.array([ti[meta[n]['twin']] for n in builds])
heavy=np.array([meta[n]['unknown_main_copies']>=6 for n in builds])
pr('heavy H size',heavy.sum(), 'twin counts',collections.Counter(meta[n]['twin'] for n in builds).most_common(3))
OPT=['1702','1703','1704']
# ---- integrity ----
pr('chunks',len(P['chunks']),'rows',len(rows),'games',sum(r['games'] for r in rows))
pr('status',collections.Counter(r['status'] for r in rows))
pr('winner values',collections.Counter(w for r in rows for w in r['winners']))
pr('win_reason',collections.Counter(x for r in rows for x in r['reasons']))
pr('duels!=games',sum(r['ndu']!=r['games'] for r in rows),'unresolved',sum(any(r['unres']) for r in rows),'reason set',sum(r['reason'] is not None for r in rows))
pr('replays total',sum(x['n'] for x in P['reps']),'replay mismatches (status,seed,winner,trace)',sum(x['mismatch'] for x in P['reps']),'replay with no cell',sum(x['missing_cell'] for x in P['reps']))
cells=sorted(r['cell'] for r in rows); pr('cell ids unique & contiguous',cells==list(range(len(cells))))
# convention checks
bad=0
for r in rows:
    d=(r['deck0'],r['deck1']); s=r['seat']
    if r['arm'] in('pilot','base','mirror') or r['arm'].startswith('mask'):
        ok = d[s]==r['X'] and d[1-s]==r['O']
    else: # face: network pilots O at index seat
        ok = d[s]==r['O'] and d[1-s]==r['X']
    pl=r['players']
    if r['arm']=='mirror': ok &= pl==('heuristic','heuristic')
    else: ok &= pl[1-s]=='heuristic' and pl[s]!='heuristic'
    bad+=not ok
pr('rows violating (network/X at index==seat) deck-player convention',bad)
pr('player labels',collections.Counter((r['arm'],r['players'][r['seat']]) for r in rows if r['arm'] in('pilot','base','face','mask5','mirror')))
# seeds
sbad=0
for r in rows:
    if r['block']=='known':
        i=ti[r['X']]; j=ti[r['O']]; base=90200000+32*(21*i+j)
    else:
        i=bi[r['X']]; j=ti[r['O']]; base=90000000+4*(21*i+j)
    sbad+= not (r['seed']==base and r['seeds']==[base+k for k in range(r['games'])])
pr('rows whose seeds differ from protocol formula (sorted-name order)',sbad)
rep_cells=set(); 
# coverage
exp_arms_known=['pilot','face','base','mask2','mask5','mask10','maskall']
cov=collections.Counter((r['block'],r['arm'],r['policy'],r['X'],r['O'],r['seat']) for r in rows if r['status']=='PASS' and r['ndu']>0)
pr('duplicate cell keys',sum(v>1 for v in cov.values()))
miss=0
for blk,xs,arms in (('held',builds,['pilot','face','base']),('known',train,exp_arms_known)):
    for x in xs:
        for o in train:
            for s in (0,1):
                for a in arms:
                    for k in OPT:
                        miss+=(blk,a,k,x,o,s) not in cov
                miss+=(blk,'mirror','heuristic',x,o,s) not in cov
pr('missing (build,arm,optimizer,opp,seat) cells',miss)
# ---- arrays ----
# win: winner==seat ; games ; draws
def arr(block,arm,pol,xs):
    W=np.zeros((len(xs),42)); G=np.zeros((len(xs),42)); Dr=np.zeros((len(xs),42)); N1=np.zeros((len(xs),42))
    return W,G,Dr,N1
xi={'held':bi,'known':ti}
store={}
for r in rows:
    key=(r['block'],r['arm'],r['policy'])
    if key not in store:
        n=747 if r['block']=='held' else 21
        store[key]=[np.zeros((n,42)) for _ in range(4)]
    W,G,Dr,N1=store[key]; i=xi[r['block']][r['X']]; c=ti[r['O']]*2+r['seat']
    w=np.array(r['winners']); s=r['seat']
    assert G[i,c]==0
    W[i,c]=(w==s).sum(); G[i,c]=len(w); Dr[i,c]=(w==2).sum(); N1[i,c]=((w==s)&(np.array(r['reasons'])==-1)).sum()
pr('draws by arm',{k:int(v[2].sum()) for k,v in store.items() if v[2].sum()>0})
pr('games per (block,arm,policy) min/max per cell',{k:(int(v[1].min()),int(v[1].max())) for k,v in list(store.items())[:3]})
# ---- convention checks ----
# (a) mirror X==O : both rows share seed & decks => identical duels, pooled rate must be exactly 0.5
chk=[]
for t in train:
    a=[r for r in rows if r['block']=='known' and r['arm']=='mirror' and r['X']==t and r['O']==t]
    assert len(a)==2 and a[0]['traces']==a[1]['traces'] and a[0]['winners']==a[1]['winners']
    w0=sum(w==0 for w in a[0]['winners'])+sum(w==1 for w in a[1]['winners']); g=sum(len(r['winners']) for r in a); chk.append((w0,g))
pr('mirror X==O: duels identical in both seat rows for all 21 lists; pooled counted-side wins/games',sum(x[0] for x in chk),sum(x[1] for x in chk),'(winner==seat counted, draws none)')
# (b) mirror known pairs: m(i,j)+m(j,i) ~ 1
Mk=store[('known','mirror','heuristic')]; rate=Mk[0].reshape(21,21,2).sum(2)/Mk[1].reshape(21,21,2).sum(2)
off=[rate[i,j]+rate[j,i] for i in range(21) for j in range(i+1,21)]
pr('mirror known: mean over pairs i<j of rate(i vs j)+rate(j vs i) =',np.mean(off),'(complement convention would also give 1; winner!=seat would give',np.mean([2-x for x in off]),')')
# (c) pilot rate with winner==seat vs complement, per seat
Pk=store[('known','pilot','1702')]
pr('known pilot 1702 network win rate (winner==seat):',Pk[0].sum()/Pk[1].sum(),' complement 1-rate-draws:',1-Pk[0].sum()/Pk[1].sum())
# network is at players index seat; also check if winner==1-seat for network cells equals heuristic-win.
pr('pilot known X==O (network piloting list against the same list), counted rate 1702/1703/1704',[ (store[('known','pilot',k)][0].reshape(21,21,2)[[i for i in range(21)],[i for i in range(21)]].sum()/store[('known','pilot',k)][1].reshape(21,21,2)[[i for i in range(21)],[i for i in range(21)]].sum()) for k in OPT])
# seat-0 mirror known
pr('mirror known seat-0 (index0) win rate: ',(Mk[0].reshape(21,21,2)[:,:,0].sum()/Mk[1].reshape(21,21,2)[:,:,0].sum()))
# win_reason -1 share
for arm in ('pilot','face','base','mirror'):
    n1=sum(1 for r in rows if r['block']=='held' and r['arm']==arm for x in r['reasons'] if x==-1); g=sum(r['games'] for r in rows if r['block']=='held' and r['arm']==arm)
    pr('held',arm,'games with win_reason -1:',n1,'of',g)
# ---- rates ----
def rate(block,arm,pol,mask=None,seat=None):
    W,G,_,_=store[(block,arm,pol)]
    if mask is not None: W,G=W[mask],G[mask]
    if seat is not None: W,G=W[:,seat::2],G[:,seat::2]
    return W.sum()/G.sum()
allm=np.ones(747,bool)
res={}
rng=np.random.default_rng(90810001)
ND=10000
def boot_idx(n,rng,nd):
    return rng.integers(0,n,size=(nd,n))
def boot_rates(arr_list, mask, rng, nd=ND, chunk=250):
    """arr_list: list of (747,42) per-cell rate arrays. Returns array (nd, len) of mean over drawn builds of mean over resampled cells."""
    idx=np.where(mask)[0]; n=len(idx)
    out=np.zeros((nd,len(arr_list)))
    for s in range(0,nd,chunk):
        m=min(chunk,nd-s)
        bsel=idx[rng.integers(0,n,size=(m,n))]            # (m,n)
        csel=rng.integers(0,42,size=(m,n,42))
        for q,A in enumerate(arr_list):
            v=A[bsel[:,:,None],csel]                      # (m,n,42)
            out[s:s+m,q]=v.mean(2).mean(1)
    return out
cellrate={k:v[0]/v[1] for k,v in store.items()}
def pct(x,q): return float(np.percentile(x,q))
for setname,mask in (('ALL',allm),('H',heavy)):
    for arm in ('pilot','face'):
        pr('=== B1/B2',arm,setname,'builds',mask.sum())
        rs=[rate('held',arm,k,mask) for k in OPT]
        bs=boot_rates([cellrate[('held',arm,k)] for k in OPT],mask,rng)
        lo=[pct(bs[:,q],2.5) for q in range(3)]
        seat=[ (sum(store[('held',arm,k)][0][mask][:,s::2].sum() for k in OPT)/sum(store[('held',arm,k)][1][mask][:,s::2].sum() for k in OPT)) for s in (0,1)]
        mean=float(np.mean(rs))
        bm=bs.mean(1)
        res[(arm,setname)]=dict(mean=mean,opt=rs,lo=lo,seat=seat,mean_lo=pct(bm,2.5))
        pr(' mean',mean,'opt rates',rs,'opt 2.5th',lo,'seat0/seat1',seat,'mean-of-opts 2.5th',pct(bm,2.5))
        ok= mean>=.55 and all(l>.5 for l in lo) and all(s>=.5 for s in seat)
        pr(' PASS' if ok else ' FAIL')
        res[(arm,setname)]['ok']=ok
# ---- gap ----
KP={k:cellrate[('known','pilot',k)] for k in OPT}; KM=cellrate[('known','mirror','heuristic')]
HP={k:cellrate[('held','pilot',k)] for k in OPT}; HM=cellrate[('held','mirror','heuristic')]
def gap_point(mask,mirror=True):
    # per build: mean over opts of (pX - mX) - (pT - mT), p/m = mean over 42 cells
    g=np.zeros(747)
    for k in OPT:
        pX=HP[k].mean(1); pT=KP[k].mean(1)[twin]
        if mirror:
            g+= (pX-HM.mean(1))-(pT-KM.mean(1)[twin])
        else: g+= pX-pT
    g/=3
    return g[mask].mean(), g
def boot_gap(mask,rng,mirror=True,nd=ND,chunk=250):
    idx=np.where(mask)[0]; n=len(idx); out=np.zeros(nd)
    Kp=np.stack([KP[k] for k in OPT]); Hp=np.stack([HP[k] for k in OPT])   # (3,*,42)
    for s in range(0,nd,chunk):
        m=min(chunk,nd-s)
        bsel=idx[rng.integers(0,n,size=(m,n))]
        csel=rng.integers(0,42,size=(m,n,42))
        tsel=rng.integers(0,42,size=(m,21,42))       # one resample per training list per draw
        # X side
        xp=np.stack([ (Hp[q][bsel[:,:,None],csel]).mean(2) for q in range(3)])  # (3,m,n)
        xm=HM[bsel[:,:,None],csel].mean(2)                                     # (m,n)
        # T side
        tp=np.stack([ np.take_along_axis(Kp[q][None].repeat(m,0),tsel,2).mean(2) for q in range(3)])  # (3,m,21)
        tm=np.take_along_axis(KM[None].repeat(m,0),tsel,2).mean(2)  # (m,21)
        tw=twin[bsel]                                                           # (m,n)
        mi=np.arange(m)[:,None]
        tpb=tp[:,mi,tw]; tmb=tm[mi,tw]
        if mirror: g=((xp-xm[None])-(tpb-tmb[None])).mean(0)
        else: g=(xp-tpb).mean(0)
        out[s:s+m]=g.mean(1)
    return out
for setname,mask in (('ALL',allm),('H',heavy)):
    for mir in (True,False):
        pt,_=gap_point(mask,mir); bs=boot_gap(mask,rng,mir)
        pr('=== B3 gap',setname,'with mirror' if mir else 'WITHOUT mirror','point',pt,'2.5th',pct(bs,2.5),'97.5th',pct(bs,97.5))
        res[('gap',setname,mir)]=dict(pt=pt,lo=pct(bs,2.5),hi=pct(bs,97.5))
# ---- readings: pilot - base ----
for setname,mask in (('ALL',allm),('H',heavy)):
    d=[ (rate('held','pilot',k,mask)-rate('held','base',k,mask)) for k in OPT]
    A=[cellrate[('held','pilot',k)]-cellrate[('held','base',k)] for k in OPT]
    bs=boot_rates([sum(A)/3],mask,rng)[:,0]
    pr('=== retraining',setname,'pilot',np.mean([rate('held','pilot',k,mask) for k in OPT]),'base',np.mean([rate('held','base',k,mask) for k in OPT]),'diff',np.mean(d),'per-opt',d,'2.5th',pct(bs,2.5),'97.5th',pct(bs,97.5))
    res[('retrain',setname)]=dict(diff=float(np.mean(d)),lo=pct(bs,2.5),hi=pct(bs,97.5))
kp=np.mean([rate('known','pilot',k) for k in OPT]); kb=np.mean([rate('known','base',k) for k in OPT])
pr('=== known block pilot',kp,'base',kb,'diff',kp-kb,'per-opt pilot',[rate('known','pilot',k) for k in OPT],'base',[rate('known','base',k) for k in OPT])
res['known']=dict(p=kp,b=kb)
# mask minus pilot, known block: resample 42 cells within each of 21 lists
def boot_known(diffcell,rng,nd=ND):
    out=np.zeros(nd)
    for s in range(0,nd,500):
        m=min(500,nd-s); csel=rng.integers(0,42,size=(m,21,42))
        v=np.take_along_axis(diffcell[None].repeat(m,0),csel,2); out[s:s+m]=v.mean(2).mean(1)
    return out
for a in ('mask2','mask5','mask10','maskall'):
    dc=sum(cellrate[('known',a,k)]-cellrate[('known','pilot',k)] for k in OPT)/3
    bs=boot_known(dc,rng)
    pr('=== ',a,'-pilot known: point',dc.mean(),'2.5th',pct(bs,2.5),'97.5th',pct(bs,97.5),' mask rate',np.mean([rate('known',a,k) for k in OPT]))
    res[(a,)]=dict(pt=float(dc.mean()),lo=pct(bs,2.5),hi=pct(bs,97.5))
# verdict
v=all(res[(a,s)]['ok'] for a in('pilot','face') for s in('ALL','H')) and all(res[('gap',s,True)]['lo']>-.10 for s in ('ALL','H'))
pr('VERDICT (B1,B2,B3 only; B4 handled separately):','STAGE_B_MET' if v else 'NOT_MET')
pr('retraining ALL', 'HELPED' if res[('retrain','ALL')]['diff']>=.03 and res[('retrain','ALL')]['lo']>0 else 'NOT_SHOWN','H','HELPED' if res[('retrain','H')]['diff']>=.03 and res[('retrain','H')]['lo']>0 else 'NOT_SHOWN')
pr('known cards','KEPT' if kp>=kb-.05 else 'LOST')
pr('hidden','SMALL_COST' if res[('mask5',)]['lo']>-.10 else 'LARGE_COST')
# B4 void-as-loss
pr('B4: void games 0; void/interrupted-as-loss means equal the plain means (no void, no interrupted rows)')
# alt: draw excluded? alt: win_reason -1 games not counted as wins
for arm in('pilot','face'):
    for setname,mask in (('ALL',allm),('H',heavy)):
        alt=np.mean([ (store[('held',arm,k)][0]-store[('held',arm,k)][3])[mask].sum()/store[('held',arm,k)][1][mask].sum() for k in OPT])
        pr('ALT (win_reason -1 games count as non-wins)',arm,setname,alt)
json.dump({str(k):v for k,v in res.items()},open(R+'recount_results.json','w'),indent=1,default=float)
open(R+'recount_log.txt','w').write('\n'.join(log))
