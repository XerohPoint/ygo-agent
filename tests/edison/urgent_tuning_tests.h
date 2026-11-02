#pragma once
#include "harness.h"
inline void urgent_tuning_window(certification::Assets& assets,int options,bool negate,bool trace) {
  using namespace certification;
  const bool historical=(options>>16)==1;
  TestDuel d(assets,0,0,trace,options);
  d.add({urgent_tuning,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({psychic_commander,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({jerry_beans,0,0,LOCATION_MZONE,1,POS_FACEUP_ATTACK});
  d.add({goyo,0,0,LOCATION_EXTRA,0,POS_FACEDOWN_DEFENSE});
  d.add({judgment,1,1,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({torrential,1,1,LOCATION_SZONE,1,POS_FACEDOWN_DEFENSE});
  int confirmed=0,resolving=0,ends=0,offers=0,materials=0,summons=0,paid=0,negated=0;
  bool started=false,tt=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) d.respond(e.player==0?7:6);
    else if(e.kind==MSG_SELECT_BATTLECMD) {
      require(e.player==1&&started&&ends==(negate?1:2)&&offers>=1&&summons==int(!negate)&&
              materials==(negate?0:2)&&negated==int(negate)&&paid==(negate?4000:0)&&tt==!negate,"Incomplete Urgent Tuning scenario");
      require(d.lp(0)==8000&&d.lp(1)==(negate?4000:8000)&&
              d.count(0,LOCATION_MZONE)==(negate?2:0)&&d.count(0,LOCATION_EXTRA)==int(negate)&&
              d.count(0,LOCATION_GRAVE)==(negate?1:4)&&d.count(1,LOCATION_GRAVE)==1,"Wrong Urgent Tuning final state");
      std::cout<<"Observed: "<<(negate?"Judgment negated Trap activation; materials retained":historical?"Synchro inside resolution; no summon-negation window; Torrential afterwards":"Synchro after chain; Judgment offered and declined; Torrential afterwards")<<".\n";return true;
    } else if(e.kind==MSG_SELECT_CHAIN) {
      int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        auto c=e.cards[i];
        if(c.code==urgent_tuning) {
          if(!started&&d.phase()>=PHASE_BATTLE_START&&d.phase()<=PHASE_BATTLE) {started=true;pick=i;}
        } else if(c.code==judgment) {
          require((started&&confirmed==1&&!summons&&!ends)||(tt&&ends==1)||(!historical&&!negate&&ends==1&&!summons),"Judgment offered for summon inside resolution");
          ++offers;if(negate&&!ends) pick=i;
        } else if(c.code==torrential) {require(!negate&&summons==1&&ends==1,"Wrong Torrential timing");tt=true;pick=i;}
        else throw std::runtime_error("Unexpected Urgent response");
      } require(!e.forced,"Forced Urgent response");d.respond(pick);
    } else if(e.kind==MSG_CHAINING) {
      require(e.cards.at(0).code==(tt?torrential:e.chain==1?urgent_tuning:judgment),"Wrong Urgent chain");
    } else if(e.kind==MSG_CHAINED) confirmed=e.chain;
    else if(e.kind==MSG_CHAIN_SOLVING) resolving=e.chain;
    else if(e.kind==MSG_CHAIN_SOLVED) resolving=0;
    else if(e.kind==MSG_CHAIN_NEGATED) {require(negate&&resolving==2&&e.chain==1,"Wrong Trap negation");++negated;}
    else if(e.kind==MSG_CHAIN_END) {++ends;confirmed=resolving=0;}
    else if(e.kind==MSG_PAY_LPCOST) {require(negate&&e.player==1&&e.mask==4000&&!ends,"Wrong Judgment cost");paid+=e.mask;}
    else if(e.kind==MSG_SELECT_CARD) {
      require(!negate&&e.cards.size()==1&&
              (e.cards[0].code==goyo||e.cards[0].code==psychic_commander),"Wrong Synchro choice");d.respond({1,0});
    } else if(e.kind==MSG_SELECT_SUM) {
      require(!negate&&resolving==(historical?1:0)&&ends==int(!historical)&&e.mask==6&&e.mandatory.size()==1&&
              e.mandatory[0].code==psychic_commander&&e.cards.size()==1&&e.cards[0].code==jerry_beans,"Wrong Synchro sum");d.respond({2,0,0});
    } else if(e.kind==MSG_SELECT_PLACE) {require(resolving==(historical?1:0)&&ends==int(!historical)&&e.player==0,"Wrong Synchro place");d.respond({0,LOCATION_MZONE,0});}
    else if(e.kind==MSG_SELECT_POSITION) d.respond(POS_FACEUP_ATTACK);
    else if(e.kind==MSG_MOVE && (e.reason&REASON_SYNCHRO)) {
      require(resolving==(historical?1:0)&&ends==int(!historical)&&(e.from.code==psychic_commander||e.from.code==jerry_beans)&&e.to.location==LOCATION_GRAVE,"Wrong material timing");++materials;
    } else if(e.kind==MSG_SPSUMMONING) require(resolving==(historical?1:0)&&ends==int(!historical)&&materials==2&&e.cards.at(0).code==goyo,"Synchro outside resolution");
    else if(e.kind==MSG_SPSUMMONED) {require(resolving==(historical?1:0)&&ends==int(!historical)&&materials==2,"Wrong summon success");++summons;}
    else if(e.kind==MSG_SELECT_EFFECTYN||e.kind==MSG_SELECT_YESNO||e.kind==MSG_CHAIN_DISABLED)
      throw std::runtime_error("Unexpected Urgent event");
    return false;
  });
}
