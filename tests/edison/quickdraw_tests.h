#pragma once
#include "harness.h"

inline void quickdraw_summon(certification::Assets& assets,int options,bool historical,bool negate,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({quickdraw,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  d.add({jerry_beans,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  if(negate) {
    d.add({wrath,1,1,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
    d.add({blue_eyes,1,1,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  }
  int announced=0,confirmed=0,resolving=0,solved=0;
  bool requested=false,chosen=false,sent=false,summoned=false,wrath_picked=false,paid=false,negated=false,ended=false;
  const bool blocked=historical&&negate;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Unexpected idle");
      if(!requested) {
        int ignition=-1,procedure=-1;
        for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==quickdraw) ignition=i;
        for(size_t i=0;i<e.idle[1].size();++i) if(e.idle[1][i].code==quickdraw) procedure=i;
        require(historical?(ignition>=0 && procedure<0):(ignition<0 && procedure>=0),
                "Edison Quickdraw must be a hand ignition effect, not a summon procedure");
        requested=true;d.respond(historical?(ignition<<16)|5:(procedure<<16)|1);
      } else {
        require(summoned==!blocked && sent==!blocked && chosen==!blocked && negated==blocked &&
                paid==blocked && ended==historical,"Incomplete Quickdraw resolution");
        require(d.count(0,LOCATION_HAND)==int(blocked) && d.count(0,LOCATION_GRAVE)==1 &&
                d.code(0,LOCATION_GRAVE,0)==(blocked?quickdraw:jerry_beans) &&
                d.count(0,LOCATION_MZONE)==int(!blocked),"Wrong Quickdraw final zones");
        if(blocked) require(d.code(0,LOCATION_HAND,0)==jerry_beans,"Negated effect must not send a monster");
        else require(d.code(0,LOCATION_MZONE,0)==quickdraw,"Missing Quickdraw");
        require(d.lp(0)==8000 && d.lp(1)==8000 && d.count(1,LOCATION_GRAVE)==(blocked?2:0) &&
                d.count(1,LOCATION_SZONE)==int(negate&&!historical),"Wrong comparison state");
        std::cout<<"Observed: "<<(historical?"hand ignition":"summon procedure")<<"; "
                 <<(blocked?"activation negated, no monster sent":"monster sent and Quickdraw summoned")<<".\n";return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced choice");d.respond(-1);}
      else {
        require(blocked && !wrath_picked && confirmed==1 && e.player==1 &&
                e.cards.size()==1 && e.cards[0].code==wrath,"Unexpected negation window");
        wrath_picked=true;d.respond(0);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(historical && e.chain==announced+1 && announced==confirmed &&
              e.cards.at(0).code==(announced?wrath:quickdraw),"Wrong hand ignition chain");++announced;
    } else if(e.kind==MSG_SELECT_CARD || e.kind==MSG_SELECT_UNSELECT_CARD) {
      require(e.minimum==1 && e.maximum==1 && e.cards.size()==1,"Wrong send/discard choice");
      if(e.player==1) {
        require(blocked && announced==2 && confirmed==1 && !resolving &&
                e.cards[0].code==blue_eyes,"Wrong Wrath cost");d.respond({1,0});
      } else {
        require(!chosen && !blocked && e.cards[0].code==jerry_beans && e.cards[0].location==LOCATION_HAND &&
                (historical?resolving==1:announced==0),"Monster send must occur during Edison resolution");
        chosen=true;d.respond({1,0});
      }
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==jerry_beans) {
        require(chosen && !sent && e.from.location==LOCATION_HAND && e.to.location==LOCATION_GRAVE &&
                !(e.reason&REASON_DISCARD) && (historical?(resolving==1 && (e.reason&REASON_EFFECT) && !(e.reason&REASON_COST)):!announced),
                "Wrong monster-send reason/timing");sent=true;
      } else if(e.from.code==blue_eyes) {
        require(blocked && announced==2 && !resolving && (e.reason&(REASON_COST|REASON_DISCARD))==(REASON_COST|REASON_DISCARD),
                "Wrong Wrath discard");paid=true;
      } else if(e.from.code==quickdraw) {
        require(e.from.location==LOCATION_HAND && e.to.location==(blocked?LOCATION_GRAVE:LOCATION_MZONE) &&
                (blocked?negated:sent),"Wrong Quickdraw movement");
      } else require(blocked && e.from.code==wrath && e.to.location==LOCATION_GRAVE,"Unexpected movement");
    } else if(e.kind==MSG_CHAINED) {
      require(historical && e.chain==announced && confirmed+1==announced && (e.chain==1 || paid),"Incomplete link");++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(!resolving && e.chain==confirmed-solved,"Wrong resolution order");resolving=e.chain;
    } else if(e.kind==MSG_CHAIN_NEGATED) {
      require(blocked && resolving==2 && e.chain==1 && !negated,"Wrong negation");negated=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving==e.chain && !(blocked&&e.chain==1),"Negated link must be skipped");resolving=0;++solved;
    } else if(e.kind==MSG_CHAIN_END) {
      require(historical && (blocked?(negated && resolving==1 && solved==1):(summoned && solved==1 && !resolving)),
              "Incomplete hand ignition chain");ended=true;resolving=0;
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(!blocked && sent && e.player==0 && e.count==1 && !(e.mask&1),"Wrong placement");d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(!blocked && sent && e.cards.at(0).code==quickdraw && (e.mask&POS_FACEUP_ATTACK),"Wrong summon position");d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONING) {
      require(!blocked && sent && e.cards.at(0).code==quickdraw &&
              (historical?resolving==1:!announced),"Summon must occur inside historical effect resolution");
    } else if(e.kind==MSG_SPSUMMONED) {require(!blocked && sent && !summoned,"Unexpected summon");summoned=true;}
    else if(e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW)
      throw std::runtime_error("Unexpected Quickdraw event");
    return false;
  });
}
