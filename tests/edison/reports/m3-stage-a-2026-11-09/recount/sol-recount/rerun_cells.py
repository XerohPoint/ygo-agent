"""Rerun only RNG-selected recorded Stage A cells; 1 and 4 env threads."""
import os,sys,json,concurrent.futures as cf,multiprocessing as mp
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'sol-recount';os.chdir(ROOT)
os.environ.update(OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1',PYTHONDONTWRITEBYTECODE='1',JAX_PLATFORMS='cpu',XLA_FLAGS='--xla_cpu_multi_thread_eigen=false intra_op_parallelism_threads=1',TF_CPP_MIN_LOG_LEVEL='2')
sys.path.insert(0,str(OUT));from audit import training_paths,ydk,differences,read,save

def init():
    global runtime
    log=open(OUT/f'cell-worker-{os.getpid()}.log','a');os.dup2(log.fileno(),1);os.dup2(log.fileno(),2)
    sys.path[:0]=[str(ROOT/'tools/edison/learner'),str(ROOT/'ygoenv'),str(ROOT)]
    import decks
    reg={k:str(v) for k,v in training_paths().items()}
    # Use independent local copies only for training lists requiring filtering.
    for name in reg:
        p=OUT/'training-decks'/f'{name}.ydk'
        if p.exists():reg[name]=str(p)
    decks.registry=lambda:reg
    os.environ['EDISON_EXTRA_DECKS']=str(ROOT/'build/edison-heldout/extra-decks.json')
    os.chdir(ROOT/'scripts')
    from actor import Runtime
    runtime=Runtime()
def clean(x):
    if isinstance(x,dict):return {k:clean(v) for k,v in x.items() if k!='elapsed' and not k.endswith('_seconds')}
    if isinstance(x,list):return [clean(v) for v in x]
    return x

def play(arg):
    record,threads=arg;j=dict(record['job']);j['threads']=threads
    for p in j['policies']:
        if p['kind']=='net':
            optimizer=record['job']['policy'];p['checkpoint']=str(ROOT/f'build/edison-retrain-2026-11-04/runs/gauntlet-{optimizer}/checkpoint-2560')
    actual=runtime.run_cell(j);expected=record['journal']
    result=dict(cell=record['record']['cell'],threads=threads,differences=differences(clean(expected),clean(actual)),actual=actual)
    (OUT/f"cell-{result['cell']}-{threads}.json").write_text(json.dumps(result,indent=1)+'\n')
    return {k:v for k,v in result.items() if k!='actual'}
if __name__=='__main__':
    d=OUT/'training-decks';d.mkdir(exist_ok=True)
    for n,p in training_paths().items():
        z=ydk(p)
        if 52352005 in z['extra']:
            z['extra']=[c for c in z['extra'] if c!=52352005]
            (d/f'{n}.ydk').write_text('#main\n'+'\n'.join(map(str,z['main']))+'\n#extra\n'+'\n'.join(map(str,z['extra']))+'\n!side\n'+'\n'.join(map(str,z['side']))+'\n')
    samples=read(OUT/'selected-cells.json'); tasks=[(r,t) for r in samples for t in (1,4)];results=[]
    with cf.ProcessPoolExecutor(max_workers=4,mp_context=mp.get_context('spawn'),initializer=init) as pool:
        futures=[pool.submit(play,t) for t in tasks]
        for f in cf.as_completed(futures):
            r=f.result();results.append(r);print(r,flush=True);save('cell-rerun-progress.json',results)
    results.sort(key=lambda r:(r['cell'],r['threads']));save('cell-reruns.json',dict(cells=len(samples),runs=len(results),games=sum(r['job']['games']*2 for r in samples),different_runs=sum(bool(r['differences']) for r in results),results=results))
