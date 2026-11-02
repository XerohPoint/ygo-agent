#pragma once
#include "harness.h"
inline void swap_extra_limit(certification::Assets& assets,int options,bool historical,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  for(uint8_t i=0;i<2;++i) d.add({swap_frog,0,0,LOCATION_MZONE,i,POS_FACEUP_ATTACK});
  for(auto c:{jerry_beans,dupe,treeborn}) d.add({c,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  int normals=0,uses=0,costs=0;bool announced=false,confirmed=false,resolving=false;
  uint32_t summoning=0;int actor_zone=-1;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      if(!normals) {
        int pick=-1;for(size_t i=0;i<e.idle[0].size();++i) if(e.idle[0][i].code==jerry_beans) pick=i;
        require(pick>=0,"Missing ordinary summon");summoning=jerry_beans;d.respond(pick<<16);
      } else if(uses<(historical?2:1)) {
        require(!e.idle[5].empty()&&e.idle[5][0].code==swap_frog,"Each historical Swap must activate independently");
        actor_zone=e.idle[5][0].sequence;d.respond(5);
      } else if(normals==1) {
        if(!historical) require(e.idle[5].empty(),"Rule 5 must retain once-per-turn shared restriction");
        int pick=-1;for(size_t i=0;i<e.idle[0].size();++i) {
          require(e.idle[0][i].code==dupe||e.idle[0][i].code==treeborn,"Swap itself cannot use extra summon");
          if(e.idle[0][i].code==dupe) pick=i;
        }
        require(pick>=0,"Missing extra Frog summon");summoning=dupe;d.respond(pick<<16);
      } else {
        require(e.idle[0].empty()&&normals==2&&costs==uses&&uses==(historical?2:1)&&
                d.count(0,LOCATION_MZONE)==(historical?2:3)&&d.count(0,LOCATION_HAND)==(historical?3:2)&&
                d.count(0,LOCATION_GRAVE)==0&&d.lp(0)==8000&&d.lp(1)==8000,"Extra normal summons must not stack");
        std::cout<<"Observed: "<<uses<<" Swap effect(s), "<<costs<<" self-return costs, exactly two Normal Summons.\n";return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {require(!e.forced,"Forced Swap response");for(auto c:e.cards) require(c.code==swap_frog,"Unexpected response");d.respond(-1);}
    else if(e.kind==MSG_CHAINING) {require(e.chain==1&&e.cards.at(0).code==swap_frog,"Wrong Swap chain");announced=true;}
    else if(e.kind==MSG_SELECT_CARD) {
      require(announced&&!confirmed,"Return must be selected as cost");int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) if(e.cards[i].code==swap_frog&&e.cards[i].sequence==actor_zone) pick=i;
      require(pick>=0,"Missing self-return");d.respond({1,uint8_t(pick)});
    } else if(e.kind==MSG_MOVE&&e.to.location==LOCATION_HAND) {
      require(announced&&!confirmed&&e.from.code==swap_frog&&(e.reason&REASON_COST),"Wrong return cost timing");++costs;
    } else if(e.kind==MSG_CHAINED) {require(costs==uses+1,"Missing cost");confirmed=true;}
    else if(e.kind==MSG_CHAIN_SOLVING) resolving=true;
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving,"Wrong resolution");resolving=false;}
    else if(e.kind==MSG_CHAIN_END) {++uses;announced=confirmed=false;}
    else if(e.kind==MSG_SELECT_PLACE) {
      int zone=0;while(zone<5&&(e.mask&(1u<<zone))) ++zone;
      require(zone<5&&summoning,"Wrong normal placement");d.respond({0,LOCATION_MZONE,uint8_t(zone)});
    } else if(e.kind==MSG_SUMMONED) {require(summoning,"Unexpected Normal Summon");++normals;summoning=0;}
    else if(e.kind==MSG_SELECT_YESNO) {require(summoning==dupe,"Wrong extra summon choice");d.respond(1);}
    else if(e.kind==MSG_SELECT_OPTION) {require(summoning==dupe&&e.options.size()>=1,"Wrong extra summon option");d.respond(0);}
    else if(e.kind==MSG_SELECT_EFFECTYN||e.kind==MSG_CHAIN_NEGATED||e.kind==MSG_CHAIN_DISABLED||e.kind==MSG_SPSUMMONED)
      throw std::runtime_error("Unexpected Swap-limit event");
    return false;
  });
}
