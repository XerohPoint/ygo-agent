#pragma once
#include "harness.h"

inline void armory_departed_victim(certification::Assets& assets,int options,bool historical,bool revive,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({psychic_commander,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({catapult_turtle,0,0,LOCATION_MZONE,1,POS_FACEUP_ATTACK});
  d.add({armory_arm,0,0,LOCATION_MZONE,2,POS_FACEUP_ATTACK});
  d.add({colossal,0,0,LOCATION_EXTRA,0,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  int stage=0,materials=0,summons=0,chains=0,announced=0,confirmed=0,resolving=0,solved=0,burn=0;
  bool tuner=false,sum=false,equipped=false,attacked=false,destroyed=false,decision=false,battle_damage=false,damage_end=false;
  const int expected_burn=historical||!revive?2800:0;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      if(e.player==0 && stage==0) {
        int pick=-1;for(size_t i=0;i<e.idle[1].size();++i) if(e.idle[1][i].code==colossal) pick=i;
        require(pick>=0,"Colossal Synchro unavailable");stage=1;d.respond((pick<<16)|1);
      } else if(e.player==0 && stage==1) {
        require(summons==1 && materials==2,"Actual Synchro setup incomplete");
        int pick=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==armory_arm) pick=i;
        require(pick>=0,"Armory equip unavailable");stage=2;d.respond((pick<<16)|5);
      } else if(e.player==0) {
        require(stage==2 && equipped && chains==1,"Equip incomplete");stage=3;d.respond(7);
      } else {require(stage==3 && e.to_battle,"Battle unavailable");stage=4;d.respond(6);}
    } else if(e.kind==MSG_SELECT_BATTLECMD) {
      require(e.player==1,"Wrong battle player");
      if(stage==4) {require(e.idle[0].size()==1 && e.idle[0][0].code==blue_eyes,"Wrong attacker");stage=5;d.respond(1);}
      else {
        require(stage==5 && damage_end && chains==2 && destroyed && decision && battle_damage &&
                summons==1+int(revive) && burn==expected_burn,"Incomplete Armory/Colossal sequence");
        require(d.lp(0)==6800 && d.lp(1)==8000-expected_burn &&
                d.count(0,LOCATION_MZONE)==int(revive) && d.count(0,LOCATION_GRAVE)==2+int(!revive) &&
                d.count(0,LOCATION_SZONE)==1 && d.code(0,LOCATION_SZONE,0)==armory_arm &&
                d.query_value(1,LOCATION_MZONE,0,QUERY_ATTACK)==4000,"Wrong final Armory state");
        std::cout<<"Observed: properly Synchro Summoned Colossal "<<(revive?"revived at CL2":"remained in Graveyard")
                 <<"; Armory CL1 inflicted "<<burn<<".\n";return true;
      }
    } else if(e.kind==MSG_SELECT_SUM) {
      require(stage==1 && tuner && !sum && e.mask==8 && e.mandatory.size()==1 &&
              e.mandatory[0].code==psychic_commander && e.mandatory[0].sum_value==3,"Wrong Synchro sum");
      int pick=-1;for(size_t i=0;i<e.cards.size();++i) if(e.cards[i].code==catapult_turtle && e.cards[i].sum_value==5) pick=i;
      require(pick>=0,"Missing Level 5 material");sum=true;d.respond({2,0,uint8_t(pick)});
    } else if(e.kind==MSG_SELECT_CARD) {
      require(e.minimum==1 && e.maximum==1,"Wrong selection count");
      uint32_t want=0;
      if(stage==1) {require(!tuner,"Repeated tuner choice");want=psychic_commander;tuner=true;}
      else if(stage==2) {require(announced==1 && !confirmed,"Wrong equip target timing");want=blue_eyes;}
      else if(!attacked) want=colossal;
      else {require(revive && announced==2 && confirmed==1,"Wrong revival target timing");want=colossal;}
      int pick=-1;for(size_t i=0;i<e.cards.size();++i) if(e.cards[i].code==want) pick=i;
      require(pick>=0,"Missing selected card");d.respond({1,uint8_t(pick)});
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced choice");d.respond(-1);}
      else if(stage==5 && destroyed && e.cards[0].code==colossal) {
        require(!decision && e.player==0 && !e.forced && e.cards.size()==1,"Wrong revival decision");
        decision=true;d.respond(revive?0:-1);
      } else {
        require(!e.forced && stage==1 && e.player==0,"Unexpected response");
        for(auto& c:e.cards) require(c.code==armory_arm,"Wrong ignition-priority offer");d.respond(-1);
      }
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(stage==5 && destroyed && !decision && e.player==0 && e.cards.at(0).code==colossal,"Wrong optional revival");
      decision=true;d.respond(revive?1:0);
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==psychic_commander || e.from.code==catapult_turtle) {
        require(stage==1 && sum && e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_MATERIAL|REASON_SYNCHRO))==(REASON_MATERIAL|REASON_SYNCHRO),"Wrong Synchro material");++materials;
      } else if(e.from.code==armory_arm) {
        require(stage==2 && resolving==1 && e.from.location==LOCATION_MZONE && e.to.location==LOCATION_SZONE,"Wrong equip move");equipped=true;
      } else {
        require(e.from.code==colossal,"Unexpected card movement");
        if(e.to.location==LOCATION_GRAVE) {
          require(stage==5 && attacked && !destroyed && (e.reason&REASON_BATTLE),"Wrong battle destruction");destroyed=true;
        } else require(e.to.location==LOCATION_MZONE &&
                       (stage==1?e.from.location==LOCATION_EXTRA:
                        revive && resolving==2 && e.from.location==LOCATION_GRAVE),"Wrong Colossal summon");
      }
    } else if(e.kind==MSG_EQUIP) {
      require(stage==2 && resolving==1 && e.from.controller==0 && e.from.location==LOCATION_SZONE &&
              e.to.controller==1 && e.to.location==LOCATION_MZONE && e.to.sequence==0,"Wrong equip relationship");
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(e.player==0 && e.count==1,"Wrong placement");
      if(stage==2) {require(resolving==1,"Wrong equip placement");d.respond({0,LOCATION_SZONE,0});}
      else {require(stage==1 || (stage==5 && revive && resolving==2),"Wrong monster placement");d.respond({0,LOCATION_MZONE,0});}
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(e.cards.at(0).code==colossal && (e.mask&POS_FACEUP_ATTACK),"Wrong summon position");d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONED) {require(stage==1 || (stage==5 && revive && resolving==2),"Wrong summon success");++summons;}
    else if(e.kind==MSG_ATTACK) {require(stage==5 && !attacked,"Wrong attack");attacked=true;}
    else if(e.kind==MSG_DAMAGE) {
      if(e.player==0) {require(stage==5 && attacked && !battle_damage && e.mask==1200,"Wrong battle damage");battle_damage=true;}
      else {require(chains==1 && resolving==1 && !burn && e.mask==uint32_t(expected_burn),"Wrong Armory damage");burn=e.mask;}
    } else if(e.kind==MSG_DAMAGE_STEP_END) {require(destroyed && chains==2,"Wrong Damage Step completion");damage_end=true;}
    else if(e.kind==MSG_CHAINING) {
      require(e.chain==announced+1 && announced==confirmed &&
              e.cards.at(0).code==(announced?colossal:armory_arm),"Wrong Armory/Colossal chain");++announced;
    } else if(e.kind==MSG_CHAINED) {require(e.chain==announced && confirmed+1==announced,"Wrong confirmation");++confirmed;}
    else if(e.kind==MSG_CHAIN_SOLVING) {require(!resolving && e.chain==confirmed-solved,"Wrong reverse resolution");resolving=e.chain;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving==e.chain,"Wrong solved link");resolving=0;++solved;}
    else if(e.kind==MSG_CHAIN_END) {
      require(!resolving && solved==(chains==0?1:1+int(revive)),"Incomplete chain");
      ++chains;announced=confirmed=solved=0;
    } else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_DRAW || e.kind==MSG_SELECT_YESNO)
      throw std::runtime_error("Unexpected Armory event");
    return false;
  });
}
