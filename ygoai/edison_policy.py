"""Small public-observation policies for the pinned Edison interface.

No core queries, opponent deck contents, or privileged observations are used.
Card metadata is public and looked up only for identities supplied by the env.
"""
from collections import Counter
import random
import numpy as np

def u16(row, i):
    return int(row[i])*256+int(row[i+1])

def power(row, i=12):
    value = u16(row, i)
    return value if value <= 10000 else 0  # unknown printed ATK (-1)

class Heuristic:
    def __init__(self, metadata, seed=0, weights=None):
        self.metadata = metadata
        self.rng = random.Random(seed)
        self.weights = weights
        self.turn = None
        self.used = Counter()
        self.attacker = 0
        self.effect = ""

    def analyze(self, cards, glob, actions):
        # Turn counter in the observation saturates; turn-player changes still
        # reset bounded per-turn memory in long games.
        turn = (int(glob[4]), int(glob[7]))
        if turn != self.turn:
            self.used.clear()
            self.turn = turn
            self.attacker = 0
        own = cards[(cards[:,2]==3) & (cards[:,4]==0) & (cards[:,6]==0)]
        opp = cards[(cards[:,2]==3) & (cards[:,4]==1) & (cards[:,6]==0)]
        own_back = cards[(cards[:,2]==4) & (cards[:,4]==0)]
        opp_back = cards[(cards[:,2]==4) & (cards[:,4]==1)]
        defenses = [power(c, 12 if int(c[5])==1 else 14)
                    if u16(c,0) else 1900 for c in opp]
        weakest = min(defenses, default=0)
        strongest = max(defenses, default=0)
        scores, features = [], []
        for a in actions:
            act, msg, phase = map(int, (a[4],a[3],a[7]))
            cid=u16(a,1)
            meta=self.metadata.get(cid, {})
            name=meta.get("name","")
            c=cards[int(a[0])-1] if a[0] else np.zeros(41,dtype=np.uint8)
            atk=power(c) if a[0] else max(0,meta.get("atk",0))
            defense=power(c,14) if a[0] else max(0,meta.get("def",0))
            loc, enemy=int(c[2]),int(c[4])
            value=max(atk,defense)/1000
            score=-1.0
            safe=0.0
            margin=(atk-weakest)/1000
            if phase:
                score={1:5,2:1,3:0}[phase]
            elif act in (3,4):
                score=20+atk/1000
                # Prefer a useful body; protect existing stronger monsters when
                # a tribute is required. Still allow Monarch removal effects.
                level=int(c[9]) if a[0] else meta.get("level",0)
                tribute=1 if level in (5,6) else 2 if level>=7 else 0
                if act==4 and tribute and own.size:
                    costs=sorted(power(x) for x in own)[:tribute]
                    if atk<sum(costs) and "Monarch" not in name: score=-3
            elif act==5:
                score=12+defense/1000
                if atk>=1600: score=9
            elif act==1:
                score=7
                if name in ("Heavy Storm","Brain Control","Mind Control","Allure of Darkness",
                            "Fissure","Smashing Ground","Black Whirlwind"): score=-2
            elif act==2:
                # Attack -> defense if outmatched, defense -> attack if useful.
                score=8 if ((int(c[5])==1 and atk<strongest and defense>atk) or
                            (int(c[5])!=1 and atk>=weakest and atk>0)) else -4
            elif act in (6,7):
                score=15+margin if atk>0 and (not opp.size or atk>weakest) else -5
            elif act==9:
                score=0
            elif act==8:
                # Optional engine-trigger prompts generally give value; active
                # effects need a more conservative whitelist and board checks.
                safe=1.0 if msg in (6,7) else 0.0
                beneficial=("Black Whirlwind","Allure of Darkness","Treeborn Frog",
                    "Fishborg Blaster","Machina Gearframe","Machina Peacekeeper",
                    "Red Gadget","Green Gadget","Yellow Gadget","Sangan",
                    "Blackwing - Blizzard the Far North","Blackwing - Vayu the Emblem of Honor",
                    "Gorz the Emissary of Darkness","Battle Fader","Tragoedia")
                if name in beneficial: safe=1
                if name=="Swap Frog": safe=float(msg==6)
                if name in ("Heavy Storm",):
                    safe=float(len(opp_back)>len(own_back))
                if name in ("Torrential Tribute","Black Rose Dragon"):
                    safe=float(len(opp)>len(own)+1)
                if name in ("Mystical Space Typhoon",):
                    safe=float(len(opp_back)>0)
                if name in ("Fissure","Smashing Ground","Brain Control","Mind Control",
                            "Book of Moon","Compulsory Evacuation Device"):
                    safe=float(len(opp)>0)
                if name in ("Caius the Shadow Monarch","Raiza the Storm Monarch",
                            "Dark Armed Dragon","Brionac, Dragon of the Ice Barrier"):
                    safe=float(len(opp)+len(opp_back)>0)
                if name in ("Mirror Force","Dimensional Prison","Bottomless Trap Hole"):
                    safe=float(len(opp)>0 and not glob[7])
                if name in ("Icarus Attack",):
                    safe=float(len(opp)+len(opp_back)>=2 and len(own)>0)
                if name in ("Solemn Judgment","Royal Oppression","Limiter Removal"):
                    safe=0
                score=11 if safe else -3
            elif msg==5:
                score=5 if ((atk>=1600 and atk>=weakest and int(a[8])==1) or
                            ((atk<1600 or atk<weakest) and int(a[8]) in (4,6))) else 0
            elif msg in (3,4,9,12) and a[0]:
                if msg==4 or (not enemy and loc in (2,3,7) and msg in (9,12)):
                    score=-value  # low-value tribute/material/discard first
                elif enemy:
                    if self.attacker and loc==3:
                        target=power(c,12 if int(c[5])==1 else 14) if u16(c,0) else 1900
                        score=10+(self.attacker-target)/1000
                    else:
                        score=10+value
                elif loc in (1,5,6):
                    score=value+3  # search/revive a useful body
                else:
                    score=-value
            elif a[5]:
                score=5  # finish a legal multi-selection promptly
            else:
                score=0
            key=(cid,act,int(a[6]))
            repeat=self.used[key] if act in (2,3,8) else 0
            if repeat>=2: score=-10
            # Generic action/board features for a small learned linear scorer.
            f=np.zeros(64,dtype=np.float32)
            f[act]=1
            f[10+min(msg,16)]=1
            f[27+min(phase,3)]=1
            f[31+min(loc,7)]=1
            f[39:]=[atk/3000,defense/3000,enemy,value/3,margin/3,
                len(own)/5,len(opp)/5,len(own_back)/5,len(opp_back)/5,
                safe,float(a[5]),min(repeat,2)/2,float(glob[7]),
                float(act in (3,4))*atk/3000,
                float(act in (6,7))*margin/3,
                float(msg in (3,4,9,12))*enemy,
                float(msg in (3,4,9,12))*value/3,
                float(msg==4)*value/3,
                float(act==8)*safe,float(act==8)*(repeat>=2),
                float(act==2)*(repeat>=2),float(act==3)*(repeat>=2),
                float(msg==5 and int(a[8])==1)*atk/3000,
                float(msg==5 and int(a[8]) in (4,6))*defense/3000,1]
            scores.append(score)
            features.append(f)
        return np.asarray(scores),np.asarray(features)

    def choose(self, cards, glob, actions):
        scores, features=self.analyze(cards,glob,actions)
        choice=int(np.argmax(scores if self.weights is None else features@self.weights))
        self.observe(cards,actions[choice])
        return choice,features,int(np.argmax(scores))

    def observe(self, cards, a):
        cid,act=u16(a,1),int(a[4])
        self.used[cid,act,int(a[6])]+=1
        if act in (6,7) and a[0]:
            self.attacker=power(cards[int(a[0])-1])
        elif act==8:
            self.attacker=0
            self.effect=self.metadata.get(cid,{}).get("name","")
        elif int(a[3]) in (1,8):
            self.attacker=0


class ContextHeuristic(Heuristic):
    """Opt-in context consumer; the frozen heuristic/64-feature model is unchanged.

    Stage 1 means the core is executing a cost callback. Stage 2 is activation
    setup, which is not a promise that a selection formally targets. Stage 3 is
    an operation callback (including summoning procedures).
    """
    def choose(self, cards, glob, actions, decision, selection):
        if self.weights is not None:
            raise ValueError("The context policy has no trained feature schema yet")
        # Attack attribution comes from the current observation, not the last
        # action remembered across intervening chains or another player's turn.
        self.attacker=0
        scores,features=self.analyze(cards,glob,actions)
        if decision[0]!=1:
            raise ValueError("Unsupported decision-context schema")
        if decision[11]:
            hint,stage,source=map(int,(decision[1],decision[2],decision[3]))
            attacker=power(cards[source-1]) if source else 0
            for i,a in enumerate(actions):
                if not a[0]:
                    continue
                row=int(a[0])-1
                c=cards[row]
                value=max(power(c),power(c,14))/1000
                enemy=bool(c[4])
                if hint==549 and source:
                    defense=power(c,12 if int(c[5])==1 else 14) if u16(c,0) else 1900
                    scores[i]=10+(attacker-defense)/1000
                elif stage==1 or hint in (500,501,511,512,513,531,533):
                    scores[i]=(10+value) if enemy else -value
                elif hint in (502,503,505,507,520,551):
                    scores[i]=(10+value) if enemy else -value
                elif hint in (506,508,509):
                    scores[i]=3+value
                elif hint==504 and stage==3:
                    # Sending cards during resolution can set up grave effects;
                    # generic value ranking needs card-specific planning.
                    pass
                if selection[row,1]:
                    scores[i]=-100  # selected cards are not new selections
        choice=int(np.argmax(scores))
        self.observe(cards,actions[choice])
        return choice,features,choice
