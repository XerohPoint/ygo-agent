#pragma once
#include "harness.h"

inline void faceup_before_attack(certification::Assets& assets,int options,bool historical,bool jade,bool faceup,bool trace) {
  using namespace certification;
  const uint32_t actor=jade?jade_knight:cyber_phoenix,reward=jade?jade_knight:blue_eyes;
  const bool triggers=!historical||faceup;
  TestDuel d(assets,0,0,trace,options);
  d.add({actor,0,0,LOCATION_MZONE,0,uint8_t(faceup?POS_FACEUP_DEFENSE:POS_FACEDOWN_DEFENSE)});
  d.add({reward,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  bool passed=false,entered=false,requested=false,attacked=false,damage=false,battle=false,destroyed=false,ended=false;
  bool decided=false,announced=false,confirmed=false,resolving=false,solved=false,chain_end=false,added=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      if(e.player==0) {require(!passed,"Unexpected later turn");passed=true;d.respond(7);}
      else {require(passed && !entered && e.to_battle,"Battle unavailable");entered=true;d.respond(6);}
    } else if(e.kind==MSG_SELECT_BATTLECMD) {
      require(e.player==1,"Wrong battle player");
      if(!requested) {require(entered && e.idle[0].size()==1 && e.idle[0][0].code==blue_eyes,"Wrong attacker");requested=true;d.respond(1);}
      else {
        require(ended && destroyed && decided==triggers && chain_end==triggers && added==triggers,
                "Historical battle trigger requires face-up position before attack selection");
        require(d.count(0,LOCATION_MZONE)==0 && d.count(0,LOCATION_GRAVE)==1 &&
                d.code(0,LOCATION_GRAVE,0)==actor && d.count(0,LOCATION_HAND)==int(triggers) &&
                d.count(0,LOCATION_DECK)==int(!triggers) && d.code(0,triggers?LOCATION_HAND:LOCATION_DECK,0)==reward &&
                d.lp(0)==8000 && d.lp(1)==8000,"Wrong battle reward/final zones");
        std::cout<<"Observed: "<<(jade?"Jade Knight":"Cyber Phoenix")<<" attacked "
                 <<(faceup?"face-up":"face-down")<<"; "<<(triggers?"reward resolved":"no trigger")<<".\n";return true;
      }
    } else if(e.kind==MSG_SELECT_CARD) {
      require(e.minimum==1 && e.maximum==1 && e.cards.size()==1,"Wrong choice");
      if(!attacked) require(e.player==1 && requested && e.cards[0].code==actor,"Wrong attack target");
      else require(jade && triggers && resolving && e.player==0 && e.cards[0].code==reward &&
                   e.cards[0].location==LOCATION_DECK,"Wrong search selection");
      d.respond({1,0});
    } else if(e.kind==MSG_ATTACK) {require(requested && !attacked,"Wrong attack");attacked=true;}
    else if(e.kind==MSG_DAMAGE_STEP_START) {require(attacked && !damage,"Wrong Damage Step");damage=true;}
    else if(e.kind==MSG_BATTLE) {require(damage && !battle && e.battle_destroyed[1],"Wrong battle result");battle=true;}
    else if(e.kind==MSG_DAMAGE_STEP_END) {require(battle && destroyed && !ended,"Wrong Damage Step end");ended=true;}
    else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(triggers && destroyed && !decided && e.player==0 && e.cards.at(0).code==actor,"Wrong optional reward");
      decided=true;d.respond(1);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced choice");d.respond(-1);}
      else {
        require(triggers && destroyed && !decided && e.player==0 && !e.forced &&
                e.cards.size()==1 && e.cards[0].code==actor,"Wrong reward offer");decided=true;d.respond(0);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(decided && !announced && e.chain==1 && e.cards.at(0).code==actor,"Wrong reward activation");announced=true;
    } else if(e.kind==MSG_MOVE) {
      if(e.from.location==LOCATION_MZONE) {
        require(battle && !destroyed && e.from.code==actor && e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_BATTLE|REASON_DESTROY))==(REASON_BATTLE|REASON_DESTROY),"Wrong battle destruction");destroyed=true;
      } else {
        require(jade && resolving && !added && e.from.code==reward && e.from.location==LOCATION_DECK &&
                e.to.location==LOCATION_HAND,"Wrong search movement");added=true;
      }
    } else if(e.kind==MSG_DRAW) {
      require(!jade && resolving && !added && e.player==0 && e.count==1 &&
              (e.cards.at(0).code&0x7fffffff)==reward,"Wrong draw reward");added=true;
    } else if(e.kind==MSG_CHAINED) {require(announced && !confirmed && e.chain==1,"Wrong confirmation");confirmed=true;}
    else if(e.kind==MSG_CHAIN_SOLVING) {require(confirmed && !resolving && e.chain==1,"Wrong resolution");resolving=true;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving && added && e.chain==1,"Reward missing");resolving=false;solved=true;}
    else if(e.kind==MSG_CHAIN_END) {require(solved && !resolving,"Incomplete reward chain");chain_end=true;}
    else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_SELECT_YESNO ||
            e.kind==MSG_DAMAGE || e.kind==MSG_SPSUMMONED) throw std::runtime_error("Unexpected face-up battle event");
    return false;
  });
}
