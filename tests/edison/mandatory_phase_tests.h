#pragma once
#include "harness.h"

inline void mandatory_phase_retry(certification::Assets& assets,int options,bool historical,bool spirit,bool drain,bool trace) {
  using namespace certification;
  const uint32_t actor=spirit?susa:lumina;
  TestDuel d(assets,0,0,trace,options);
  d.add({actor,0,0,uint8_t(spirit?LOCATION_HAND:LOCATION_MZONE),0,uint8_t(spirit?POS_FACEDOWN_DEFENSE:POS_FACEUP_ATTACK)});
  if(drain) d.add({skill_drain,1,1,LOCATION_SZONE,0,POS_FACEUP_ATTACK});
  else d.add({light_dark_dragon,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  if(!spirit) for(int i=0;i<3;++i) d.add({blue_eyes,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  int chains=0,announced=0,confirmed=0,resolving=0,solved=0,negations=0,disabled=0,milled=0;
  bool normal=false,summoned=!spirit,passed=false,returned=false,chain_negated=false;
  const int expected_chains=historical&&!drain?5:1,expected_negations=drain?0:historical?4:1;
  const bool succeeds=historical&&!drain;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN && e.player==1) {
      require(passed && chains==expected_chains && negations==expected_negations && disabled==int(drain) &&
              returned==(spirit&&succeeds) && milled==(!spirit&&succeeds?3:0),"Mandatory phase trigger retry count/outcome is wrong");
      require(d.count(0,LOCATION_MZONE)==int(!returned) && d.count(0,LOCATION_HAND)==int(returned) &&
              d.count(0,LOCATION_GRAVE)==milled && d.count(0,LOCATION_DECK)==(spirit?0:3-milled),
              "Wrong phase final zones");
      if(!drain) require(d.query_value(1,LOCATION_MZONE,0,QUERY_ATTACK)==uint32_t(2800-500*negations) &&
                         d.query_value(1,LOCATION_MZONE,0,QUERY_DEFENSE)==uint32_t(2400-500*negations),"Wrong LADD stat reduction");
      require(d.lp(0)==8000 && d.lp(1)==8000,"Unexpected LP change");
      std::cout<<"Observed: "<<chains<<" phase activation(s), "<<negations<<" activation negation(s), "
               <<disabled<<" effect negation(s); "<<(returned?"Spirit returned":milled?"milled three":"no resolved operation")<<".\n";return true;
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && !passed,"Unexpected idle");
      if(spirit&&!normal) {
        require(e.idle[0].size()==1 && e.idle[0][0].code==susa,"Spirit summon unavailable");normal=true;d.respond(0);
      } else {require(summoned && e.to_end,"Expected End Phase");passed=true;d.respond(7);}
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(spirit && normal && !summoned && e.player==0 && !(e.mask&1),"Wrong Spirit placement");d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SUMMONED) {require(spirit && normal && !summoned,"Wrong summon");summoned=true;}
    else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced prompt");d.respond(-1);}
      else {
        require(passed && d.phase()==PHASE_END && e.forced && e.cards.size()==1 &&
                (e.cards[0].code==actor || (!drain&&e.cards[0].code==light_dark_dragon)),
                "Wrong phase trigger choice");d.respond(0);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(passed && d.phase()==PHASE_END && chains<expected_chains && e.chain==announced+1 &&
              announced==confirmed && e.cards.at(0).code==(announced?light_dark_dragon:actor),
              "Unexpected mandatory phase chain");++announced;
    } else if(e.kind==MSG_CHAINED) {require(e.chain==announced && confirmed+1==announced,"Wrong confirmation");++confirmed;}
    else if(e.kind==MSG_CHAIN_SOLVING) {
      require(!resolving && e.chain==confirmed-solved,"Wrong resolution order");resolving=e.chain;
    } else if(e.kind==MSG_CHAIN_NEGATED) {
      require(!drain && resolving==2 && e.chain==1 && !chain_negated,"Wrong activation negation");
      ++negations;chain_negated=true;
    } else if(e.kind==MSG_CHAIN_DISABLED) {
      require(drain && resolving==1 && e.chain==1 && !disabled,"Wrong effect negation");++disabled;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(e.chain==resolving && !(e.chain==1&&chain_negated),"Negated activation must skip operation");
      resolving=0;++solved;
    } else if(e.kind==MSG_CHAIN_END) {
      require(chain_negated?(resolving==1 && solved==1):(!resolving && solved==confirmed),"Incomplete phase chain");
      ++chains;announced=confirmed=resolving=solved=0;chain_negated=false;
    } else if(e.kind==MSG_MOVE) {
      if(spirit && e.from.location==LOCATION_HAND) {
        require(normal && !summoned && e.from.code==susa && e.to.location==LOCATION_MZONE,"Wrong Spirit summon move");
      } else {
        require(succeeds && resolving==1 && chains==4 && (e.reason&REASON_EFFECT),"Wrong phase movement timing");
        if(spirit) {require(!returned && e.from.code==susa && e.to.location==LOCATION_HAND,"Wrong return");returned=true;}
        else {require(milled<3 && e.from.code==blue_eyes && e.from.location==LOCATION_DECK &&
                      e.to.location==LOCATION_GRAVE,"Wrong mill");++milled;}
      }
    } else if(e.kind==MSG_SELECT_CARD || e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO ||
              e.kind==MSG_DRAW || e.kind==MSG_PAY_LPCOST) throw std::runtime_error("Unexpected mandatory-phase event");
    return false;
  });
}
