#pragma once
#include "harness.h"

inline void ryko_targeting(certification::Assets& assets,int options,bool historical,bool destroy,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({ryko,0,0,LOCATION_MZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({jerry_beans,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  for(int i=0;i<3;++i) d.add({blue_eyes,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  bool requested=false,flipped=false,announced=false,confirmed=false,resolving=false,ended=false;
  bool decision=false,selected=false,destroyed=false;int milled=0;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0,"Wrong turn");
      if(!requested) {
        require(e.idle[2].size()==1 && e.idle[2][0].code==ryko,"Ryko cannot Flip Summon");
        requested=true;d.respond(2);
      } else {
        require(ended && decision && selected==destroy && destroyed==destroy && milled==3,"Incomplete Ryko effect");
        require(d.count(0,LOCATION_DECK)==0 && d.count(0,LOCATION_GRAVE)==3 &&
                d.code(0,LOCATION_MZONE,0)==ryko && d.count(1,LOCATION_MZONE)==int(!destroy) &&
                d.count(1,LOCATION_GRAVE)==int(destroy) && d.lp(0)==8000 && d.lp(1)==8000,"Wrong Ryko final state");
        std::cout<<"Observed: destruction "<<(destroy?"accepted":"declined")<<"; decision "
                 <<(historical?"at activation":"at resolution")<<"; exactly three cards milled afterward.\n";return true;
      }
    } else if(e.kind==MSG_FLIPSUMMONED) {require(requested && !flipped,"Wrong Flip Summon");flipped=true;}
    else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced choice");d.respond(-1);}
      else {
        require(flipped && !announced && e.player==0 && e.forced && e.cards.size()==1 &&
                e.cards[0].code==ryko,"Unexpected chain offer");d.respond(0);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(flipped && !announced && e.chain==1 && e.cards.at(0).code==ryko,"Wrong Flip trigger");announced=true;
    } else if(e.kind==MSG_SELECT_YESNO) {
      require(announced && !decision && e.player==0 && resolving==!historical && confirmed==!historical,
              "Ryko must choose its optional target at activation in Edison");
      decision=true;d.respond(destroy?1:0);
    } else if(e.kind==MSG_SELECT_CARD) {
      require(decision && destroy && !selected && resolving==!historical && e.player==0 &&
              e.minimum==1 && e.maximum==1,"Wrong destruction selection");
      int pick=-1;for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==jerry_beans && e.cards[i].controller==1) pick=i;
      require(pick>=0,"Missing destruction target");selected=true;d.respond({1,uint8_t(pick)});
    } else if(e.kind==MSG_CHAINED) {
      require(announced && !confirmed && e.chain==1 && decision==historical,"Wrong activation decision timing");
      confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {require(confirmed && !resolving && e.chain==1,"Wrong resolution");resolving=true;}
    else if(e.kind==MSG_MOVE) {
      require(resolving && (e.reason&REASON_EFFECT) && !(e.reason&REASON_COST),"Ryko movement must be an effect");
      if(e.from.code==jerry_beans) {
        require(destroy && selected && !destroyed && milled==0 && e.from.location==LOCATION_MZONE &&
                e.to.location==LOCATION_GRAVE && (e.reason&REASON_DESTROY),"Wrong destruction order");destroyed=true;
      } else {
        require(e.from.code==blue_eyes && e.from.location==LOCATION_DECK && e.to.location==LOCATION_GRAVE &&
                destroyed==destroy && milled<3,"Milling must follow optional destruction");++milled;
      }
    } else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving && milled==3 && e.chain==1,"Incomplete mill");resolving=false;}
    else if(e.kind==MSG_CHAIN_END) {require(!resolving && milled==3 && !ended,"Incomplete chain");ended=true;}
    else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_PAY_LPCOST ||
            e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_DRAW) throw std::runtime_error("Unexpected Ryko event");
    return false;
  });
}
