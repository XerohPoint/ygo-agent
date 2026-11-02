#pragma once
#include "harness.h"

inline void strike_ninja_history(certification::Assets& assets,int options,bool historical,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  for(uint8_t i=0;i<2;++i) d.add({strike_ninja,0,0,LOCATION_MZONE,i,POS_FACEUP_ATTACK});
  for(int i=0;i<4;++i) d.add({sangan,0,0,LOCATION_GRAVE,0,POS_FACEUP_ATTACK});
  const int uses=historical?2:1;
  int chains=0,costs=0,departed=0,returned=0;
  bool requested=false,announced=false,confirmed=false,resolving=false,solved=false,passed=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN && e.player==1) {
      require(passed && chains==uses && costs==2*uses && departed==uses && returned==uses,"Incomplete temporary return");
      require(d.count(0,LOCATION_MZONE)==2 && d.count(0,LOCATION_REMOVED)==2*uses &&
              d.count(0,LOCATION_GRAVE)==4-2*uses && d.lp(0)==8000 && d.lp(1)==8000,"Wrong final Ninja/cost zones");
      for(int i=0;i<2*uses;++i) require(d.code(0,LOCATION_REMOVED,i)==sangan,"Only costs must remain banished");
      std::cout<<"Observed: "<<uses<<" Ninja activation(s), "<<2*uses<<" DARK costs, "<<returned<<" End Phase returns.\n";return true;
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && !requested && !passed,"Unexpected idle");
      if(chains==1) require(e.idle[5].empty()==!historical,"Strike Ninja limit must be per copy in Edison");
      if(chains<uses) {
        require(!e.idle[5].empty() && e.idle[5][0].code==strike_ninja,"Ninja unavailable");requested=true;d.respond(5);
      } else {require(e.idle[5].empty(),"Used Ninja should not remain available");passed=true;d.respond(7);}
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced prompt");d.respond(-1);}
      else if(passed && e.forced) {
        require(d.phase()==PHASE_END && e.player==0,"Wrong return window");
        for(auto& c:e.cards) require(c.code==strike_ninja,"Wrong delayed effect");d.respond(0);
      } else {
        require(!e.forced && e.player==0,"Wrong response");
        for(auto& c:e.cards) require(c.code==strike_ninja,"Wrong quick effect");d.respond(-1);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(requested && !announced && e.chain==1 && e.cards.at(0).code==strike_ninja,"Wrong activation");announced=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced && !confirmed && !resolving && e.player==0 && e.minimum==2 &&
              e.maximum==2 && e.cards.size()==size_t(4-costs),"Wrong DARK cost selection");
      for(auto& c:e.cards) require(c.code==sangan && c.location==LOCATION_GRAVE,"Wrong cost card");d.respond({2,0,1});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==sangan) {
        require(announced && !confirmed && !resolving && e.from.location==LOCATION_GRAVE &&
                e.to.location==LOCATION_REMOVED && (e.reason&REASON_COST),"Wrong banish cost timing");++costs;
      } else {
        require(e.from.code==strike_ninja,"Unexpected movement");
        if(e.to.location==LOCATION_REMOVED) {
          require(resolving && !passed && e.from.location==LOCATION_MZONE &&
                  (e.reason&(REASON_EFFECT|REASON_TEMPORARY))==(REASON_EFFECT|REASON_TEMPORARY),"Wrong temporary departure");++departed;
        } else {
          require(passed && d.phase()==PHASE_END && e.from.location==LOCATION_REMOVED &&
                  e.to.location==LOCATION_MZONE,"Wrong return");++returned;
        }
      }
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(passed && d.phase()==PHASE_END && e.player==0 && e.count==1,"Unexpected placement");
      int slot=0;while(slot<5&&(e.mask&(1u<<slot))) ++slot;
      require(slot<5,"No return zone");d.respond({0,LOCATION_MZONE,uint8_t(slot)});
    } else if(e.kind==MSG_CHAINED) {
      require(announced && !confirmed && costs==2*(chains+1) && e.chain==1,"Unpaid cost");confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {require(confirmed && !resolving && e.chain==1,"Wrong resolution");resolving=true;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving && departed==chains+1 && e.chain==1,"Departure missing");resolving=false;solved=true;}
    else if(e.kind==MSG_CHAIN_END) {require(solved && !resolving,"Incomplete chain");++chains;requested=announced=confirmed=solved=false;}
    else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_SPSUMMONING ||
            e.kind==MSG_SPSUMMONED || e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW)
      throw std::runtime_error("Unexpected Ninja event (temporary return is not a Special Summon)");
    return false;
  });
}
