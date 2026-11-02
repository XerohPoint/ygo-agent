#pragma once
#include "harness.h"

inline void historical_turtle_repeat(certification::Assets& assets,int options,bool historical,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({catapult_turtle,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  for(uint8_t zone:{uint8_t(1),uint8_t(2)}) d.add({blue_eyes,0,0,LOCATION_MZONE,zone,POS_FACEUP_ATTACK});
  int completed=0,damage_count=0,cost_count=0;
  bool requested=false,announced=false,selected=false,confirmed=false,resolving=false,solved=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Unexpected idle");
      int index=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==catapult_turtle) index=int(i);
      if(completed==(historical?2:1)) {
        if(!historical) require(index<0,"Rule 5 Turtle must retain its once-per-turn limit");
        require(cost_count==completed && damage_count==completed && d.lp(0)==8000 &&
                d.lp(1)==8000-1500*completed && d.count(0,LOCATION_MZONE)==3-completed &&
                d.code(0,LOCATION_MZONE,0)==catapult_turtle && d.count(0,LOCATION_GRAVE)==completed,
                "Wrong repeat-effect state");
        for(int i=0;i<completed;++i) require(d.code(0,LOCATION_GRAVE,i)==blue_eyes,"Wrong tribute identity");
        if(!historical) require(d.code(0,LOCATION_MZONE,2)==blue_eyes,"Unspent tribute changed");
        for(int p=0;p<2;++p) {
          for(int loc:{LOCATION_DECK,LOCATION_HAND,LOCATION_SZONE,LOCATION_EXTRA,LOCATION_REMOVED})
            require(d.count(p,loc)==0,"Unexpected extra zone");
          if(p==1) require(d.count(p,LOCATION_MZONE)==0 && d.count(p,LOCATION_GRAVE)==0,"Unexpected opponent card");
        }
        std::cout<<"Observed: "<<completed<<" separate Turtle activations, "<<cost_count<<" tributes, opponent LP "<<d.lp(1)<<".\n";
        return true;
      }
      require(index>=0 && !requested,"Historical Turtle must permit another activation in the same turn");
      requested=true;d.respond((index<<16)|5);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced && e.cards.empty(),"Unexpected response");d.respond(-1);
    } else if(e.kind==MSG_CHAINING) {
      require(requested && !announced && e.chain==1 && e.player==0 && e.cards.at(0).code==catapult_turtle,"Wrong activation");announced=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced && !confirmed && !selected && e.player==0 && e.minimum==1 && e.maximum==1,"Wrong tribute selection");
      int index=-1;for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==blue_eyes && e.cards[i].controller==0 && e.cards[i].location==LOCATION_MZONE &&
           e.cards[i].sequence==completed+1) index=int(i);
      require(index>=0,"Required tribute unavailable");selected=true;d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_MOVE) {
      require(announced && selected && !confirmed && e.from.code==blue_eyes &&
              e.from.location==LOCATION_MZONE && e.from.sequence==completed+1 &&
              e.to.location==LOCATION_GRAVE && cost_count==completed &&
              (e.reason&(REASON_COST|REASON_RELEASE))==(REASON_COST|REASON_RELEASE),"Wrong tribute cost");++cost_count;
    } else if(e.kind==MSG_CHAINED) {
      require(announced && selected && cost_count==completed+1 && !confirmed && e.chain==1,"Incomplete paid-cost link");confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && !solved && e.chain==1,"Wrong resolution");resolving=true;
    } else if(e.kind==MSG_DAMAGE) {
      require(resolving && damage_count==completed && e.player==1 && e.mask==1500,"Wrong Turtle damage");++damage_count;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving && e.chain==1 && damage_count==completed+1,"Damage did not resolve");resolving=false;solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved && !resolving,"Incomplete chain");++completed;
      requested=announced=selected=confirmed=solved=false;
    } else if(e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW ||
              e.kind==MSG_PAY_LPCOST || e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED)
      throw std::runtime_error("Unexpected Turtle event");
    return false;
  });
}
