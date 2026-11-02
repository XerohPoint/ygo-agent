#pragma once
#include "harness.h"
#include "ygoenv/ygopro/decision_context.h"

inline void decision_hint_lifecycle() {
  using namespace certification;
  ygopro::DecisionContext c;
  c.hint_for(0,501);
  c.begin(1,MSG_SELECT_CARD,1,2);
  require(c.hint==0,"Hint leaked to the other player");
  c.begin(0,MSG_SELECT_CARD,1,2);
  require(c.hint==0,"Stale hint survived another player's prompt");
  c.hint_for(0,502);
  c.hint_for(0,89631139);
  c.begin(0,MSG_SELECT_CARD,1,2);
  require(c.hint==0,"Private passcode hint or prior hint exposed");
  c.hint_for(1,549);
  c.begin(1,MSG_SELECT_CARD,1,1);
  require(c.hint==549,"Attack-target hint lost");
  c.selected={"m1"}; c.mandatory={"m1"}; c.candidates={"m2"};
  require(c.knows_candidate("m1") && c.knows_candidate("m2") &&
          !c.knows_candidate("om2"),"Selection knowledge/perspective wrong");
  c.begin(1,MSG_SELECT_TRIBUTE,1,1);
  require(c.hint==531 && c.selected.empty() && c.mandatory.empty(),
          "Selection state leaked into the next prompt");
  c.reset();
  require(!c.active && !c.knows_candidate("m2"),"Reset retained selection knowledge");
}

inline void decision_brionac_stages(certification::Assets& assets,int options,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({brionac,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({blue_eyes,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  d.add({jerry_beans,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  d.add({jerry_beans,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  bool activated=false,cost=false,target=false,selected=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      if(activated) {
        require(cost && target && d.count(1,LOCATION_MZONE)==0,"Cost/target sequence incomplete");
        require(ygopro::decision_execution_context(d.handle()).stage==ygopro::DecisionStage::Unknown,
                "Stale execution context in idle command");
        return true;
      }
      int index=-1;
      for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==brionac) index=i;
      require(index>=0,"Brionac activation missing");
      activated=true; d.respond((index<<16)|5);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced chain"); d.respond(-1);
    } else if(e.kind==MSG_SELECT_UNSELECT_CARD) {
      auto frame=ygopro::decision_execution_context(d.handle());
      require(frame.stage==ygopro::DecisionStage::Cost && frame.source &&
              frame.source->data.code==brionac,"Discard is not attributed to Brionac's cost");
      cost=true;
      if(e.selected.empty()) { require(!selected,"Repeated initial cost"); selected=true; d.respond({1,0}); }
      else { require(e.selected.size()==1 && e.finishable,"Missing selected-cost group"); d.respond(-1); }
    } else if(e.kind==MSG_SELECT_CARD) {
      auto frame=ygopro::decision_execution_context(d.handle());
      require(cost && frame.stage==ygopro::DecisionStage::Activation && frame.source &&
              frame.source->data.code==brionac,"Return target mislabeled as a cost");
      target=true;
      int index=-1;
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].controller==1 && e.cards[i].location==LOCATION_MZONE) index=i;
      require(index>=0,"Return target missing");d.respond({1,static_cast<byte>(index)});
    }
    return false;
  });
}

inline void decision_operation_stage(certification::Assets& assets,int options,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({dealings,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  for(int p=0;p<2;++p) {
    d.add({jerry_beans,uint8_t(p),uint8_t(p),LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
    d.add({blue_eyes,uint8_t(p),uint8_t(p),LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE});
  }
  bool activated=false;int discards=0;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      if(activated) {require(discards==2,"Both resolving discards were not observed"); return true;}
      int index=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==dealings) index=i;
      require(index>=0,"Dealings activation missing");activated=true;d.respond((index<<16)|5);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced response");d.respond(-1);
    } else if(e.kind==MSG_SELECT_PLACE) {
      d.respond({0,LOCATION_SZONE,0});
    } else if(e.kind==MSG_SELECT_CARD) {
      auto frame=ygopro::decision_execution_context(d.handle());
      require(frame.stage==ygopro::DecisionStage::Operation && frame.source &&
              frame.source->data.code==dealings,"Resolving discard mislabeled as an activation cost");
      require(e.player==discards && e.minimum==1 && e.maximum==1,"Unexpected resolving selection");
      ++discards;d.respond({1,0});
    }
    return false;
  });
}
