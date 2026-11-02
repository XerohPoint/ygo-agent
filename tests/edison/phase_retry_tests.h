#pragma once
#include "harness.h"

inline void treeborn_retry(certification::Assets& assets,int options,bool historical,bool accept_retry,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({treeborn,0,0,LOCATION_GRAVE,0,POS_FACEUP_ATTACK});
  d.add({wrath,1,1,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes,1,1,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  int decisions=0,stage=0,announced=0,confirmed=0,resolving=0,solved=0;
  bool wrath_chosen=false,cost_selected=false,cost=false,negated=false,revived=false;
  auto choose=[&] {
    require(d.phase()==PHASE_STANDBY && decisions<2 && !announced,"Unexpected Treeborn decision");
    if(decisions==1) require(historical && stage==1 && negated,"Only historical Treeborn may retry");
    return ++decisions==1 || accept_retry;
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1 && decisions==(historical?2:1) && cost && negated &&
              stage==(historical && accept_retry?2:1) && revived==(historical && accept_retry),
              "Treeborn did not offer/resolve the required same-Standby retry");
      require(d.count(0,LOCATION_MZONE)==int(revived) && d.count(0,LOCATION_GRAVE)==int(!revived) &&
              d.code(0,revived?LOCATION_MZONE:LOCATION_GRAVE,0)==treeborn &&
              d.count(1,LOCATION_GRAVE)==2 && d.code(1,LOCATION_GRAVE,0)==blue_eyes &&
              d.code(1,LOCATION_GRAVE,1)==wrath,"Wrong retry final state");
      for(int p=0;p<2;++p) {
        require(d.lp(p)==8000,"Unexpected LP");
        for(int loc:{LOCATION_HAND,LOCATION_DECK,LOCATION_SZONE,LOCATION_EXTRA,LOCATION_REMOVED})
          require(d.count(p,loc)==0,"Unexpected extra cards");
      }
      require(d.count(1,LOCATION_MZONE)==0,"Unexpected opponent monster");
      std::cout<<"Observed: first activation negated; "<<(historical?accept_retry?"second offer accepted and revived":"second offer declined":"Rule 5 permits no retry")<<".\n";
      return true;
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(e.player==0 && e.cards.at(0).code==treeborn,"Wrong optional offer");d.respond(choose()?1:0);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced choice");d.respond(-1);}
      else if(e.player==0) {
        require(!e.forced && e.cards.size()==1 && e.cards[0].code==treeborn,"Wrong phase choice");d.respond(choose()?0:-1);
      } else {
        require(stage==0 && confirmed==1 && !wrath_chosen && e.cards.size()==1 && e.cards[0].code==wrath,"Wrong Wrath response");
        wrath_chosen=true;d.respond(0);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(announced==confirmed && e.chain==announced+1 && announced<(stage==0?2:1) &&
              e.cards.at(0).code==(e.chain==1?treeborn:wrath) && e.player==(e.chain==1?0:1),
              "Wrong retry chain formation");++announced;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(stage==0 && announced==2 && confirmed==1 && !cost_selected && e.player==1 &&
              e.minimum==1 && e.maximum==1 && e.cards.size()==1 && e.cards[0].code==blue_eyes &&
              e.cards[0].location==LOCATION_HAND,"Wrong Wrath discard");cost_selected=true;d.respond({1,0});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==blue_eyes) {
        require(stage==0 && cost_selected && !cost && !resolving && e.from.location==LOCATION_HAND &&
                e.to.location==LOCATION_GRAVE && (e.reason&(REASON_COST|REASON_DISCARD))==(REASON_COST|REASON_DISCARD),
                "Wrong Wrath cost movement");cost=true;
      } else if(e.from.code==wrath) require(stage==0 && negated && e.from.location==LOCATION_SZONE &&
                                            e.to.location==LOCATION_GRAVE,"Wrong Wrath cleanup");
      else require(stage==1 && historical && accept_retry && resolving==1 && e.from.code==treeborn &&
                   e.from.location==LOCATION_GRAVE && e.to.location==LOCATION_MZONE,"Wrong Treeborn movement");
    } else if(e.kind==MSG_CHAINED) {
      require(announced==confirmed+1 && e.chain==announced && (e.chain==1 || cost),"Incomplete link");++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed==(stage==0?2:1) && !resolving && e.chain==confirmed-solved,"Wrong resolution order");resolving=e.chain;
    } else if(e.kind==MSG_CHAIN_NEGATED) {
      require(stage==0 && resolving==2 && e.chain==1 && !negated,"Wrong activation negation");negated=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving==e.chain && !(stage==0 && e.chain==1),"Negated link must be skipped");resolving=0;++solved;
    } else if(e.kind==MSG_CHAIN_END) {
      if(stage==0) require(negated && resolving==1 && solved==1,"First Treeborn activation must be skipped");
      else require(revived && !resolving && solved==1,"Retry must resolve");
      ++stage;announced=confirmed=resolving=solved=0;
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(stage==1 && historical && accept_retry && resolving==1 && e.player==0 && e.count==1 && !(e.mask&1),"Wrong retry placement");
      d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(stage==1 && resolving==1 && e.cards.at(0).code==treeborn && (e.mask&POS_FACEUP_ATTACK),"Wrong retry position");
      d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONING) {
      require(stage==1 && resolving==1 && e.cards.at(0).code==treeborn,"Unexpected Special Summon");
    } else if(e.kind==MSG_SPSUMMONED) {
      require(stage==1 && resolving==1 && !revived,"Unexpected revival success");revived=true;
    } else if(e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_PAY_LPCOST || e.kind==MSG_DRAW ||
              e.kind==MSG_SELECT_YESNO || e.kind==MSG_DAMAGE)
      throw std::runtime_error("Unexpected phase-retry event");
    return false;
  });
}
