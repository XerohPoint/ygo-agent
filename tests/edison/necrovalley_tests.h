#pragma once
#include "harness.h"

inline void historical_necrovalley(certification::Assets& assets,int options,bool historical,int mode,bool trace) {
  using namespace certification;
  const bool frog=mode==0,call=mode==1,banish=mode==2;
  const bool revival=frog && historical, disabled=!banish && !revival;
  const uint32_t monster=frog?treeborn:blue_eyes, actor=frog?treeborn:call?call_haunted:bazoo;
  TestDuel d(assets,0,0,trace,options);
  d.add({necrovalley,1,1,LOCATION_SZONE,5,POS_FACEUP_ATTACK});
  d.add({monster,0,0,LOCATION_GRAVE,0,POS_FACEUP_ATTACK});
  if(call) d.add({call_haunted,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  if(banish) d.add({bazoo,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  bool chosen=false,announced=false,target=false,confirmed=false,resolving=false,solved=false;
  bool finished=false,negated=false,revived=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Unexpected idle");
      int index=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==actor) index=int(i);
      if(call && !chosen) {
        require(index>=0,"Targeted Call activation should be allowed before resolution negation");
        chosen=true;d.respond((index<<16)|5);
      } else {
        if(banish) require(index<0 && !chosen && !announced,"Necrovalley must prevent the Graveyard banish cost");
        else require(finished && chosen && negated==disabled && revived==revival && target==call,"Wrong Necrovalley resolution");
        require(d.count(0,LOCATION_MZONE)==int(banish || revival) &&
                d.count(0,LOCATION_GRAVE)==int(!revival) && d.count(0,LOCATION_SZONE)==int(call),
                "Wrong Necrovalley final zones");
        if(banish || revival) require(d.code(0,LOCATION_MZONE,0)==(banish?bazoo:treeborn),"Wrong retained/revived monster");
        if(!revival) require(d.code(0,LOCATION_GRAVE,0)==monster,"Graveyard card changed");
        if(call) require(d.code(0,LOCATION_SZONE,0)==call_haunted &&
                         (d.position(0,LOCATION_SZONE,0)&POS_FACEUP),"Negated Call must remain face-up");
        require(d.count(1,LOCATION_SZONE)==1 && d.code(1,LOCATION_SZONE,5)==necrovalley &&
                (d.position(1,LOCATION_SZONE,5)&POS_FACEUP),"Necrovalley missing");
        for(int p=0;p<2;++p) {
          require(d.lp(p)==8000,"Unexpected LP");
          for(int loc:{LOCATION_HAND,LOCATION_DECK,LOCATION_EXTRA,LOCATION_REMOVED}) require(d.count(p,loc)==0,"Unexpected extra zone");
          if(p==1) require(d.count(p,LOCATION_MZONE)==0 && d.count(p,LOCATION_GRAVE)==0,"Unexpected opponent monster");
        }
        std::cout<<"Observed: "<<(banish?"Graveyard banish cost unavailable":call?"targeted revival negated":
                    revival?"non-targeting Treeborn revival resolved":"modern Necrovalley negated Treeborn revival")<<".\n";
        return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");int index=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        require(!banish && e.player==0 && e.cards[i].code==actor,"Unexpected response");
        if(frog) {
          require(d.phase()==PHASE_STANDBY && !chosen,"Repeated/wrong-phase Treeborn offer");chosen=true;index=int(i);
        }
      }
      d.respond(index);
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(frog && !chosen && d.phase()==PHASE_STANDBY && e.player==0 && e.cards.at(0).code==treeborn,"Wrong Treeborn offer");
      chosen=true;d.respond(1);
    } else if(e.kind==MSG_CHAINING) {
      require(chosen && !announced && e.player==0 && e.chain==1 && e.cards.at(0).code==actor,"Wrong activation");announced=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(call && announced && !confirmed && !target && e.player==0 && e.minimum==1 && e.maximum==1 &&
              e.cards.size()==1 && e.cards[0].code==blue_eyes && e.cards[0].location==LOCATION_GRAVE,
              "Wrong targeted revival");target=true;d.respond({1,0});
    } else if(e.kind==MSG_CHAINED) {
      require(announced && !confirmed && e.chain==1 && (!call || target),"Incomplete link");confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && !solved && e.chain==1,"Wrong resolution");resolving=true;
    } else if(e.kind==MSG_CHAIN_DISABLED) {
      require(disabled && resolving && !negated && e.chain==1,"Unexpected negation");negated=true;
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(revival && resolving && e.player==0 && e.count==1 && !(e.mask&1),"Unexpected revival placement");
      d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(revival && resolving && e.cards.at(0).code==treeborn && (e.mask&POS_FACEUP_ATTACK),"Wrong revival position");
      d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_MOVE) {
      require(revival && resolving && e.from.code==treeborn && e.from.location==LOCATION_GRAVE &&
              e.to.location==LOCATION_MZONE && (e.reason&REASON_SPSUMMON),"Unexpected movement");
    } else if(e.kind==MSG_SPSUMMONING) {
      require(revival && resolving && e.cards.at(0).code==treeborn,"Unexpected Special Summon");
    } else if(e.kind==MSG_SPSUMMONED) {
      require(revival && resolving && !revived,"Unexpected revival success");revived=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving && e.chain==1,"Wrong solved link");resolving=false;solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved && !resolving && !finished,"Incomplete chain");finished=true;
    } else if(e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW || e.kind==MSG_PAY_LPCOST ||
              e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_DAMAGE)
      throw std::runtime_error("Unexpected Necrovalley event");
    return false;
  });
}
