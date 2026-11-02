#pragma once
#include "harness.h"

inline void historical_brionac(certification::Assets& assets,int options,bool historical,bool own,bool trace) {
  using namespace certification;
  const uint8_t target_player=own?0:1;
  const int activations=historical?(own?1:2):(own?0:1), available=own?1:2;
  TestDuel d(assets,0,0,trace,options);
  d.add({brionac,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  for(int i=0;i<available;++i) {
    d.add({blue_eyes,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
    d.add({jerry_beans,target_player,target_player,LOCATION_MZONE,uint8_t(own?1:i),POS_FACEUP_ATTACK});
  }
  int completed=0,costs=0,returns=0;
  bool requested=false,announced=false,cost_selected=false,target_selected=false,confirmed=false,resolving=false,solved=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Unexpected idle");
      int index=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==brionac) index=int(i);
      if(completed==activations) {
        if(!historical) require(index<0,"Rule 5 must retain Brionac's target/count restrictions");
        require(costs==completed && returns==completed && d.count(0,LOCATION_GRAVE)==completed &&
                d.code(0,LOCATION_MZONE,0)==brionac && d.count(0,LOCATION_MZONE)==1+(own?available-completed:0) &&
                d.count(1,LOCATION_MZONE)==(own?0:available-completed),"Wrong Brionac final zones");
        for(int i=0;i<completed;++i) require(d.code(0,LOCATION_GRAVE,i)==blue_eyes,"Wrong discarded cost");
        std::vector<uint32_t> hand,want(available-completed,blue_eyes);
        if(own) for(int i=0;i<completed;++i) want.push_back(jerry_beans);
        for(int i=0;i<d.count(0,LOCATION_HAND);++i) hand.push_back(d.code(0,LOCATION_HAND,i));
        std::sort(hand.begin(),hand.end());std::sort(want.begin(),want.end());require(hand==want,"Wrong own hand identities");
        require(d.count(1,LOCATION_HAND)==(own?0:completed),"Wrong opponent hand count");
        for(int i=0;i<d.count(1,LOCATION_HAND);++i) require(d.code(1,LOCATION_HAND,i)==jerry_beans,"Wrong returned card");
        if(available>completed) require(d.code(target_player,LOCATION_MZONE,own?1:1)==jerry_beans,"Unreturned target changed");
        for(int p=0;p<2;++p) {
          require(d.lp(p)==8000,"Unexpected LP");
          for(int loc:{LOCATION_DECK,LOCATION_EXTRA,LOCATION_SZONE,LOCATION_REMOVED}) require(d.count(p,loc)==0,"Unexpected extra zones");
          if(p==1) require(d.count(p,LOCATION_GRAVE)==0,"Unexpected opponent Graveyard");
        }
        std::cout<<"Observed: "<<completed<<" Brionac activations targeting "<<(own?"own":"opposing")
                 <<" cards; "<<costs<<" discards paid.\n";
        return true;
      }
      require(index>=0 && !requested,"Historical Brionac target/repeat activation unavailable");
      requested=true;d.respond((index<<16)|5);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced && e.cards.empty(),"Unexpected response");d.respond(-1);
    } else if(e.kind==MSG_CHAINING) {
      require(requested && !announced && e.player==0 && e.chain==1 && e.cards.at(0).code==brionac,"Wrong activation");announced=true;
    } else if(e.kind==MSG_SELECT_UNSELECT_CARD) {
      require(announced && !confirmed && costs==completed && e.player==0 && e.minimum==1 &&
              e.maximum>=1 && !e.cancelable,"Wrong discard subgroup prompt");
      if(e.selected.empty()) {
        require(!cost_selected && !e.finishable,"Repeated discard choice");
        int index=-1;for(size_t i=0;i<e.cards.size();++i)
          if(e.cards[i].code==blue_eyes && e.cards[i].controller==0 && e.cards[i].location==LOCATION_HAND) index=int(i);
        require(index>=0,"Discard filler unavailable");cost_selected=true;d.respond({1,static_cast<byte>(index)});
      } else {
        require(cost_selected && e.finishable && e.selected.size()==1 && e.selected[0].code==blue_eyes &&
                e.selected[0].location==LOCATION_HAND,"Must finish after exactly one selected discard");d.respond(-1);
      }
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced && !confirmed && costs==completed+1 && !target_selected &&
              e.player==0 && e.minimum==1 && e.maximum==1,"Wrong bounce target prompt");
      int index=-1;for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==jerry_beans && e.cards[i].controller==target_player &&
           e.cards[i].location==LOCATION_MZONE && e.cards[i].sequence==(own?1:completed)) index=int(i);
      require(index>=0,"Required bounce target unavailable");target_selected=true;d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==blue_eyes) {
        require(announced && cost_selected && !confirmed && costs==completed &&
                e.from.location==LOCATION_HAND && e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_COST|REASON_DISCARD))==(REASON_COST|REASON_DISCARD),"Wrong discard cost");++costs;
      } else {
        require(resolving && target_selected && returns==completed && e.from.code==jerry_beans &&
                e.from.controller==target_player && e.to.controller==target_player &&
                e.from.location==LOCATION_MZONE && e.to.location==LOCATION_HAND &&
                (e.reason&REASON_EFFECT),"Wrong target return");++returns;
      }
    } else if(e.kind==MSG_CHAINED) {
      require(announced && target_selected && costs==completed+1 && !confirmed && e.chain==1,"Incomplete link");confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && !solved && e.chain==1,"Wrong resolution");resolving=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving && e.chain==1 && returns==completed+1,"Bounce did not resolve");resolving=false;solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved && !resolving,"Incomplete chain");++completed;
      requested=announced=cost_selected=target_selected=confirmed=solved=false;
    } else if(e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW ||
              e.kind==MSG_PAY_LPCOST || e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED)
      throw std::runtime_error("Unexpected Brionac event");
    return false;
  });
}
