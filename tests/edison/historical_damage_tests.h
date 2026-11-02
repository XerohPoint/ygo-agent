#pragma once
#include "harness.h"

// mode 0: accept before calculation; 1: wait for calculation and accept if
// historical; 2: decline throughout, requiring the historical late offer.
inline void historical_damage_modifier(certification::Assets& assets, int options,
                                       bool historical, bool blackwing, int mode, bool trace) {
  using namespace certification;
  const uint32_t modifier=blackwing?kalut:honest, defender=blackwing?bora:blue_eyes;
  const bool activate=mode==0 || (historical && mode==1);
  const uint32_t final_attack=(blackwing?1700u:3000u)+(activate?(blackwing?1400u:3000u):0u);
  const bool defender_dies=final_attack<=3000,attacker_dies=final_attack>=3000;
  TestDuel d(assets,0,0,trace,options);
  d.add({defender,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({modifier,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  int turns=0;bool passed=false,entered=false,requested=false,attacked=false,damage=false,battle=false,ended=false;
  bool early=false,late=false,chosen=false,cost=false,announced=false,confirmed=false,resolving=false,solved=false,chain_end=false;
  bool grave[2]={};int damage_count=0;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {require(e.player==turns && turns<2,"Unexpected turn");++turns;}
    else if(e.kind==MSG_SELECT_IDLECMD) {
      if(e.player==0) {require(turns==1 && !passed && e.to_end,"P0 must pass");passed=true;d.respond(7);}
      else {require(turns==2 && passed && !entered && e.to_battle,"P1 must enter battle");entered=true;d.respond(6);}
    } else if(e.kind==MSG_SELECT_BATTLECMD) {
      require(e.player==1 && entered,"Unexpected battle command");
      if(!requested) {
        require(e.idle[0].size()==1 && e.idle[0][0].code==blue_eyes,"Expected attacker");requested=true;d.respond(1);
      } else {
        require(attacked && damage && battle && ended && early && chosen==activate && cost==activate &&
                chain_end==activate && grave[0]==defender_dies && grave[1]==attacker_dies &&
                damage_count==int(final_attack!=3000),"Incomplete modifier outcome");
        require(d.lp(0)==8000-int(final_attack<3000?3000-final_attack:0) &&
                d.lp(1)==8000-int(final_attack>3000?final_attack-3000:0),"Wrong battle damage");
        require(d.count(0,LOCATION_HAND)==int(!activate) && d.count(0,LOCATION_GRAVE)==int(activate)+int(defender_dies) &&
                d.count(0,LOCATION_MZONE)==int(!defender_dies) &&
                d.count(1,LOCATION_MZONE)==int(!attacker_dies) && d.count(1,LOCATION_GRAVE)==int(attacker_dies),
                "Wrong final cost/battle zones");
        if(activate) require(d.code(0,LOCATION_GRAVE,0)==modifier,"Hand cost not retained");
        else require(d.code(0,LOCATION_HAND,0)==modifier,"Unused modifier must remain in hand");
        if(defender_dies) require(d.code(0,LOCATION_GRAVE,int(activate))==defender,"Missing destroyed defender");
        else require(d.code(0,LOCATION_MZONE,0)==defender &&
                     d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==final_attack,"Modifier must persist after calculation");
        require(d.code(1,attacker_dies?LOCATION_GRAVE:LOCATION_MZONE,0)==blue_eyes,"Wrong attacker destination");
        for(int p=0;p<2;++p) require(d.count(p,LOCATION_SZONE)==0 && d.count(p,LOCATION_DECK)==0 &&
                                     d.count(p,LOCATION_REMOVED)==0 && d.count(p,LOCATION_EXTRA)==0,
                                     "Unexpected other zones");
        require(d.count(1,LOCATION_HAND)==0,"Unexpected opposing hand");
        std::cout<<"Observed: "<<(blackwing?"Kalut":"Honest")<<" early offer; calculation offer "
                 <<(late?"present":"absent")<<"; "
                 <<(activate?(mode==0?"activated before calculation":"activated during calculation"):"not activated")
                 <<"; final defender ATK "<<final_attack<<".\n";
        return true;
      }
    } else if(e.kind==MSG_ATTACK) {
      require(requested && !attacked && e.from.controller==1 && e.to.controller==0,"Wrong attack");attacked=true;
    } else if(e.kind==MSG_DAMAGE_STEP_START) {
      require(attacked && !damage,"Unexpected Damage Step");damage=true;
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced prompt");int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        require(e.player==0 && e.cards[i].code==modifier && damage && !ended && !battle &&
                (d.phase()==PHASE_DAMAGE || (historical && d.phase()==PHASE_DAMAGE_CAL)),
                "Hand modifier offered outside its legal Damage Step window");
        if(d.phase()==PHASE_DAMAGE_CAL) late=true;else early=true;
        if(!chosen && activate && ((mode==0 && d.phase()==PHASE_DAMAGE) ||
                                  (mode==1 && d.phase()==PHASE_DAMAGE_CAL))) {chosen=true;pick=int(i);}
      }
      d.respond(pick);
    } else if(e.kind==MSG_SELECT_CARD) {
      require(requested && !attacked && e.player==1 && e.minimum==1 && e.maximum==1 &&
              e.cards.size()==1 && e.cards[0].code==defender,"Expected sole attack target");d.respond({1,0});
    } else if(e.kind==MSG_CHAINING) {
      require(chosen && !announced && e.chain==1 && e.player==0 && e.cards.at(0).code==modifier &&
              d.phase()==(mode==0?PHASE_DAMAGE:PHASE_DAMAGE_CAL),"Modifier activated in wrong window");announced=true;
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==modifier) {
        require(announced && !confirmed && !cost && e.from.controller==0 && e.from.location==LOCATION_HAND &&
                e.to.location==LOCATION_GRAVE && (e.reason&REASON_COST) && !(e.reason&REASON_EFFECT),
                "Modifier must leave hand as cost");cost=true;
      } else {
        int p=e.from.controller;require(battle && p>=0 && p<=1 && !grave[p] &&
                e.from.code==(p==0?defender:blue_eyes) && e.from.location==LOCATION_MZONE &&
                e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_BATTLE|REASON_DESTROY))==(REASON_BATTLE|REASON_DESTROY),
                "Unexpected battle destruction");grave[p]=true;
      }
    } else if(e.kind==MSG_CHAINED) {
      require(announced && cost && !confirmed && e.chain==1,"Cost must precede confirmation");confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && !solved && e.chain==1,"Wrong resolution");resolving=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving && !solved && e.chain==1 &&
              d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==final_attack,"Wrong ATK modification");resolving=false;solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved && !resolving && !chain_end,"Incomplete modifier chain");chain_end=true;
    } else if(e.kind==MSG_BATTLE) {
      require(damage && !battle && early,"Missing early modifier window");
      require(mode==0 || late==historical,"Historical modifier missing during Damage Calculation");
      require(chain_end==activate && e.battle_stats[0]==3000 && e.battle_stats[2]==final_attack &&
              e.battle_destroyed[0]==attacker_dies && e.battle_destroyed[1]==defender_dies,
              "Wrong battle calculation after modifier");battle=true;
    } else if(e.kind==MSG_DAMAGE) {
      require(battle && damage_count==0 && final_attack!=3000 &&
              e.player==(final_attack>3000?1:0) && e.mask==(final_attack>3000?final_attack-3000:3000-final_attack),
              "Wrong battle damage event");++damage_count;
    } else if(e.kind==MSG_DAMAGE_STEP_END) {
      require(battle && !ended,"Unexpected Damage Step end");ended=true;
    } else if(e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_CHAIN_DISABLED ||
              e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_PAY_LPCOST || e.kind==MSG_DRAW)
      throw std::runtime_error("Unexpected event in historical modifier fixture");
    return false;
  });
}
