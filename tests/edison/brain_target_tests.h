#pragma once
#include "harness.h"

// Goyo is preplaced solely as a face-up Synchro target. Its summon procedure
// and battle trigger are not exercised here.
inline void historical_brain_target(certification::Assets& assets, int options,
                                    bool historical, bool synchro, bool trace) {
  using namespace certification;
  const uint32_t target=synchro?goyo:blue_eyes;
  const bool allowed=historical || !synchro;
  TestDuel d(assets,0,0,trace,options);
  d.add({target,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({brain,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  bool requested=false,announced=false,paid=false,selected=false,confirmed=false;
  bool resolving=false,solved=false,finished=false,controlled=false;
  auto final=[&] {
    require(d.lp(0)==(allowed?7200:8000) && d.lp(1)==8000 &&
            d.count(0,LOCATION_MZONE)==int(allowed) && d.count(1,LOCATION_MZONE)==int(!allowed) &&
            d.code(allowed?0:1,LOCATION_MZONE,0)==target &&
            d.position(allowed?0:1,LOCATION_MZONE,0)==POS_FACEUP_ATTACK,"Wrong final control/LP");
    require(d.count(0,LOCATION_SZONE)==int(!allowed) && d.count(0,LOCATION_GRAVE)==int(allowed),
            "Wrong Brain Control destination");
    if(allowed) require(d.code(0,LOCATION_GRAVE,0)==brain,"Resolved Brain missing");
    else require(d.code(0,LOCATION_SZONE,0)==brain &&
                 (d.position(0,LOCATION_SZONE,0)&POS_FACEDOWN),"Unavailable Brain must stay set");
    for(int p=0;p<2;++p) {
      for(int loc:{LOCATION_HAND,LOCATION_DECK,LOCATION_EXTRA,LOCATION_REMOVED})
        require(d.count(p,loc)==0,"Unexpected extra zone");
      if(p==1) require(d.count(p,LOCATION_SZONE)==0 && d.count(p,LOCATION_GRAVE)==0,"Unexpected opponent card");
    }
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Unexpected idle");
      int index=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==brain) index=int(i);
      if(!requested) {
        require((index>=0)==allowed,"Brain Control target eligibility differs from selected historical rules");
        if(!allowed) {
          final();std::cout<<"Observed: Rule 5 excludes the sole Synchro target; no activation or LP payment.\n";
          return true;
        }
        requested=true;d.respond((index<<16)|5);
      } else {
        require(finished && controlled && paid && selected && confirmed && solved,"Incomplete control change");
        final();std::cout<<"Observed: Brain paid 800 LP and took "<<(synchro?"Goyo Guardian":"Blue-Eyes")<<".\n";
        return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(e.cards.empty() && !e.forced,"Unexpected response");d.respond(-1);
    } else if(e.kind==MSG_CHAINING) {
      require(requested && !announced && e.player==0 && e.chain==1 && e.cards.at(0).code==brain,
              "Wrong activation");announced=true;
    } else if(e.kind==MSG_PAY_LPCOST) {
      require(announced && !confirmed && !paid && e.player==0 && e.mask==800,"Wrong Brain cost");paid=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced && paid && !confirmed && !selected && e.player==0 &&
              e.minimum==1 && e.maximum==1 && e.cards.size()==1 &&
              e.cards[0].code==target && e.cards[0].controller==1 && e.cards[0].location==LOCATION_MZONE,
              "Wrong Brain target");selected=true;d.respond({1,0});
    } else if(e.kind==MSG_CHAINED) {
      require(selected && !confirmed && e.chain==1,"Incomplete Brain link");confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && !solved && e.chain==1,"Wrong resolution");resolving=true;
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(resolving && e.player==0 && e.count==1 && !(e.mask&1),"Unexpected control placement");
      d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==target) {
        require(resolving && !controlled && e.from.controller==1 && e.to.controller==0 &&
                e.from.location==LOCATION_MZONE && e.to.location==LOCATION_MZONE,"Wrong control movement");
        controlled=true;
      } else require(e.from.code==brain && e.from.location==LOCATION_SZONE &&
                     e.to.location==LOCATION_GRAVE,"Unexpected movement");
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving && controlled && e.chain==1,"Control effect did not resolve");resolving=false;solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved && !resolving && !finished,"Incomplete chain");finished=true;
    } else if(e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_DRAW ||
              e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_SPSUMMONED)
      throw std::runtime_error("Unexpected Brain Control event");
    return false;
  });
}
