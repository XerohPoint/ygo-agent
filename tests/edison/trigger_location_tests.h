#pragma once
#include "harness.h"

inline void trigger_location(certification::Assets& assets, int options, bool historical, bool use_crow, bool trace) {
  using namespace certification;
  const bool search=historical||!use_crow;
  TestDuel d(assets,0,0,trace,options);
  d.add({sangan,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({enemy_controller,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({dupe,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  d.add({jerry_beans,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({dd_crow,1,1,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  bool requested=false,setup=false,ended=false,option=false,tribute_choice=false,tributed=false;
  bool control_target=false,controlled=false,crow_offered=false,crow_chosen=false,crow_cost=false,crow_target=false;
  bool banished=false,search_choice=false,added=false;
  int announced=0,confirmed=0,resolving=0,solved=0;
  auto count=[&] { return setup?1:1+int(use_crow); };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Unexpected idle");
      if(requested) {
        require(setup && ended==search && tributed && controlled && crow_offered && crow_chosen==use_crow &&
                crow_cost==use_crow && crow_target==use_crow && banished==use_crow &&
                search_choice==search && added==search,"Incomplete trigger-location checkpoints");
        require(d.count(0,LOCATION_MZONE)==1 && d.code(0,LOCATION_MZONE,0)==jerry_beans &&
                d.count(1,LOCATION_MZONE)==0 && d.count(0,LOCATION_HAND)==int(search) &&
                d.count(0,LOCATION_DECK)==int(!search) &&
                d.code(0,search?LOCATION_HAND:LOCATION_DECK,0)==dupe,"Wrong final control/search outcome");
        require(d.count(0,LOCATION_GRAVE)==(use_crow?1:2) &&
                d.code(0,LOCATION_GRAVE,use_crow?0:1)==enemy_controller &&
                d.count(0,LOCATION_REMOVED)==int(use_crow) &&
                d.code(0,use_crow?LOCATION_REMOVED:LOCATION_GRAVE,0)==sangan,
                "Wrong final Sangan/Enemy Controller locations");
        require(d.count(1,LOCATION_HAND)==int(!use_crow) && d.count(1,LOCATION_GRAVE)==int(use_crow) &&
                d.code(1,use_crow?LOCATION_GRAVE:LOCATION_HAND,0)==dd_crow,"Crow cost not retained");
        for(int p=0;p<2;++p) require(d.lp(p)==8000 && d.count(p,LOCATION_SZONE)==0 &&
                                     d.count(p,LOCATION_EXTRA)==0,"Unexpected final state");
        require(d.count(1,LOCATION_REMOVED)==0 && d.count(1,LOCATION_DECK)==0,"Unexpected opposing zones");
        std::cout<<"Observed: Sangan "<<(use_crow?"banished before trigger window":"remained in Graveyard")
                 <<"; "<<(search?"search activated on a new chain and resolved":"no trigger offered under Rule 5")<<".\n";
        return true;
      }
      int index=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==enemy_controller) index=int(i);
      require(index>=0,"Enemy Controller unavailable");requested=true;d.respond((index<<16)|5);
    } else if(e.kind==MSG_SELECT_OPTION) {
      require(!setup && announced==1 && !option && e.player==0 &&
              e.options==std::vector<uint32_t>{enemy_controller*16,enemy_controller*16+1},"Wrong mode prompt");
      option=true;d.respond(1);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");int index=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        const auto code=e.cards[i].code;
        if(code==enemy_controller) require(!requested && e.player==0,"Unexpected Enemy Controller response");
        else {
          require(code==dd_crow && e.player==1,"Unexpected chain candidate");
          if(!setup && confirmed==1 && !resolving && !crow_chosen) {
            crow_offered=true;
            if(use_crow) {index=int(i);crow_chosen=true;}
          }
        }
      }
      d.respond(index);
    } else if(e.kind==MSG_CHAINING) {
      require(announced==confirmed && e.chain==announced+1 && announced<count(),"Wrong chain index");
      if(setup) {
        require(search && !ended && e.player==0 && e.cards.at(0).code==sangan &&
                controlled && banished==use_crow,"Unexpected or wrong-location Sangan trigger");
      } else require(e.cards.at(0).code==(announced==0?enemy_controller:dd_crow) &&
                     e.player==(announced==0?0:1),"Wrong setup chain order");
      ++announced;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(e.minimum==1 && e.maximum==1,"Unexpected selection size");
      uint32_t want;int player,loc;
      if(setup) {
        require(search && resolving==1 && !search_choice && e.player==0,"Unexpected search choice");
        search_choice=true;want=dupe;player=0;loc=LOCATION_DECK;
      } else if(announced==1) {
        require(option && e.player==0,"Wrong Enemy Controller selection");
        if(!tributed) {require(!tribute_choice,"Repeated tribute");tribute_choice=true;want=sangan;player=0;}
        else {require(!control_target,"Repeated control target");control_target=true;want=jerry_beans;player=1;}
        loc=LOCATION_MZONE;
      } else {
        require(announced==2 && crow_cost && !crow_target && e.player==1,"Wrong Crow target");crow_target=true;
        want=sangan;player=0;loc=LOCATION_GRAVE;
      }
      int index=-1;for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want && e.cards[i].controller==player && e.cards[i].location==loc) index=int(i);
      require(index>=0,"Required selection missing");d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==sangan && e.to.location==LOCATION_GRAVE) {
        require(!setup && announced==1 && !confirmed && tribute_choice && !tributed &&
                e.from.location==LOCATION_MZONE &&
                (e.reason&(REASON_COST|REASON_RELEASE))==(REASON_COST|REASON_RELEASE),"Wrong Sangan cost");tributed=true;
      } else if(e.from.code==dd_crow) {
        require(use_crow && announced==2 && confirmed==1 && !crow_cost && e.from.location==LOCATION_HAND &&
                e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_COST|REASON_DISCARD))==(REASON_COST|REASON_DISCARD),"Wrong Crow cost");crow_cost=true;
      } else if(e.from.code==sangan) {
        require(use_crow && !setup && resolving==2 && crow_target && !banished &&
                e.from.location==LOCATION_GRAVE && e.to.location==LOCATION_REMOVED &&
                (e.reason&REASON_EFFECT),"Crow must banish before setup chain ends");banished=true;
      } else if(e.from.code==jerry_beans) {
        require(!setup && resolving==1 && control_target && !controlled && e.from.controller==1 &&
                e.to.controller==0 && e.to.location==LOCATION_MZONE,"Wrong control change");controlled=true;
      } else if(e.from.code==dupe) {
        require(setup && resolving==1 && search_choice && !added && e.from.location==LOCATION_DECK &&
                e.to.location==LOCATION_HAND,"Wrong search movement");added=true;
      } else require(e.from.code==enemy_controller && e.from.location==LOCATION_SZONE &&
                     e.to.location==LOCATION_GRAVE,"Unexpected movement");
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(!setup && resolving==1 && e.player==0 && e.count==1 && !(e.mask&1u),
              "Unexpected control placement");d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_CHAINED) {
      require(announced==confirmed+1 && e.chain==confirmed+1 &&
              (setup || (e.chain==1?control_target:crow_target)),"Incomplete link");++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed==count() && !resolving && e.chain==confirmed-solved,"Wrong resolution order");resolving=e.chain;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(e.chain==resolving,"Wrong solved link");resolving=0;++solved;
    } else if(e.kind==MSG_CHAIN_END) {
      require(!resolving && solved==count() && !ended,"Incomplete chain");
      if(!setup) {require(controlled && banished==use_crow,"Setup incomplete");setup=true;announced=confirmed=solved=0;}
      else {require(search && added,"Missing search");ended=true;}
    } else if(e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_MISSED_EFFECT ||
              e.kind==MSG_DRAW || e.kind==MSG_PAY_LPCOST || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_CHAIN_NEGATED)
      throw std::runtime_error("Unexpected event in trigger-location fixture");
    return false;
  });
}
