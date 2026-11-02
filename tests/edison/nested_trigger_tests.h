#pragma once
#include "harness.h"

inline void nested_trigger(certification::Assets& assets, int options, bool accept, bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({dekoichi,0,0,LOCATION_MZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({watapon,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  bool requested=false,flipping=false,flipped=false,drawn=false,decided=false,summoning=false,summoned=false;
  bool announced=false,confirmed=false,resolving=false,solved=false;
  int ended=0;
  auto decide=[&] {
    require(ended==1 && !announced && drawn && !decided,"Nested trigger must wait for the original chain to end");
    require(d.count(0,LOCATION_HAND)==1 && d.code(0,LOCATION_HAND,0)==watapon &&
            (d.position(0,LOCATION_HAND,0)&POS_FACEDOWN),"Watapon must be private and eligible");
    decided=true;
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Expected first Main Phase");
      if(requested) {
        require(flipped && drawn && decided && ended==1+int(accept) && summoned==accept,
                "Nested trigger outcome incomplete");
        require(d.count(0,LOCATION_DECK)==0 && d.count(0,LOCATION_HAND)==int(!accept) &&
                d.count(0,LOCATION_MZONE)==1+int(accept) && d.code(0,LOCATION_MZONE,0)==dekoichi,
                "Unexpected final own zones");
        require(d.code(0,accept?LOCATION_MZONE:LOCATION_HAND,accept?1:0)==watapon,
                "Unexpected Watapon destination");
        for(int p=0;p<2;++p) {
          require(d.lp(p)==8000 && d.count(p,LOCATION_GRAVE)==0 && d.count(p,LOCATION_SZONE)==0 &&
                  d.count(p,LOCATION_EXTRA)==0 && d.count(p,LOCATION_REMOVED)==0,"Unexpected final state");
          if(p==1) for(int loc:{LOCATION_DECK,LOCATION_HAND,LOCATION_MZONE})
            require(d.count(p,loc)==0,"Unexpected opposing cards");
        }
        std::cout << "Observed: Dekoichi drew private Watapon and ended its chain; "
                  << (accept?"Watapon started a new CL1 and summoned":"Watapon was offered and declined") << ".\n";
        return true;
      }
      require(e.idle[2].size()==1 && e.idle[2][0].code==dekoichi,"Expected Dekoichi Flip Summon");
      requested=true;d.respond(2);
    } else if(e.kind==MSG_FLIPSUMMONING) {
      require(requested && !flipping && e.cards.at(0).code==dekoichi,"Unexpected Flip Summon");flipping=true;
    } else if(e.kind==MSG_FLIPSUMMONED) {
      require(flipping && !flipped,"Unexpected Flip Summon success");flipped=true;
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(e.player==0 && e.cards.size()==1 && e.cards[0].code==watapon,"Unexpected optional effect");
      decide();d.respond(accept?1:0);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) { require(!e.forced,"Empty forced choice");d.respond(-1); }
      else {
        require(!e.forced && e.player==0 && e.cards.size()==1 && e.cards[0].code==watapon,
                "Unexpected chain candidate");decide();d.respond(accept?0:-1);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(flipped && !announced && e.chain==1 && e.player==0 &&
              e.cards.at(0).code==(ended==0?dekoichi:watapon) &&
              (ended==0 || (ended==1 && decided && accept)),"Nested trigger joined the wrong chain");
      announced=true;
    } else if(e.kind==MSG_CHAINED) {
      require(announced && !confirmed && e.chain==1,"Unexpected confirmed link");confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && !solved && e.chain==1,"Unexpected resolution");resolving=true;
    } else if(e.kind==MSG_DRAW) {
      require(ended==0 && resolving && !drawn && e.player==0 && e.count==1 &&
              (e.cards.at(0).code&0x7fffffffu)==watapon,"Dekoichi must draw Watapon during resolution");drawn=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving && !solved && e.chain==1 && (ended==0?drawn:summoned),
              "Effect resolved without expected outcome");resolving=false;solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved && !resolving && ended<1+int(accept),"Incomplete/extra chain");
      ++ended;announced=confirmed=solved=false;
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(ended==1 && resolving && accept && e.player==0 && e.count==1 && !(e.mask&2u),
              "Unexpected summon placement");d.respond({0,LOCATION_MZONE,1});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(ended==1 && resolving && accept && e.player==0 && e.cards.at(0).code==watapon &&
              (e.mask&POS_FACEUP_ATTACK),"Unexpected summon position");d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONING) {
      require(ended==1 && resolving && accept && !summoning && e.cards.at(0).code==watapon &&
              e.cards[0].controller==0 && e.cards[0].sequence==1,"Unexpected Special Summon");summoning=true;
    } else if(e.kind==MSG_SPSUMMONED) {
      require(summoning && !summoned && resolving,"Unexpected summon success");summoned=true;
    } else if(e.kind==MSG_SELECT_CARD || e.kind==MSG_SELECT_YESNO || e.kind==MSG_SELECT_TRIBUTE ||
              e.kind==MSG_MISSED_EFFECT || e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED ||
              e.kind==MSG_PAY_LPCOST)
      throw std::runtime_error("Unexpected event in nested trigger fixture");
    return false;
  });
}
