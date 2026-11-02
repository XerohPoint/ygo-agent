#pragma once
#include "harness.h"

// Small zone fixture, not a submitted deck: extra copies isolate per-name limits.
inline void rescue_cat_history(certification::Assets& assets,int options,bool historical,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  for(uint8_t i=0;i<2;++i) d.add({rescue_cat,0,0,LOCATION_MZONE,i,POS_FACEUP_ATTACK});
  for(int i=0;i<4;++i) d.add({sea_koala,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  const int cats=historical?2:1;
  int chains=0,costs=0,recruited=0,summon_batches=0,destroyed=0;
  bool requested=false,announced=false,confirmed=false,resolving=false,solved=false,target=false,disabled=false,passed=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN && e.player==1) {
      require(passed && chains==cats+1 && costs==cats && recruited==2*cats && summon_batches==cats &&
              destroyed==2*cats && disabled==!historical,"Incomplete Rescue Cat sequence");
      require(d.count(0,LOCATION_MZONE)==2-cats && d.count(0,LOCATION_GRAVE)==3*cats &&
              d.count(0,LOCATION_DECK)==4-2*cats && d.count(0,LOCATION_HAND)==0 &&
              d.query_value(1,LOCATION_MZONE,0,QUERY_ATTACK)==3000 && d.lp(0)==8000 && d.lp(1)==8000,
              "Wrong final End Phase destruction/reset state");
      std::cout<<"Observed: "<<cats<<" Cat activation(s); Koala "<<(disabled?"negated":"resolved")
               <<"; all "<<destroyed<<" recruits destroyed at End Phase.\n";return true;
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && !passed && !requested,"Unexpected idle");
      int cat=-1,koala=-1;
      for(size_t i=0;i<e.idle[5].size();++i) {
        if(e.idle[5][i].code==rescue_cat) cat=i;
        else if(e.idle[5][i].code==sea_koala) koala=i;
        else require(false,"Unexpected ignition");
      }
      if(chains==1) require((cat>=0)==historical,"Rescue Cat historical per-name limit is wrong");
      if(chains<cats) {require(cat>=0,"Cat unavailable");requested=true;d.respond((cat<<16)|5);}
      else if(chains==cats) {require(koala>=0,"Koala activation unavailable");requested=true;d.respond((koala<<16)|5);}
      else {
        require(chains==cats+1 && target && disabled==!historical &&
                d.query_value(1,LOCATION_MZONE,0,QUERY_ATTACK)==uint32_t(historical?0:3000),
                "Historical Rescue Cat must not negate its recruits");
        passed=true;d.respond(7);
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced choice");d.respond(-1);}
      else if(passed) {
        require(e.player==0 && e.forced && d.phase()==PHASE_END,"Wrong delayed destruction window");
        for(auto& c:e.cards) require(c.code==rescue_cat,"Wrong delayed effect");d.respond(0);
      } else {
        require(!e.forced && e.player==0,"Unexpected response");
        for(auto& c:e.cards) require(c.code==sea_koala || c.code==rescue_cat,"Wrong ignition-priority candidate");
        d.respond(-1);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(requested && !announced && e.chain==1 && e.cards.at(0).code==(chains<cats?rescue_cat:sea_koala),
              "Wrong activation");announced=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced && e.player==0,"Wrong choice");
      if(chains<cats) {
        require(resolving && e.minimum==2 && e.maximum==2 && e.cards.size()==size_t(4-recruited),"Wrong recruitment");
        for(auto& c:e.cards) require(c.code==sea_koala && c.location==LOCATION_DECK,"Wrong Beast");
        d.respond({2,0,1});
      } else {
        require(!confirmed && !target && e.cards.size()==1 && e.cards[0].code==blue_eyes,"Wrong Koala target");
        target=true;d.respond({1,0});
      }
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==rescue_cat) {
        require(announced && !confirmed && chains<cats && e.from.location==LOCATION_MZONE &&
                e.to.location==LOCATION_GRAVE && (e.reason&REASON_COST) && !(e.reason&REASON_RELEASE),
                "Cat sends itself as cost, not tribute");++costs;
      } else {
        require(e.from.code==sea_koala,"Unexpected movement");
        if(e.from.location==LOCATION_DECK) {
          require(resolving && chains<cats && e.to.location==LOCATION_MZONE && (e.reason&REASON_SPSUMMON),"Wrong recruit movement");++recruited;
        } else {
          require(passed && d.phase()==PHASE_END && e.from.location==LOCATION_MZONE &&
                  e.to.location==LOCATION_GRAVE && (e.reason&REASON_DESTROY),"Wrong delayed destruction");++destroyed;
        }
      }
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(resolving && chains<cats && e.player==0 && e.count==1,"Wrong placement");
      int slot=0;while(slot<5 && (e.mask&(1u<<slot))) ++slot;
      require(slot<5,"No free monster zone");d.respond({0,LOCATION_MZONE,uint8_t(slot)});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(resolving && chains<cats && e.cards.at(0).code==sea_koala && (e.mask&POS_FACEUP_ATTACK),"Wrong recruit position");
      d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONED) {require(resolving && chains<cats,"Wrong summon success");++summon_batches;}
    else if(e.kind==MSG_CHAINED) {require(announced && !confirmed && e.chain==1,"Wrong confirmation");confirmed=true;}
    else if(e.kind==MSG_CHAIN_SOLVING) {require(confirmed && !resolving && e.chain==1,"Wrong resolution");resolving=true;}
    else if(e.kind==MSG_CHAIN_DISABLED) {require(!historical && chains==cats && resolving && !disabled,"Wrong negation");disabled=true;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving && e.chain==1,"Wrong solved link");resolving=false;solved=true;}
    else if(e.kind==MSG_CHAIN_END) {require(solved && !resolving,"Incomplete chain");++chains;requested=announced=confirmed=solved=false;}
    else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_SELECT_YESNO || e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_DRAW)
      throw std::runtime_error("Unexpected Rescue Cat event");
    return false;
  });
}
