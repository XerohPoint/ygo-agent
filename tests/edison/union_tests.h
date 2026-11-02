#pragma once
#include "harness.h"

inline void union_limit(certification::Assets& assets,int options,bool historical,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({jade_knight,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  for(uint8_t i=1;i<3;++i) d.add({gearframe,0,0,LOCATION_MZONE,i,POS_FACEUP_ATTACK});
  int turns=0,chains=0,equips=0;const int uses=historical?1:2;
  bool requested=false,announced=false,confirmed=false,resolving=false,solved=false,passed=false,unequip=false,summoned=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {require(e.player==turns%2 && turns<3,"Wrong turn");++turns;}
    else if(e.kind==MSG_SELECT_IDLECMD) {
      if(e.player==1) {require(passed && turns==2,"Wrong opposing turn");d.respond(7);}
      else if(turns==1) {
        require(!requested,"Incomplete action");
        if(equips==1) require(e.idle[5].empty()==historical,"Edison must prevent a second Union on the same monster");
        if(equips<uses) {
          require(!e.idle[5].empty() && e.idle[5][0].code==gearframe &&
                  e.idle[5][0].location==LOCATION_MZONE,"Wrong equip availability");requested=true;d.respond(5);
        } else {require(e.idle[5].empty(),"Union cannot equip/unequip twice in one turn");passed=true;d.respond(7);}
      } else if(!unequip) {
        require(turns==3 && passed && chains==uses,"Wrong unequip turn");
        int pick=-1;for(size_t i=0;i<e.idle[5].size();++i)
          if(e.idle[5][i].code==gearframe && e.idle[5][i].location==LOCATION_SZONE && e.idle[5][i].sequence==0) pick=i;
        require(pick>=0,"Next-turn unequip unavailable");unequip=requested=true;d.respond((pick<<16)|5);
      } else {
        require(chains==uses+1 && summoned && equips==uses && d.count(0,LOCATION_MZONE)==4-uses &&
                d.count(0,LOCATION_SZONE)==uses-1 && d.count(0,LOCATION_GRAVE)==0 &&
                d.code(0,LOCATION_MZONE,1)==gearframe && d.lp(0)==8000 && d.lp(1)==8000,"Wrong Union return state");
        for(auto& c:e.idle[5]) require(!(c.location==LOCATION_MZONE && c.sequence==1),"Unequipped Union cannot equip again this turn");
        std::cout<<"Observed: "<<uses<<" Union(s) equipped to Jade; same-turn reuse blocked; next-turn unequip succeeds.\n";return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced effect");
      for(auto& c:e.cards) require(c.code==gearframe && e.player==0,"Wrong response candidate");d.respond(-1);
    } else if(e.kind==MSG_CHAINING) {
      require(requested && !announced && e.chain==1 && e.cards.at(0).code==gearframe,"Wrong Union effect");announced=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(!unequip && announced && !confirmed && e.player==0 && e.minimum==1 && e.maximum==1,"Wrong equip target choice");
      int pick=-1;for(size_t i=0;i<e.cards.size();++i) if(e.cards[i].code==jade_knight) pick=i;
      require(pick>=0,"Jade must be an eligible equip target");d.respond({1,uint8_t(pick)});
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(resolving && e.player==0 && e.count==1,"Wrong placement");
      d.respond({0,uint8_t(unequip?LOCATION_MZONE:LOCATION_SZONE),uint8_t(unequip?1:equips)});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(unequip && resolving && e.cards.at(0).code==gearframe && (e.mask&POS_FACEUP_ATTACK),"Wrong unequip position");d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_MOVE) {
      require(resolving && e.from.code==gearframe && e.from.location==(unequip?LOCATION_SZONE:LOCATION_MZONE) &&
              e.to.location==(unequip?LOCATION_MZONE:LOCATION_SZONE),"Wrong Union movement");
    } else if(e.kind==MSG_EQUIP) {
      require(!unequip && resolving && e.to.controller==0 && e.to.location==LOCATION_MZONE &&
              e.to.sequence==0,"Wrong equip relationship");++equips;
    } else if(e.kind==MSG_SPSUMMONED) {require(unequip && resolving && !summoned,"Wrong unequip summon");summoned=true;}
    else if(e.kind==MSG_CHAINED) {require(announced && !confirmed && e.chain==1,"Wrong confirmation");confirmed=true;}
    else if(e.kind==MSG_CHAIN_SOLVING) {require(confirmed && !resolving && e.chain==1,"Wrong resolution");resolving=true;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving && e.chain==1,"Wrong solved link");resolving=false;solved=true;}
    else if(e.kind==MSG_CHAIN_END) {require(solved && !resolving,"Incomplete chain");++chains;requested=announced=confirmed=solved=false;}
    else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_SELECT_YESNO ||
            e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_DRAW) throw std::runtime_error("Unexpected Union event");
    return false;
  });
}
