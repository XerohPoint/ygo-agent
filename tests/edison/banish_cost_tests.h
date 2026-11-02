#pragma once
#include "harness.h"

// mode: 0 applied Chalice, 1 Skill Drain, 2 unnegated accept, 3 decline.
// Chalice is applied on the attacking player's turn so its End Phase reset
// cannot explain the result.
inline void banish_cost_negation(certification::Assets& assets, int options, int mode, bool trace) {
  using namespace certification;
  const bool active=mode<2, drain=mode==1, accept=mode!=3, disabled=mode==0;
  const uint32_t negator=drain?skill_drain:chalice;
  TestDuel d(assets,0,0,trace,options);
  d.add({cyber_valley,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({blue_eyes,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  d.add({negator,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  int turns=0;bool passed=false,entered=false,requested=false,attacked=false;
  bool ready=!active,negator_requested=false,target=false,paid=false,decided=false;
  bool cost=false,drawn=false,negated=false,trigger_ended=false,replay_declined=false;
  bool calculated=false,destroyed=false,damaged=false,damage_ended=false;
  bool announced=false,confirmed=false,resolving=false,solved=false;
  auto decide=[&] {
    require(ready && attacked && !decided && !calculated,"Valley choice outside attack declaration");decided=true;
  };
  auto finish=[&] {
    require(attacked && decided && cost==accept && drawn==(accept && !disabled) &&
            negated==disabled && trigger_ended==accept && paid==drain,
            "Banish-cost trigger outcome incomplete");
    require(calculated==!accept && destroyed==!accept && damaged==!accept && damage_ended==!accept,
            "Unexpected damage calculation after removing attack target");
    require(replay_declined==disabled,"Only negated Valley should require declining the attack replay");
    require(d.lp(0)==(drain?7000:accept?8000:5000) && d.lp(1)==8000 &&
            d.count(0,LOCATION_MZONE)==0 && d.count(0,LOCATION_REMOVED)==int(accept) &&
            d.code(0,accept?LOCATION_REMOVED:LOCATION_GRAVE,0)==cyber_valley &&
            d.count(0,LOCATION_HAND)==int(drawn) && d.count(0,LOCATION_DECK)==int(!drawn),
            "Wrong banishment, draw, or LP outcome");
    require(d.code(0,drawn?LOCATION_HAND:LOCATION_DECK,0)==blue_eyes &&
            d.count(1,LOCATION_MZONE)==1 && d.code(1,LOCATION_MZONE,0)==blue_eyes,
            "Unexpected filler/attacker identity");
    require(d.count(0,LOCATION_GRAVE)==int(disabled)+int(!accept) &&
            d.count(0,LOCATION_SZONE)==int(!disabled),"Unexpected negator destination");
    if(disabled) require(d.code(0,LOCATION_GRAVE,0)==chalice,"Chalice missing");
    else require(d.code(0,LOCATION_SZONE,0)==negator &&
                 (drain?bool(d.position(0,LOCATION_SZONE,0)&POS_FACEUP):
                        bool(d.position(0,LOCATION_SZONE,0)&POS_FACEDOWN)),"Negator state changed");
    for(int loc:{LOCATION_HAND,LOCATION_DECK,LOCATION_GRAVE,LOCATION_REMOVED,LOCATION_SZONE,LOCATION_EXTRA})
      require(d.count(1,loc)==0,"Unexpected opposing zone");
    require(d.count(0,LOCATION_EXTRA)==0,"Unexpected Extra Deck");
    std::cout<<"Observed: "<<(!accept?"Valley declined; battle dealt 3000 and destroyed it":
                  disabled?"Valley banished for cost; applied Chalice negated draw/end-Battle-Phase effect":
                           "Valley banished for cost, drew once, and ended the Battle Phase")<<".\n";
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {require(e.player==turns && turns<2,"Unexpected turn");++turns;}
    else if(e.kind==MSG_SELECT_IDLECMD) {
      if(e.player==0) {
        require(turns==1 && !passed && e.to_end && (!drain || ready),"P0 setup incomplete");passed=true;d.respond(7);
      } else if(!entered) {
        require(turns==2 && passed && ready && e.to_battle,"P1 must enter battle after negator setup");
        require(d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==(disabled?400u:0u),
                "Chalice must still be applied on the attacking turn");
        entered=true;d.respond(6);
      } else {
        require(accept && !disabled && d.phase()==PHASE_MAIN2,"Valley's successful effect must end Battle Phase");
        finish();return true;
      }
    } else if(e.kind==MSG_SELECT_BATTLECMD) {
      require(e.player==1 && entered,"Wrong battle player");
      if(!requested) {
        require(e.idle[0].size()==1 && e.idle[0][0].code==blue_eyes,"Expected Blue-Eyes attacker");
        requested=true;d.respond(1);
      } else {
        require(disabled || !accept,"Successful Valley should end the Battle Phase");
        finish();return true;
      }
    } else if(e.kind==MSG_ATTACK) {
      require(requested && !attacked && e.from.controller==1 && e.to.controller==0,"Unexpected attack");attacked=true;
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(e.player==0 && e.cards.size()==1 && e.cards[0].code==cyber_valley,"Unexpected optional effect");
      decide();d.respond(accept?1:0);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        require(e.player==0,"Unexpected response player");
        if(e.cards[i].code==negator) {
          if(!ready && !negator_requested && turns==(drain?1:2)) {negator_requested=true;pick=int(i);}
        } else {
          require(e.cards[i].code==cyber_valley,"Unexpected response candidate");decide();
          if(accept) pick=int(i);
        }
      }
      d.respond(pick);
    } else if(e.kind==MSG_CHAINING) {
      require(!announced && e.player==0 && e.chain==1 &&
              e.cards.at(0).code==(ready?cyber_valley:negator) &&
              (ready?decided && accept:negator_requested),"Unexpected chain");announced=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(e.minimum==1 && e.maximum==1,"Unexpected selection");
      if(!ready) {
        require(!drain && announced && !confirmed && !target && e.player==0,"Unexpected Chalice target");
        int index=-1;for(size_t i=0;i<e.cards.size();++i)
          if(e.cards[i].code==cyber_valley && e.cards[i].controller==0) index=int(i);
        require(index>=0,"Valley target missing");target=true;d.respond({1,static_cast<byte>(index)});
      } else {
        require(requested && !attacked && e.player==1 && e.cards.size()==1 &&
                e.cards[0].code==cyber_valley && e.cards[0].controller==0,"Unexpected attack target");
        d.respond({1,0});
      }
    } else if(e.kind==MSG_SELECT_YESNO) {
      require(disabled && trigger_ended && e.player==1 && !replay_declined,"Unexpected attack replay");
      replay_declined=true;d.respond(0);
    } else if(e.kind==MSG_PAY_LPCOST) {
      require(!ready && drain && !paid && e.player==0 && e.mask==1000,"Wrong Drain cost");paid=true;
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==cyber_valley && accept) {
        require(ready && announced && !confirmed && !cost && e.from.location==LOCATION_MZONE &&
                e.to.location==LOCATION_REMOVED && (e.reason&REASON_COST) &&
                !(e.reason&REASON_EFFECT),"Valley must banish itself for cost before confirmation");cost=true;
      } else if(e.from.code==cyber_valley) {
        require(calculated && damaged && !destroyed && e.from.location==LOCATION_MZONE &&
                e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_BATTLE|REASON_DESTROY))==(REASON_BATTLE|REASON_DESTROY),"Wrong battle destruction");destroyed=true;
      } else require(disabled && e.from.code==chalice && e.from.location==LOCATION_SZONE &&
                     e.to.location==LOCATION_GRAVE,"Unexpected movement");
    } else if(e.kind==MSG_CHAINED) {
      require(announced && !confirmed && e.chain==1 && (ready?cost:drain?paid:target),"Cost/target not complete");confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && !solved && e.chain==1,"Unexpected resolution");resolving=true;
    } else if(e.kind==MSG_CHAIN_DISABLED) {
      require(ready && disabled && resolving && !negated && e.chain==1,"Unexpected negation");negated=true;
    } else if(e.kind==MSG_DRAW) {
      require(ready && accept && !disabled && resolving && !drawn && e.player==0 && e.count==1 &&
              (e.cards.at(0).code&0x7fffffffu)==blue_eyes,"Wrong Valley draw");drawn=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving && !solved && e.chain==1,"Unexpected solved link");resolving=false;solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved && !resolving,"Incomplete chain");
      if(!ready) {ready=true;announced=confirmed=solved=false;}
      else trigger_ended=true;
    } else if(e.kind==MSG_BATTLE) {
      require(!accept && decided && !calculated && e.battle_stats[0]==3000 &&
              e.battle_stats[2]==0 && e.battle_destroyed[1],"Unexpected calculation");calculated=true;
    } else if(e.kind==MSG_DAMAGE) {
      require(!accept && calculated && !damaged && e.player==0 && e.mask==3000,"Wrong damage");damaged=true;
    } else if(e.kind==MSG_DAMAGE_STEP_END) {
      require(!accept && destroyed && !damage_ended,"Unexpected Damage Step end");damage_ended=true;
    } else if(e.kind==MSG_DAMAGE_STEP_START) {
      require(!accept && decided,"Banish-cost effect must occur before Damage Step");
    } else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_SELECT_PLACE || e.kind==MSG_SELECT_POSITION)
      throw std::runtime_error("Unexpected action in banish-cost fixture");
    return false;
  });
}
