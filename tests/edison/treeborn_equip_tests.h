#pragma once
#include "harness.h"
inline void treeborn_equipped_monster(certification::Assets& assets,int options,bool equip,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({treeborn,0,0,LOCATION_GRAVE,0,POS_FACEUP_ATTACK});
  d.add({jade_knight,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({gearframe,0,0,LOCATION_MZONE,1,POS_FACEUP_ATTACK});
  int turns=0,initial_offer=0,revival_offer=0,chains=0,resolving=0;
  bool requested=false,equipped=false,revived=false;
  d.start(options);
  auto tree_choice=[&]() {
    require(d.phase()==PHASE_STANDBY,"Treeborn outside Standby");
    if(turns==1) {++initial_offer;d.respond(0);}
    else {require(turns==3&&!equip&&!revival_offer,"Equipped monster must block Treeborn");++revival_offer;d.respond(1);}
  };
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) ++turns;
    else if(e.kind==MSG_SELECT_IDLECMD) {
      if(turns==1&&equip&&!requested) {
        require(e.idle[5].size()==1&&e.idle[5][0].code==gearframe,"Missing equip effect");requested=true;d.respond(5);
      } else if(turns<3) {if(equip&&turns==1) require(equipped,"Equip incomplete");d.respond(7);}
      else {
        require(turns==3&&initial_offer>=1&&revival_offer==int(!equip)&&revived==!equip&&equipped==equip&&chains==1&&
                d.count(0,LOCATION_SZONE)==int(equip)&&d.count(0,LOCATION_GRAVE)==int(equip)&&
                d.count(0,LOCATION_MZONE)==(equip?1:3)&&d.lp(0)==8000&&d.lp(1)==8000,"Wrong Treeborn equip exclusion");
        if(equip) require(d.code(0,LOCATION_SZONE,0)==gearframe,"Actual monster equip missing");
        else require(d.code(0,LOCATION_MZONE,2)==treeborn,"Treeborn not revived");
        std::cout<<"Observed: first Standby declined; next Standby "<<(equip?"actual Union equip blocked Treeborn":"unequipped monster allowed Treeborn revival")<<".\n";return true;
      }
    } else if(e.kind==MSG_SELECT_EFFECTYN) {require(e.cards.at(0).code==treeborn,"Wrong optional effect");tree_choice();}
    else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Forced Treeborn response");int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        if(e.cards[i].code==treeborn) {
          require(d.phase()==PHASE_STANDBY,"Wrong Treeborn phase");
          if(turns==1) ++initial_offer;
          else {require(turns==3&&!equip&&!revival_offer,"Equip must block Treeborn");++revival_offer;pick=i;}
        } else require(e.cards[i].code==gearframe,"Wrong response");
      }d.respond(pick);
    } else if(e.kind==MSG_CHAINING) {
      require(e.cards.at(0).code==(equip?gearframe:treeborn)&&e.chain==1,"Wrong equip/revival chain");++chains;
    } else if(e.kind==MSG_CHAIN_SOLVING) resolving=e.chain;
    else if(e.kind==MSG_CHAIN_SOLVED) resolving=0;
    else if(e.kind==MSG_SELECT_CARD) {
      require(equip&&turns==1&&!resolving,"Wrong equip target timing");int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) if(e.cards[i].code==jade_knight) pick=i;
      require(pick>=0,"Missing Machine target");d.respond({1,uint8_t(pick)});
    } else if(e.kind==MSG_SELECT_PLACE) {require(resolving==1,"Wrong equip/revival placement");d.respond({0,uint8_t(equip?LOCATION_SZONE:LOCATION_MZONE),uint8_t(equip?0:2)});}
    else if(e.kind==MSG_SELECT_POSITION) d.respond(POS_FACEUP_ATTACK);
    else if(e.kind==MSG_EQUIP) {require(equip&&resolving==1&&e.to.sequence==0,"Wrong equip relationship");equipped=true;}
    else if(e.kind==MSG_SPSUMMONED) {require(!equip&&turns==3&&resolving==1,"Wrong Treeborn revival");revived=true;}
    else if(e.kind==MSG_CHAIN_NEGATED||e.kind==MSG_CHAIN_DISABLED||e.kind==MSG_SELECT_YESNO)
      throw std::runtime_error("Unexpected equipped-Treeborn event");
    return false;
  });
}
