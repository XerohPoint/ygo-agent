#pragma once
#include "harness.h"

inline void mark_rose_phases(certification::Assets& assets,int options,bool historical,bool cold,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({mark_rose,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({jerry_beans,0,0,LOCATION_GRAVE,0,POS_FACEUP_ATTACK});
  d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  if(cold) d.add({cold_wave,1,1,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  int turns=0,chains=0,control_changes=0;bool requested=false,announced=false,confirmed=false,resolving=false,solved=false;
  bool cost=false,target=false,passed=false,cold_used=false;
  const bool regained=!historical||!cold;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {require(e.player==turns%2 && turns<3,"Wrong turn");++turns;}
    else if(e.kind==MSG_SELECT_IDLECMD) {
      if(turns==1 && !requested) {
        require(e.player==0 && e.idle[5].size()==1 && e.idle[5][0].code==mark_rose,"Mark unavailable");requested=true;d.respond(5);
      } else if(turns==1) {
        require(!passed && chains==1 && cost && target && control_changes==1 && d.code(0,LOCATION_MZONE,0)==blue_eyes,
                "Initial control incomplete");passed=true;d.respond(7);
      } else if(turns==2) {
        require(control_changes==2 && d.code(1,LOCATION_MZONE,0)==blue_eyes,"End Phase control return missing");
        if(cold&&!cold_used) {
          require(e.idle[5].size()==1 && e.idle[5][0].code==cold_wave,"Cold Wave must be first Main Phase action");
          cold_used=true;d.respond(5);
        } else d.respond(7);
      } else {
        require(passed && turns==3 && e.player==0 && chains==2+int(cold)+int(historical&&!cold) &&
                control_changes==2+int(regained),"Historical Standby control return must start a chain and respect Cold Wave");
        require(d.count(0,LOCATION_MZONE)==int(regained) && d.count(1,LOCATION_MZONE)==int(!regained) &&
                d.code(regained?0:1,LOCATION_MZONE,0)==blue_eyes && d.count(0,LOCATION_REMOVED)==1 &&
                d.code(0,LOCATION_REMOVED,0)==jerry_beans && d.count(0,LOCATION_GRAVE)==0 &&
                d.code(0,LOCATION_SZONE,0)==mark_rose && d.lp(0)==8000 && d.lp(1)==8000,"Wrong final control/cost state");
        std::cout<<"Observed: End Phase return chained; Standby "<<(historical?"trigger":"automatic")
                 <<(regained?" regained control":" blocked by Cold Wave")<<".\n";return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced choice");d.respond(-1);}
      else {
        require(e.forced && e.player==0 && e.cards.size()==1 && e.cards[0].code==mark_rose &&
                (d.phase()==PHASE_END || (historical&&!cold&&d.phase()==PHASE_STANDBY)),"Wrong phase trigger");
        d.respond(0);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(!announced && e.chain==1 && e.cards.at(0).code==(turns==2?cold_wave:mark_rose),"Wrong phase chain");
      if(turns==3) require(historical&&!cold && d.phase()==PHASE_STANDBY,"Wrong Standby activation");announced=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(turns==1 && chains==0 && announced && !confirmed && e.player==0 &&
              e.minimum==1 && e.maximum==1 && e.cards.size()==1,"Wrong activation choice");
      if(!cost) require(e.cards[0].code==jerry_beans && e.cards[0].location==LOCATION_GRAVE,"Wrong Plant cost");
      else {require(!target && e.cards[0].code==blue_eyes,"Wrong equip target");target=true;}
      d.respond({1,0});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==jerry_beans) {
        require(!cost && announced && !confirmed && e.from.location==LOCATION_GRAVE &&
                e.to.location==LOCATION_REMOVED && (e.reason&REASON_COST),"Wrong Plant banish");cost=true;
      } else if(e.from.code==blue_eyes) {
        require(e.from.location==LOCATION_MZONE && e.to.location==LOCATION_MZONE &&
                e.to.controller==(control_changes==1?1:0),"Wrong control movement");
        if(control_changes==2 && historical) require(!cold && announced && confirmed && (resolving||solved) && turns==3,
                                                    "Historical regain requires a resolved Standby trigger");
        ++control_changes;
      } else require(cold && e.from.code==cold_wave && e.to.location==LOCATION_GRAVE,"Unexpected movement");
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(e.count==1,"Wrong control placement");
      const int controller=control_changes==1?1:0;
      d.respond({uint8_t(controller),LOCATION_MZONE,0});
    } else if(e.kind==MSG_CHAINED) {require(announced && !confirmed && e.chain==1,"Wrong confirmation");confirmed=true;}
    else if(e.kind==MSG_CHAIN_SOLVING) {require(confirmed && !resolving && e.chain==1,"Wrong resolution");resolving=true;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving && e.chain==1,"Wrong solved link");resolving=false;solved=true;}
    else if(e.kind==MSG_CHAIN_END) {require(solved && !resolving,"Incomplete chain");++chains;announced=confirmed=solved=false;}
    else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_SELECT_YESNO ||
            e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_DRAW) throw std::runtime_error("Unexpected Mark of the Rose event");
    return false;
  });
}
