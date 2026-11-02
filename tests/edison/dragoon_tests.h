#pragma once
#include "harness.h"

inline void dragoon_retry(certification::Assets& assets,int options,bool historical,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({polymerization,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({plasma,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  d.add({dogma,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  d.add({destiny_dragoon,0,0,LOCATION_EXTRA,0,POS_FACEDOWN_DEFENSE});
  d.add({exiled_force,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({wrath,1,1,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes,1,1,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  int turns=0,chains=0,announced=0,confirmed=0,resolving=0,solved=0,materials=0,costs=0,summons=0,decisions=0;
  bool poly=false,exiled=false,destroyed=false,negated=false,wrath_chosen=false,discarded=false;
  auto phase_choice=[&] {
    require(turns==3 && d.phase()==PHASE_STANDBY && chains==2+decisions &&
            decisions<(historical?2:1),"Wrong Dragoon retry availability");
    ++decisions;
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {require(e.player==turns%2 && turns<3,"Wrong turn");++turns;}
    else if(e.kind==MSG_SELECT_IDLECMD) {
      if(turns==1 && !poly) {
        int pick=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==polymerization) pick=i;
        require(pick>=0,"Polymerization unavailable");poly=true;d.respond((pick<<16)|5);
      } else if(turns==1) {require(chains==1 && materials==2 && summons==1,"Actual Fusion setup incomplete");d.respond(7);}
      else if(turns==2 && !exiled) {
        require(e.idle[5].size()==1 && e.idle[5][0].code==exiled_force,"Exiled unavailable");exiled=true;d.respond(5);
      } else if(turns==2) {require(chains==2 && destroyed,"Dragoon destruction incomplete");d.respond(7);}
      else {
        require(turns==3 && chains==(historical?4:3) && decisions==(historical?2:1) &&
                costs==decisions && summons==(historical?2:1) && negated && discarded,
                "Historical Dragoon must retry with a second paid banish cost");
        require(d.count(0,LOCATION_MZONE)==int(historical) && d.count(0,LOCATION_REMOVED)==costs &&
                d.count(0,LOCATION_GRAVE)==(historical?1:3) && d.count(0,LOCATION_HAND)==0 &&
                d.count(0,LOCATION_EXTRA)==0 && d.count(1,LOCATION_GRAVE)==3 &&
                d.count(1,LOCATION_HAND)==0 && d.count(1,LOCATION_MZONE)==0 &&
                d.lp(0)==8000 && d.lp(1)==8000,"Wrong final retry zones");
        std::cout<<"Observed: actual Fusion Summon; activation negated; "<<costs<<" banish cost(s), "
                 <<(historical?"same-Standby revival":"modern retry absent")<<".\n";return true;
      }
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(e.player==0 && e.cards.at(0).code==destiny_dragoon,"Wrong phase effect");phase_choice();d.respond(1);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced prompt");int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        const auto& c=e.cards[i];
        require(c.code==destiny_dragoon || c.code==wrath,"Wrong response candidate");
        if(c.code==destiny_dragoon && turns==3 && d.phase()==PHASE_STANDBY && !announced) {phase_choice();pick=i;}
        if(c.code==wrath && chains==2 && confirmed==1) {require(!wrath_chosen,"Repeated Wrath");wrath_chosen=true;pick=i;}
      }
      d.respond(pick);
    } else if(e.kind==MSG_CHAINING) {
      const uint32_t actor=chains==0?polymerization:chains==1?exiled_force:destiny_dragoon;
      require(e.chain==announced+1 && announced==confirmed && e.cards.at(0).code==(announced?wrath:actor),
              "Wrong setup/retry chain");++announced;
    } else if(e.kind==MSG_SELECT_CARD || e.kind==MSG_SELECT_UNSELECT_CARD) {
      require(!e.cards.empty(),"Empty selection");
      int pick=0;
      if(chains==0) {
        require(resolving==1 && e.player==0,"Fusion selection must be in resolution");
        for(auto& c:e.cards) require(c.code==destiny_dragoon || c.code==plasma || c.code==dogma,"Wrong Fusion card");
      } else if(chains==1) {
        require(!confirmed && e.player==1 && e.cards.size()==1 && e.cards[0].code==destiny_dragoon,"Wrong destruction target");
      } else if(e.player==1) {
        require(chains==2 && announced==2 && confirmed==1 && e.cards.size()==1 && e.cards[0].code==blue_eyes,"Wrong Wrath discard");
      } else {
        require(!confirmed && !resolving,"Dragoon banish must be an activation cost");
        for(auto& c:e.cards) require((c.code==plasma||c.code==dogma) && c.location==LOCATION_GRAVE,"Wrong banish cost");
        for(size_t i=0;i<e.cards.size();++i) if(e.cards[i].code==plasma) pick=i;
      }
      if(e.kind==MSG_SELECT_UNSELECT_CARD && e.finishable) d.respond(-1);
      else if(e.kind==MSG_SELECT_CARD && e.minimum==2) d.respond({2,0,1});
      else d.respond({1,uint8_t(pick)});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==plasma || e.from.code==dogma) {
        if(chains==0) {
          require(resolving==1 && e.from.location==LOCATION_HAND && e.to.location==LOCATION_GRAVE &&
                  (e.reason&(REASON_MATERIAL|REASON_FUSION))==(REASON_MATERIAL|REASON_FUSION),"Wrong Fusion materials");++materials;
        } else {
          require(chains>=2 && announced==1 && !confirmed && e.from.location==LOCATION_GRAVE &&
                  e.to.location==LOCATION_REMOVED && (e.reason&REASON_COST),"Wrong revival cost timing");++costs;
        }
      } else if(e.from.code==destiny_dragoon) {
        if(e.to.location==LOCATION_GRAVE) {
          require(chains==1 && resolving==1 && !destroyed && (e.reason&REASON_DESTROY),"Wrong Dragoon destruction");destroyed=true;
        } else require(e.to.location==LOCATION_MZONE && resolving==1 &&
                       (chains==0?e.from.location==LOCATION_EXTRA:historical&&chains==3&&e.from.location==LOCATION_GRAVE),
                       "Wrong Dragoon summon");
      } else if(e.from.code==exiled_force) require(chains==1 && announced==1 && !confirmed && (e.reason&REASON_COST),"Wrong Exiled cost");
      else if(e.from.code==blue_eyes) {
        require(chains==2 && announced==2 && !resolving && (e.reason&(REASON_COST|REASON_DISCARD))==(REASON_COST|REASON_DISCARD),
                "Wrong Wrath cost");discarded=true;
      } else require(e.to.location==LOCATION_GRAVE && ((chains==0&&e.from.code==polymerization) ||
                                                      (chains==2&&e.from.code==wrath)),"Unexpected movement");
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(resolving==1 && (chains==0||chains==3) && e.player==0 && !(e.mask&1),"Wrong summon placement");d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(resolving==1 && e.cards.at(0).code==destiny_dragoon && (e.mask&POS_FACEUP_ATTACK),"Wrong position");d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONED) {require(resolving==1 && (chains==0||chains==3),"Wrong summon success");++summons;}
    else if(e.kind==MSG_CHAINED) {require(e.chain==announced && confirmed+1==announced,"Wrong confirmation");++confirmed;}
    else if(e.kind==MSG_CHAIN_SOLVING) {require(!resolving && e.chain==confirmed-solved,"Wrong reverse resolution");resolving=e.chain;}
    else if(e.kind==MSG_CHAIN_NEGATED) {require(chains==2 && resolving==2 && e.chain==1 && !negated,"Wrong negation");negated=true;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving==e.chain && !(chains==2&&e.chain==1),"Negated activation must be skipped");resolving=0;++solved;}
    else if(e.kind==MSG_CHAIN_END) {
      require(chains==2?(negated&&resolving==1&&solved==1):(!resolving&&solved==1),"Incomplete chain");
      ++chains;announced=confirmed=resolving=solved=0;
    } else if(e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW || e.kind==MSG_DAMAGE)
      throw std::runtime_error("Unexpected Dragoon event");
    return false;
  });
}
