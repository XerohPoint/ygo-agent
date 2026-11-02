#pragma once
#include "harness.h"

inline void end_dragon_departure(certification::Assets& assets,int options,bool light,bool leave,bool trace) {
  using namespace certification;
  const uint32_t actor=light?light_end:dark_end;const uint8_t tp=light?1:0,op=1-tp;
  TestDuel d(assets,0,0,trace,options);
  d.add({actor,tp,tp,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({jerry_beans,op,op,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  if(leave) d.add({compulsory,op,op,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  int announced=0,confirmed=0,resolving=0,solved=0;
  bool requested=false,attacked=false,decided=false,comp=false,target=false,left=false,entered=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      if(light && e.player==0) d.respond(7);
      else if(light) {require(!entered && e.to_battle,"Battle unavailable");entered=true;d.respond(6);}
      else {require(!requested && e.idle[5].size()==1 && e.idle[5][0].code==actor,"Dark End unavailable");requested=true;d.respond(5);}
    } else if(e.kind==MSG_SELECT_BATTLECMD) {
      require(light && entered && !requested && e.idle[0].size()==1 && e.idle[0][0].code==actor,"Wrong attacker");
      requested=true;d.respond(1);
    } else if(e.kind==MSG_ATTACK) {require(light && requested && !attacked,"Wrong attack");attacked=true;}
    else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(light && attacked && !decided && e.player==tp && e.cards.at(0).code==actor,"Wrong optional trigger");decided=true;d.respond(1);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced choice");d.respond(-1);}
      else if(e.player==tp) {
        require(light && attacked && !decided && e.cards.size()==1 && e.cards[0].code==actor,"Wrong Light End trigger");
        decided=true;d.respond(0);
      } else {
        require(leave && !comp && e.cards.size()==1 && e.cards[0].code==compulsory,"Wrong response");
        if(confirmed==1) {comp=true;d.respond(0);} else {require(!announced,"Wrong bounce window");d.respond(-1);}
      }
    } else if(e.kind==MSG_CHAINING) {
      require(e.chain==announced+1 && announced==confirmed && e.cards.at(0).code==(announced?compulsory:actor),
              "Wrong chain");++announced;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(e.minimum==1 && e.maximum==1,"Wrong selection");
      if(!announced) require(light && requested && !attacked && e.player==tp,"Wrong attack target");
      else if(announced==1) {require(!light && !confirmed && !target && e.player==tp,"Wrong Dark End target");target=true;}
      else require(leave && comp && confirmed==1 && e.player==op,"Wrong Compulsory target");
      int pick=-1;for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==jerry_beans && e.cards[i].controller==op) pick=i;
      require(pick>=0,"Missing target");d.respond({1,uint8_t(pick)});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==jerry_beans) {
        require(!left && e.from.location==LOCATION_MZONE && e.to.location==(leave?LOCATION_HAND:LOCATION_GRAVE) &&
                resolving==(leave?2:1) && (e.reason&REASON_EFFECT),"Wrong target departure");left=true;
      } else require(leave && e.from.code==compulsory && e.to.location==LOCATION_GRAVE,"Unexpected movement");
    } else if(e.kind==MSG_CHAINED) {require(e.chain==announced && confirmed+1==announced,"Wrong confirmation");++confirmed;}
    else if(e.kind==MSG_CHAIN_SOLVING) {require(!resolving && e.chain==confirmed-solved,"Wrong resolution");resolving=e.chain;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving==e.chain,"Wrong solved link");resolving=0;++solved;}
    else if(e.kind==MSG_CHAIN_END) {
      require(!resolving && solved==(leave?2:1) && left==(leave||!light) &&
              d.query_value(tp,LOCATION_MZONE,0,QUERY_ATTACK)==2100 &&
              d.query_value(tp,LOCATION_MZONE,0,QUERY_DEFENSE)==1600,"Dragon must lose 500 ATK/DEF even after target leaves");
      require(d.count(op,LOCATION_HAND)==int(leave) && d.count(op,LOCATION_MZONE)==int(light&&!leave) &&
              d.count(op,LOCATION_GRAVE)==int(leave||!light),"Wrong target zones");
      if(light&&!leave) require(d.query_value(op,LOCATION_MZONE,0,QUERY_ATTACK)==250,"Light End target reduction missing");
      require(d.lp(0)==8000 && d.lp(1)==8000,"Unexpected LP before battle");
      std::cout<<"Observed: "<<(light?"Light":"Dark")<<" End loses 500 ATK/DEF; target "
               <<(leave?"already returned to hand":light?"loses 1500 ATK":"sent to Graveyard")<<".\n";return true;
    } else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_SELECT_YESNO ||
              e.kind==MSG_DRAW || e.kind==MSG_DAMAGE) throw std::runtime_error("Unexpected End Dragon event");
    return false;
  });
}
