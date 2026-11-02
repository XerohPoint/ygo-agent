#pragma once
#include "harness.h"
inline void ladd_sequence(certification::Assets& assets,int options,bool sequential,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({dupe,0,0,LOCATION_MZONE,0,uint8_t(sequential?POS_FACEDOWN_DEFENSE:POS_FACEUP_DEFENSE)});
  if(sequential) {
    d.add({light_dark_dragon,0,0,LOCATION_MZONE,1,POS_FACEUP_DEFENSE});
    d.add({jerry_beans,0,0,LOCATION_GRAVE,0,POS_FACEUP_ATTACK});
  }
  d.add({treeborn,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  bool attack=false,battle=false,damage_end=false,dupe_gone=false,revived=false,searched=false,ended=false,decided=false;
  int chains=0,resolving=0,missed=0;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) d.respond(e.player==0?7:6);
    else if(e.kind==MSG_SELECT_BATTLECMD) {
      if(!attack) {require(e.idle[0].size()==1,"Missing attacker");attack=true;d.respond(1);}
      else {
        require(missed==int(sequential)&&damage_end&&ended&&chains==1&&dupe_gone&&revived==sequential&&searched==!sequential&&decided==!sequential,"Wrong sequential trigger result");
        require(d.lp(0)==8000&&d.lp(1)==8000&&d.count(0,LOCATION_MZONE)==int(sequential)&&
                d.count(0,LOCATION_GRAVE)==(sequential?2:1)&&d.count(0,LOCATION_HAND)==int(!sequential)&&
                d.count(0,LOCATION_DECK)==int(sequential),"Wrong sequential final zones");
        if(sequential) require(d.code(0,LOCATION_MZONE,0)==jerry_beans,"Wrong revived monster");
        else require(d.code(0,LOCATION_HAND,0)==treeborn,"Wrong searched Frog");
        std::cout<<"Observed: "<<(sequential?"LADD destroyed Dupe, then revived Jerry; Dupe missed timing":"Dupe destroyed last; optional search resolved")<<".\n";return true;
      }
    } else if(e.kind==MSG_SELECT_CARD) {
      uint32_t want=!battle?(sequential?light_dark_dragon:dupe):sequential?jerry_beans:treeborn;
      int pick=-1;for(size_t i=0;i<e.cards.size();++i) if(e.cards[i].code==want) pick=i;
      require(pick>=0,"Missing attack/revival/search target");d.respond({1,uint8_t(pick)});
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(!sequential&&dupe_gone&&!decided&&e.cards.at(0).code==dupe,"Sequential destruction must miss optional timing");decided=true;d.respond(1);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced choice");d.respond(-1);}
      else {
        require(e.cards.size()==1&&e.cards[0].code==(sequential?light_dark_dragon:dupe)&&battle&&!chains,"Wrong delayed trigger");
        if(!sequential) {require(!decided,"Repeated optional offer");decided=true;}d.respond(0);
      }
    } else if(e.kind==MSG_BATTLE) {require(e.battle_destroyed[1],"Wrong battle destruction");battle=true;}
    else if(e.kind==MSG_CHAINING) {require(e.chain==1&&e.cards.at(0).code==(sequential?light_dark_dragon:dupe),"Wrong destruction trigger");++chains;}
    else if(e.kind==MSG_CHAIN_SOLVING) resolving=e.chain;
    else if(e.kind==MSG_CHAIN_SOLVED) resolving=0;
    else if(e.kind==MSG_CHAIN_END) ended=true;
    else if(e.kind==MSG_MOVE) {
      if(e.from.code==dupe&&e.to.location==LOCATION_GRAVE) {
        require(sequential?(resolving==1&&(e.reason&REASON_EFFECT)):(battle&&(e.reason&REASON_BATTLE)),"Wrong Dupe destruction");dupe_gone=true;
      } else if(e.from.code==jerry_beans) require(sequential&&resolving==1&&dupe_gone&&e.to.location==LOCATION_MZONE,"Revival must follow destruction");
      else if(e.from.code==treeborn) {require(!sequential&&resolving==1&&e.to.location==LOCATION_HAND,"Wrong search");searched=true;}
      else require(sequential&&e.from.code==light_dark_dragon&&battle&&e.to.location==LOCATION_GRAVE,"Unexpected movement");
    } else if(e.kind==MSG_SPSUMMONED) {require(sequential&&resolving==1&&dupe_gone,"Wrong revival order");revived=true;}
    else if(e.kind==MSG_SELECT_PLACE) d.respond({0,LOCATION_MZONE,0});
    else if(e.kind==MSG_SELECT_POSITION) d.respond(POS_FACEUP_ATTACK);
    else if(e.kind==MSG_DAMAGE_STEP_END) damage_end=true;
    else if(e.kind==MSG_MISSED_EFFECT) {require(sequential&&e.from.code==dupe,"Wrong missed effect");++missed;}
    else if(e.kind==MSG_CHAIN_NEGATED||e.kind==MSG_CHAIN_DISABLED||e.kind==MSG_SELECT_YESNO)
      throw std::runtime_error("Unexpected LADD sequence event");
    return false;
  });
}
