#pragma once
#include "harness.h"

inline void redmd_limits(certification::Assets& assets,int options,bool historical,bool procedure,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  for(uint8_t i=0;i<2;++i) {
    d.add({redmd,0,0,uint8_t(procedure?LOCATION_HAND:LOCATION_MZONE),i,uint8_t(procedure?POS_FACEDOWN_DEFENSE:POS_FACEUP_ATTACK)});
    d.add({blue_eyes,0,0,uint8_t(procedure?LOCATION_MZONE:LOCATION_GRAVE),i,POS_FACEUP_ATTACK});
  }
  const int uses=historical?2:1;
  int summons=0,costs=0,chains=0,choices=0;bool requested=false,announced=false,confirmed=false,resolving=false,solved=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && (!requested || (procedure&&summons>0)),"Unexpected idle");
      requested=false;
      auto& list=e.idle[procedure?1:5];
      if(summons==1) require(!list.empty()==historical,"REDMD historical per-name restriction is wrong");
      if(summons==uses) {
        require(list.empty() && choices==uses && costs==(procedure?uses:0) && chains==(procedure?0:uses),"Incomplete REDMD sequence");
        require(d.count(0,LOCATION_MZONE)==(procedure?2:2+uses) && d.count(0,LOCATION_HAND)==(procedure?2-uses:0) &&
                d.count(0,LOCATION_GRAVE)==(procedure?0:2-uses) && d.count(0,LOCATION_REMOVED)==(procedure?uses:0) &&
                d.lp(0)==8000 && d.lp(1)==8000,"Wrong REDMD final zones");
        std::cout<<"Observed: "<<uses<<" "<<(procedure?"summon procedure(s)":"non-targeting revival effect(s)")<<".\n";return true;
      }
      require(!list.empty() && list[0].code==redmd,"REDMD unavailable");
      requested=true;d.respond(procedure?1:5);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");
      for(auto& c:e.cards) require(c.code==redmd && e.player==0,"Unexpected quick response");
      d.respond(-1);
    } else if(e.kind==MSG_CHAINING) {
      require(!procedure && requested && !announced && e.chain==1 && e.cards.at(0).code==redmd,"Wrong ignition chain");announced=true;
    } else if(e.kind==MSG_SELECT_CARD || e.kind==MSG_SELECT_UNSELECT_CARD) {
      require(requested && e.player==0 && e.minimum==1 && e.maximum==1 && e.cards.size()==size_t(procedure?2:2-summons) &&
              (procedure?!announced:resolving),"REDMD revival must select at resolution");
      int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        auto& c=e.cards[i];
        require((c.code==blue_eyes || (procedure&&c.code==redmd)) &&
                c.location==(procedure?LOCATION_MZONE:LOCATION_GRAVE),"Wrong Dragon choice");
        if(c.code==blue_eyes) pick=i;
      }
      require(pick>=0,"Missing Blue-Eyes choice");++choices;d.respond({1,uint8_t(pick)});
    } else if(e.kind==MSG_MOVE) {
      if(procedure && e.from.code==blue_eyes) {
        require(requested && !announced && e.from.location==LOCATION_MZONE && e.to.location==LOCATION_REMOVED &&
                (e.reason&REASON_SPSUMMON),"Wrong procedure banish");++costs;
      } else {
        require(requested && e.from.code==(procedure?redmd:blue_eyes) &&
                e.from.location==(procedure?LOCATION_HAND:LOCATION_GRAVE) && e.to.location==LOCATION_MZONE &&
                (procedure?costs==summons+1:resolving),"Wrong summon movement");
      }
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(requested && e.player==0 && e.count==1,"Wrong placement");
      int slot=0;while(slot<5&&(e.mask&(1u<<slot))) ++slot;
      require(slot<5,"No summon zone");d.respond({0,LOCATION_MZONE,uint8_t(slot)});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(requested && e.cards.at(0).code==(procedure?redmd:blue_eyes) && (e.mask&POS_FACEUP_ATTACK),"Wrong position");
      d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONED) {
      require(requested && choices==summons+1 && (procedure?!announced:resolving),"Wrong summon success");++summons;
    } else if(e.kind==MSG_CHAINED) {require(announced && !confirmed && e.chain==1 && choices==chains,"Revival must not target at activation");confirmed=true;}
    else if(e.kind==MSG_CHAIN_SOLVING) {require(confirmed && !resolving && e.chain==1,"Wrong resolution");resolving=true;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving && summons==chains+1 && e.chain==1,"Revival missing");resolving=false;solved=true;}
    else if(e.kind==MSG_CHAIN_END) {require(solved && !resolving,"Incomplete chain");++chains;requested=announced=confirmed=solved=false;}
    else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_SELECT_EFFECTYN ||
            e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW) throw std::runtime_error("Unexpected REDMD event");
    return false;
  });
}
