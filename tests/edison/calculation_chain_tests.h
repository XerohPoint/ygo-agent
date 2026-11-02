#pragma once
#include "harness.h"

inline void single_calculation_chain(certification::Assets& assets, int options,
                                     bool historical, bool early_window, bool both, bool trace) {
  using namespace certification;
  const int uses=(historical||early_window)?(both?2:1):0;
  const uint32_t atk=3000+uses*3000;
  TestDuel d(assets,0,0,trace,options);
  for(uint8_t p:{0,1}) d.add({blue_eyes,p,p,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  for(int i=0;i<2;++i) d.add({honest,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  int turns=0,announced=0,confirmed=0,resolving=0,solved=0,costs=0,selected=0;
  bool passed=false,entered=false,requested=false,attacked=false,damage=false,battle=false,ended=false;
  bool early_seen=false,late_seen=false,second_response=false,chain_end=false,damage_seen=false;
  bool grave[2]={};
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {require(e.player==turns && turns<2,"Unexpected turn");++turns;}
    else if(e.kind==MSG_SELECT_IDLECMD) {
      if(e.player==0) {require(!passed && e.to_end,"Expected P0 pass");passed=true;d.respond(7);}
      else {require(passed && !entered && e.to_battle,"Expected P1 battle");entered=true;d.respond(6);}
    } else if(e.kind==MSG_SELECT_BATTLECMD) {
      require(e.player==1 && entered,"Unexpected battle command");
      if(!requested) {require(e.idle[0].size()==1 && e.idle[0][0].code==blue_eyes,"Expected attacker");requested=true;d.respond(1);}
      else {
        require(ended && early_seen && announced==uses && confirmed==uses && costs==uses && solved==uses &&
                selected==uses && chain_end==bool(uses) && second_response==bool(uses) &&
                damage_seen==bool(uses) && grave[0]==!uses && grave[1],"Incomplete single-chain checkpoints");
        require(early_window || late_seen==historical,"Wrong calculation availability");
        require(d.lp(0)==8000 && d.lp(1)==8000-uses*3000 && d.count(0,LOCATION_HAND)==2-uses &&
                d.count(0,LOCATION_GRAVE)==uses+int(!uses) && d.count(0,LOCATION_MZONE)==int(uses>0) &&
                d.count(1,LOCATION_GRAVE)==1 && d.code(1,LOCATION_GRAVE,0)==blue_eyes &&
                d.count(1,LOCATION_MZONE)==0,"Wrong final damage/cost/battle state");
        if(uses) require(d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==atk,"Wrong persistent ATK");
        for(int i=0;i<2-uses;++i) require(d.code(0,LOCATION_HAND,i)==honest,"Unused Honest missing");
        for(int i=0;i<uses;++i) require(d.code(0,LOCATION_GRAVE,i)==honest,"Honest cost missing");
        for(int p=0;p<2;++p) require(d.count(p,LOCATION_DECK)==0 && d.count(p,LOCATION_SZONE)==0 &&
                                     d.count(p,LOCATION_EXTRA)==0 && d.count(p,LOCATION_REMOVED)==0,"Unexpected other cards");
        require(d.count(1,LOCATION_HAND)==0,"Unexpected opposing hand");
        std::cout<<"Observed: "<<uses<<" Honest activation(s) in "<<int(uses>0)
                 <<" chain; "<<2-uses<<" unused copy/copies retained; no second calculation chain.\n";
        return true;
      }
    } else if(e.kind==MSG_ATTACK) {
      require(requested && !attacked && e.from.controller==1 && e.to.controller==0,"Wrong attack");attacked=true;
    } else if(e.kind==MSG_DAMAGE_STEP_START) {require(attacked && !damage,"Unexpected Damage Step");damage=true;}
    else if(e.kind==MSG_SELECT_CARD) {
      require(requested && !attacked && e.player==1 && e.minimum==1 && e.maximum==1 &&
              e.cards.size()==1 && e.cards[0].controller==0 && e.cards[0].code==blue_eyes,
              "Unexpected attack target");d.respond({1,0});
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");int pick=-1;
      for(const auto& c:e.cards) require(e.player==0 && c.code==honest && damage && !battle && !ended,
                                        "Unexpected Honest offer");
      if(!e.cards.empty()) {
        bool calc=d.phase()==PHASE_DAMAGE_CAL;
        require(d.phase()==PHASE_DAMAGE || (historical && calc),"Illegal modifier window");
        if(calc) {
          late_seen=true;
          require(!chain_end,"Second manual chain offered during Damage Calculation");
        } else early_seen=true;
        if(confirmed==1 && !chain_end) second_response=true;
        if(uses && !chain_end && (early_window?!calc:calc) && selected<uses) {
          require(selected==confirmed,"Repeated selection before confirmation");pick=0;++selected;
        }
      }
      d.respond(pick);
    } else if(e.kind==MSG_CHAINING) {
      require(!chain_end && selected==announced+1 && announced==confirmed && e.chain==announced+1 &&
              e.cards.at(0).code==honest && e.player==0 &&
              d.phase()==(early_window?PHASE_DAMAGE:PHASE_DAMAGE_CAL),"Wrong Honest chain/window");++announced;
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==honest) {
        require(announced==costs+1 && !resolving && e.from.controller==0 &&
                e.from.location==LOCATION_HAND && e.to.location==LOCATION_GRAVE &&
                (e.reason&REASON_COST),"Wrong Honest cost");++costs;
      } else {
        int p=e.from.controller;
        require(battle && p>=0 && p<=1 && !grave[p] && e.from.code==blue_eyes &&
                e.from.location==LOCATION_MZONE && e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_BATTLE|REASON_DESTROY))==(REASON_BATTLE|REASON_DESTROY),
                "Wrong battle destruction");grave[p]=true;
      }
    } else if(e.kind==MSG_CHAINED) {
      require(announced==confirmed+1 && costs==announced && e.chain==announced,"Wrong confirmation");++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed==uses && !resolving && e.chain==confirmed-solved,"Wrong reverse resolution");resolving=e.chain;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(e.chain==resolving && d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==uint32_t(3000+(solved+1)*3000),
              "Wrong cumulative Honest ATK");resolving=0;++solved;
    } else if(e.kind==MSG_CHAIN_END) {
      require(!chain_end && !resolving && solved==uses,"Unexpected second/incomplete chain");chain_end=true;
    } else if(e.kind==MSG_BATTLE) {
      require(damage && !battle && early_seen && (early_window || late_seen==historical) &&
              chain_end==bool(uses) && e.battle_stats[0]==3000 && e.battle_stats[2]==atk &&
              e.battle_destroyed[0] && e.battle_destroyed[1]==!uses,"Wrong calculation");battle=true;
    } else if(e.kind==MSG_DAMAGE) {
      require(battle && uses && !damage_seen && e.player==1 && e.mask==uint32_t(uses*3000),"Wrong damage");damage_seen=true;
    } else if(e.kind==MSG_DAMAGE_STEP_END) {require(battle && !ended,"Unexpected end");ended=true;}
    else if(e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_CHAIN_DISABLED ||
            e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_PAY_LPCOST || e.kind==MSG_DRAW)
      throw std::runtime_error("Unexpected single-calculation-chain event");
    return false;
  });
}
