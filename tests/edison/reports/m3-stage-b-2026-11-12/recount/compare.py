import json, pickle, numpy as np
R='/home/mada/ygo-agent/build/edison-stage-b-2026-11-12/recount/'
O=json.load(open('/home/mada/ygo-agent/build/edison-stage-b-2026-11-12/stage-b-91b27ba9e1d934bc/stage-b-result.json'))
M={eval(k):v for k,v in json.load(open(R+'recount_results.json')).items() if not k.startswith('known')}
M2=json.load(open(R+'recount_results.json'))
N=O['numbers']; tab=[]
def add(name,mine,off): tab.append((name,mine,off,abs(mine-off)))
for arm in('pilot','face'):
    for s,key in (('ALL','all'),('H','heavy')):
        m=M[(arm,s)]; o=N[key][arm]
        add(f'{arm} {s} mean',m['mean'],o['mean'])
        for q,k in enumerate(('1702','1703','1704')):
            add(f'{arm} {s} {k} rate',m['opt'][q],o['by_optimizer'][k]['rate']); add(f'{arm} {s} {k} 2.5th',m['lo'][q],o['by_optimizer'][k]['low'])
        for sd in (0,1): add(f'{arm} {s} seat{sd}',m['seat'][sd],o['by_seat'][str(sd)])
for s,key in (('ALL','all'),('H','heavy')):
    g=M[('gap',s,True)]; add(f'gap {s} point',g['pt'],N[key]['gap']['mean']); add(f'gap {s} 2.5th',g['lo'],N[key]['gap']['low']); add(f'gap {s} 97.5th',g['hi'],N[key]['gap']['high'])
    add(f'gap-no-mirror {s} point',M[('gap',s,False)]['pt'],N[key]['raw_gap']['mean'])
    r=M[('retrain',s)]; add(f'pilot-base {s} mean',r['diff'],N[key]['pilot_minus_base']['mean']); add(f'pilot-base {s} 2.5th',r['lo'],N[key]['pilot_minus_base']['low']); add(f'pilot-base {s} 97.5th',r['hi'],N[key]['pilot_minus_base']['high'])
add('known pilot',M2['known']['p'],N['known']['pilot']); add('known base',M2['known']['b'],N['known']['base']); add('known pilot-base',M2['known']['p']-M2['known']['b'],N['known']['known_minus_base']['mean'])
for a in('mask2','mask5','mask10','maskall'):
    m=M[(a,)]; o=N['known'][a+'_minus_pilot']; add(a+'-pilot mean',m['pt'],o['mean']); add(a+'-pilot 2.5th',m['lo'],o['low']); add(a+'-pilot 97.5th',m['hi'],o['high'])
# usage
U=json.load(open(R+'usage_recount.json')); idx={'Set':1,'Special Summon':3,'Normal Summon':4,'Set a monster':5,'attack':6,'activate':8}
for arm in('pilot','base','face','mirror'):
    for kind in ('activate','Normal Summon','attack'):
        u=np.array(U[f'held/{arm}/counted'])
        for kn,ui in (('known',0),('unknown',1)):
            o=O['usage'][f'held/{arm}'][kind][kn]; m=u[ui,idx[kind]]
            add(f'usage held/{arm} {kind} {kn} rate',m[1]/m[0],o['rate']); 
            assert m[0]==o['offered'] and m[1]==o['chosen'],(arm,kind,kn)
json.dump(tab,open(R+'comparison.json','w'),indent=0)
with open(R+'comparison.txt','w') as f:
    for n,a,b,d in tab: f.write(f'{n:40s} mine {a:.6f}  official {b:.6f}  absdiff {d:.2e}\n')
print(open(R+'comparison.txt').read())
print('max point-estimate diff', max(d for n,a,b,d in tab if '%' not in n and 'th' not in n and 'seat' not in n or True))
print('max interval diff',max(d for n,a,b,d in tab if 'th' in n))
print('max non-interval diff',max(d for n,a,b,d in tab if 'th' not in n))
print(O['draws'],O['void_reasons'],O['broken'], N['voids'].get('held/pilot/1702'))
# held mirror seat0
P=pickle.load(open(R+'rows.pkl','rb'))['rows']
w=g=0
for r in P:
    if r['block']=='held' and r['arm']=='mirror' and r['seat']==0: w+=sum(x==0 for x in r['winners']); g+=len(r['winners'])
print('held mirror seat0',w/g,O['reported']['held_mirror_list_in_seat0'])
w=g=0
for r in P:
    if r['block']=='known' and r['arm']=='mirror': w+=sum(x==r['seat'] for x in r['winners']); g+=len(r['winners'])
print('known mirror',w/g,N['known']['mirror'])
