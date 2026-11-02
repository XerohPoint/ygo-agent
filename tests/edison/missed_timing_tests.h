#pragma once
#include "harness.h"

// mode 0: tribute Dupe for Caius (miss timing); 1/2: Exiled destroys
// Dupe as its final action, then accept/decline its optional search.
inline void missed_timing(certification::Assets& assets, int options, int mode, bool trace) {
  using namespace certification;
  const bool tribute=mode==0, accept=mode==1;
  const uint32_t first=tribute?caius:exiled_force;
  TestDuel d(assets,0,0,trace,options);
  d.add({dupe,0,0,LOCATION_MZONE,1,POS_FACEUP_DEFENSE});
  d.add({treeborn,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  if(tribute) {
    d.add({caius,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
    d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  } else d.add({exiled_force,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  bool requested=false,paid=false,left=false,summoned=false,decided=false,target=false,added=false;
  bool announced=false,confirmed=false,resolving=false,solved=false;
  int chains=0;
  bool missed=false;
  auto choose=[&] {
    require(!tribute && chains==1 && left && !decided,"Dupe offered with incorrect timing or repeated");
    decided=true;d.respond(accept?1:0);
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Unexpected idle phase/player");
      if(requested) {
        require(chains==1+int(accept) && paid && left && target && summoned==tribute &&
                decided==!tribute && added==accept && missed==tribute,"Missing missed-timing checkpoint");
        require(d.count(0,LOCATION_HAND)==int(accept) && d.count(0,LOCATION_DECK)==int(!accept) &&
                d.count(0,LOCATION_GRAVE)==(tribute?1:2) && d.count(0,LOCATION_MZONE)==int(tribute),
                "Unexpected final own zones");
        require(d.code(0,accept?LOCATION_HAND:LOCATION_DECK,0)==treeborn,"Wrong search outcome");
        require(d.code(0,LOCATION_GRAVE,tribute?0:1)==dupe,"Dupe must stay in Graveyard");
        if(tribute) require(d.code(0,LOCATION_MZONE,0)==caius &&
                            d.count(1,LOCATION_REMOVED)==1 && d.code(1,LOCATION_REMOVED,0)==blue_eyes,
                            "Caius must banish its target");
        else require(d.code(0,LOCATION_GRAVE,0)==exiled_force && d.count(1,LOCATION_REMOVED)==0,
                     "Exiled cost missing");
        for(int p=0;p<2;++p)
          require(d.lp(p)==8000 && d.count(p,LOCATION_SZONE)==0 && d.count(p,LOCATION_EXTRA)==0,
                  "Unexpected LP/S/T/Extra state");
        require(d.count(1,LOCATION_HAND)==0 && d.count(1,LOCATION_DECK)==0 &&
                d.count(1,LOCATION_MZONE)==0 && d.count(1,LOCATION_GRAVE)==0 &&
                d.count(0,LOCATION_REMOVED)==0,"Unexpected remaining cards");
        std::cout << "Observed: Dupe " << (tribute?"missed timing after Tribute Summon":
                    accept?"offered and resolved its search after destruction":"offered and declined after destruction")
                  << ".\n";
        return true;
      }
      auto& list=e.idle[tribute?0:5];int index=-1;
      for(size_t i=0;i<list.size();++i) if(list[i].code==first) index=int(i);
      require(index>=0,"Required setup action missing");requested=true;d.respond((index<<16)|(tribute?0:5));
    } else if(e.kind==MSG_SELECT_YESNO) {
      require(tribute && requested && !summoned && !paid,"Unexpected yes/no prompt");d.respond(1);
    } else if(e.kind==MSG_SELECT_TRIBUTE) {
      require(tribute && requested && !paid && e.player==0 && e.minimum==1 && e.maximum==1 &&
              e.cards.size()==1 && e.cards[0].code==dupe,"Expected Dupe tribute");d.respond({1,0});
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(tribute && paid && !summoned && e.player==0 && e.count==1 && !(e.mask&1u),
              "Expected Caius placement");d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SUMMONED) {
      require(tribute && paid && left && !summoned,"Unexpected summon success");summoned=true;
    } else if(e.kind==MSG_MISSED_EFFECT) {
      require(tribute && left && !missed && e.from.code==dupe && e.from.controller==0 &&
              e.from.location==LOCATION_GRAVE,"Unexpected missed-effect notification");
      missed=true;
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(e.player==0 && e.cards.size()==1 && e.cards[0].code==dupe,"Unexpected optional effect");choose();
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) { require(!e.forced,"Empty forced prompt");d.respond(-1); }
      else {
        require(e.player==0 && e.cards.size()==1 && e.cards[0].code==dupe && !e.forced,
                "Unexpected chain candidate");
        require(!tribute && chains==1 && !decided,"Dupe missed timing or repeated choice");decided=true;
        d.respond(accept?0:-1);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(!announced && e.player==0 && e.chain==1 &&
              e.cards.at(0).code==(chains==0?first:dupe) &&
              (chains==0 || (!tribute && decided && accept)),"Unexpected trigger chain");
      if(tribute) require(paid && left && summoned,"Caius must follow Tribute Summon");
      announced=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced && e.player==0 && e.minimum==1 && e.maximum==1,"Unexpected selection");
      uint32_t want=chains==0?(tribute?blue_eyes:dupe):treeborn;
      int loc=chains==0?LOCATION_MZONE:LOCATION_DECK,controller=chains==0 && tribute?1:0,index=-1;
      if(chains==0) { require(!confirmed && !target && paid,"Target must follow paid cost/summon");target=true; }
      else require(resolving && accept && !added,"Unexpected search selection");
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want && e.cards[i].controller==controller && e.cards[i].location==loc) index=int(i);
      require(index>=0,"Required target or search missing");d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==dupe && e.to.location==LOCATION_GRAVE) {
        require(!left && e.from.location==LOCATION_MZONE &&
                (tribute?bool(e.reason&REASON_RELEASE):
                 resolving && (e.reason&(REASON_EFFECT|REASON_DESTROY))==(REASON_EFFECT|REASON_DESTROY)),
                "Wrong Dupe departure reason/timing");left=true;if(tribute) paid=true;
      } else if(e.from.code==exiled_force) {
        require(!tribute && announced && !confirmed && !paid && e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_COST|REASON_RELEASE))==(REASON_COST|REASON_RELEASE),"Wrong Exiled cost");paid=true;
      } else if(e.from.code==treeborn) {
        require(accept && chains==1 && resolving && !added && e.from.location==LOCATION_DECK &&
                e.to.location==LOCATION_HAND,"Unexpected search movement");added=true;
      } else if(e.from.code==blue_eyes) {
        require(tribute && resolving && target && e.from.location==LOCATION_MZONE &&
                e.to.location==LOCATION_REMOVED && (e.reason&REASON_EFFECT),"Unexpected banish");
      } else require(tribute && e.from.code==caius && paid && !summoned &&
                     e.from.location==LOCATION_HAND && e.to.location==LOCATION_MZONE,"Unexpected movement");
    } else if(e.kind==MSG_CHAINED) {
      require(announced && !confirmed && e.chain==1 && (chains>0 || target),"Incomplete confirmed link");confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && !solved && e.chain==1,"Unexpected resolution");resolving=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving && !solved && e.chain==1,"Unexpected solved link");resolving=false;solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved && !resolving,"Incomplete chain");++chains;announced=confirmed=solved=false;
    } else if(e.kind==MSG_DRAW || e.kind==MSG_PAY_LPCOST || e.kind==MSG_CHAIN_DISABLED ||
              e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_SPSUMMONING)
      throw std::runtime_error("Unexpected event in missed timing fixture");
    return false;
  });
}
