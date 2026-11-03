"""Small CPU PPO actor/critic using only audited public observations.

Schema v1: the fixed 64 public features plus 64 context features. The heuristic
scores and imitation labels are never used by the learner. Its initial actor
is uniform over the exact offered legal actions.
"""
import json
from pathlib import Path
import numpy as np
from ygoai.edison_policy import Heuristic, power, u16

FEATURES=128
HIDDEN=32
INPUT_MODES=("full","context_masked_v1")
# Only decision/selection-derived inputs, including their interactions.
# Retain independent public features 0:64, 112:121, and 125:128.
CONTEXT_COLUMNS=tuple(range(64,112))+tuple(range(121,125))
FORMAT="edison_context_ppo_v1"
MASKED_FORMAT="edison_context_masked_ppo_v1"
HINTS=(0,500,501,502,503,504,505,506,507,508,509,510,511,512,513,520,528,531,533,549,551,560)

class ContextEncoder(Heuristic):
    def encode(self,cards,glob,actions,decision,selection):
        if int(decision[0])!=1 or len(actions)==0:
            raise ValueError("Expected schema-1 live observation and legal actions")
        # Retain only the public feature vector, never the heuristic score/label.
        _,base=self.analyze(cards,glob,actions)
        x=np.zeros((len(actions),FEATURES),dtype=np.float32)
        x[:,:64]=base
        hint,stage,source=map(int,(decision[1],decision[2],decision[3]))
        x[:,64+(HINTS.index(hint) if hint in HINTS else 0)]=1
        x[:,86+stage]=1
        src=cards[source-1] if source else np.zeros(41,dtype=np.uint8)
        if source and not u16(src,0):
            raise ValueError("Private source reference")
        chosen=selection[:,1].astype(bool)
        chosen_own=cards[chosen & (cards[:,4]==0)]
        chosen_opp=cards[chosen & (cards[:,4]==1)]
        for i,a in enumerate(actions):
            row=int(a[0])-1
            c=cards[row] if a[0] else np.zeros(41,dtype=np.uint8)
            flags=selection[row] if a[0] else np.zeros(3,dtype=np.uint8)
            enemy=float(c[4])
            atk,defense=power(c),power(c,14)
            target=power(c,12 if int(c[5])==1 else 14) if u16(c,0) else 1900
            margin=(power(src)-target)/3000 if source and a[0] else 0
            value=max(atk,defense)/3000
            x[i,90:]=[
                decision[11],bool(source),power(src)/3000,power(src,14)/3000,
                decision[4]/5,decision[5]/5,decision[6]/5,decision[7]/5,
                decision[8],decision[9],decision[10]/12,
                *flags,
                sum(power(v) for v in chosen_own)/9000,
                sum(power(v) for v in chosen_opp)/9000,
                margin,float(hint==549)*margin,
                float(stage==1)*value,float(stage==1)*enemy,
                float(stage==2)*value,float(stage==3)*value,
                u16(glob,0)/8000,u16(glob,2)/8000,int(glob[4])/16,
                int(glob[7]),int(glob[5])/9,bool(u16(c,0)),
                int(c[9])/12,int(c[11]),int(c[5])/8,
                float(hint==501)*value,float(hint==506)*value,
                float(hint in (502,503,505,507))*value*enemy,
                float(a[5])*decision[6]/5,
                float(a[4]==9),bool(a[0]),1]
        return np.clip(x,-4,4)

class RLModel:
    shapes={"w":(FEATURES,HIDDEN),"b":(HIDDEN,),"out":(HIDDEN,),
            "v":(FEATURES,),"vb":(1,)}
    def __init__(self,seed=1701,params=None,input_mode="full"):
        if input_mode not in INPUT_MODES: raise ValueError("Unknown RL input mode")
        self.input_mode=input_mode
        rng=np.random.default_rng(seed)
        self.p=({"w":rng.normal(0,1/np.sqrt(FEATURES),(FEATURES,HIDDEN)),
                 "b":np.zeros(HIDDEN),"out":np.zeros(HIDDEN),
                 "v":np.zeros(FEATURES),"vb":np.zeros(1)}
                if params is None else {k:np.asarray(v,dtype=np.float64) for k,v in params.items()})
        if set(self.p)!=set(self.shapes):
            raise ValueError("Wrong model parameters")
        for k,shape in self.shapes.items():
            if self.p[k].shape!=shape or not np.isfinite(self.p[k]).all():
                raise ValueError("Invalid model parameter: "+k)

    def mask_features(self,x):
        if self.input_mode=="full": return x
        x=x.copy();x[...,list(CONTEXT_COLUMNS)]=0
        return x

    def forward(self,x):
        x=self.mask_features(x)
        h=np.tanh(x@self.p["w"]+self.p["b"])
        return h@self.p["out"],h

    def value(self,state):
        return np.tanh(self.mask_features(state)@self.p["v"]+self.p["vb"][0])

    def save(self,path,metadata=None):
        data={"format":FORMAT,"features":FEATURES,
            "observation_schema":1,"params":{k:v.tolist() for k,v in self.p.items()},
            "metadata":metadata or {}}
        # Preserve legacy full-context checkpoint bytes and default behavior.
        if self.input_mode!="full":
            data["format"]=MASKED_FORMAT
            data["input_mode"]=self.input_mode
        Path(path).write_text(json.dumps(data,separators=(",",":"))+"\n")

    @classmethod
    def load(cls,path):
        data=json.loads(Path(path).read_text())
        mode=data.get("input_mode","full")
        expected=FORMAT if mode=="full" else MASKED_FORMAT
        if data.get("format")!=expected or data.get("features")!=FEATURES or data.get("observation_schema")!=1:
            raise ValueError("Incompatible RL checkpoint")
        return cls(params=data["params"],input_mode=data.get("input_mode","full"))

def distribution(logits,offsets):
    """Segmented softmax: no padding and no probability on unoffered actions."""
    sizes=np.diff(offsets)
    if np.any(sizes<=0): raise ValueError("Empty legal-action segment")
    groups=np.repeat(np.arange(len(sizes)),sizes)
    shifted=logits-np.maximum.reduceat(logits,offsets[:-1])[groups]
    exp=np.exp(shifted)
    logp=shifted-np.log(np.add.reduceat(exp,offsets[:-1]))[groups]
    return np.exp(logp),logp,groups

class RLAgent:
    def __init__(self,metadata,seed,model):
        self.encoder=ContextEncoder(metadata,seed)
        self.model=model
        self.rng=np.random.default_rng(seed^0x50504F)

    def choose(self,cards,glob,actions,decision,selection):
        x=self.model.mask_features(self.encoder.encode(cards,glob,actions,decision,selection))
        logits,_=self.model.forward(x)
        probs,logp,_=distribution(logits,np.array([0,len(x)]))
        choice=int(self.rng.choice(len(x),p=probs))
        value=float(self.model.value(x.mean(axis=0)))
        self.encoder.observe(cards,actions[choice])
        return choice,x,float(logp[choice]),value

def ppo_loss_grad(model,x,offsets,actions,old_logp,advantages,returns,
                  weights,clip=0.2,entropy_coef=0.01,value_coef=0.5):
    x=model.mask_features(x)
    logits,h=model.forward(x)
    probs,logp,groups=distribution(logits,offsets)
    target=offsets[:-1]+actions
    ratio=np.exp(logp[target]-old_logp)
    clipped=np.clip(ratio,1-clip,1+clip)
    policy_loss=-np.minimum(ratio*advantages,clipped*advantages)
    active=((advantages>=0)&(ratio<=1+clip))|((advantages<0)&(ratio>=1-clip))
    entropy=-np.add.reduceat(probs*logp,offsets[:-1])
    weights=weights/weights.sum()
    grad_logits=probs.copy()
    grad_logits[target]-=1
    grad_logits*=((advantages*ratio*active)*weights)[groups]
    grad_logits+=entropy_coef*probs*(logp+entropy[groups])*weights[groups]
    grad={"out":h.T@grad_logits}
    dh=(grad_logits[:,None]*model.p["out"])*(1-h*h)
    grad["w"]=x.T@dh
    grad["b"]=dh.sum(axis=0)
    states=np.add.reduceat(x,offsets[:-1],axis=0)/np.diff(offsets)[:,None]
    values=model.value(states)
    residual=values-returns
    dv=value_coef*residual*(1-values*values)*weights
    grad["v"]=states.T@dv
    grad["vb"]=np.array([dv.sum()])
    loss=float(np.sum(weights*(policy_loss-entropy_coef*entropy+value_coef*0.5*residual**2)))
    stats={"loss":loss,"entropy":float(weights@entropy),
           "approx_kl":float(weights@(ratio-1-np.log(ratio))),
           "clip_fraction":float(weights@(np.abs(ratio-1)>clip)),
           "value_mse":float(weights@(residual**2))}
    return loss,grad,stats

class Adam:
    def __init__(self,model,lr=0.002):
        self.lr=lr;self.t=0
        self.m={k:np.zeros_like(v) for k,v in model.p.items()}
        self.v={k:np.zeros_like(v) for k,v in model.p.items()}
    def step(self,model,grad):
        norm=np.sqrt(sum(float(np.sum(g*g)) for g in grad.values()))
        scale=min(1.0,0.5/max(norm,1e-12))
        self.t+=1
        for k in model.p:
            g=grad[k]*scale
            self.m[k]=0.9*self.m[k]+0.1*g
            self.v[k]=0.999*self.v[k]+0.001*g*g
            model.p[k]-=self.lr*(self.m[k]/(1-0.9**self.t))/(np.sqrt(self.v[k]/(1-0.999**self.t))+1e-8)
        return norm
