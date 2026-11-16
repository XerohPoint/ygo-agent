import pickle, numpy as np, json
R='/home/mada/ygo-agent/build/edison-stage-b-2026-11-12/recount/'
P=pickle.load(open(R+'rows.pkl','rb')); rows=P['rows']
ACTS=("", "Set", "Change position", "Special Summon", "Normal Summon", "Set monster", "Attack", "Direct attack", "Activate", "Cancel")
out={}; log=[]
def pr(*a):
    s=' '.join(str(x) for x in a); print(s); log.append(s)
for blk in ('held','known'):
  for arm in ('pilot','base','face','mirror'):
    tot={'counted':np.zeros((2,10,2),dtype=np.int64),'other':np.zeros((2,10,2),dtype=np.int64)}
    n=0
    for r in rows:
        if r['block']==blk and r['arm']==arm and r['usage'] is not None:
            u=np.array(r['usage']); s=r['seat']
            tot['counted']+=u[s]; tot['other']+=u[1-s]; n+=1
    if n==0: continue
    for side in ('counted','other'):
        if arm=='mirror' and side=='other': pass
        u=tot[side]
        pr(f'--- {blk} {arm} rows={n} side={side} (seat==row seat: network/X-sided; for mirror it is X)')
        for a in (1,2,3,4,5,6,7,8,9):
            k,uk=u[0,a],u[1,a]
            pr(f'  {ACTS[a]:16s} known offered {k[0]:9d} chosen {k[1]:9d} rate {k[1]/max(k[0],1):.5f} | unknown offered {uk[0]:8d} chosen {uk[1]:8d} rate {uk[1]/max(uk[0],1):.5f}')
        out[f'{blk}/{arm}/{side}']=u.tolist()
json.dump(out,open(R+'usage_recount.json','w'))
open(R+'usage_log.txt','w').write('\n'.join(log))
