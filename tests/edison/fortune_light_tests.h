#pragma once
#include "harness.h"

inline void fortune_light_departure(certification::Assets& assets,int options,bool historical,bool faceup,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({exiled_force,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({fortune_light,1,1,LOCATION_MZONE,0,uint8_t(faceup?POS_FACEUP_ATTACK:POS_FACEDOWN_DEFENSE)});
  d.add({fortune_light,1,1,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  const bool triggers=historical||faceup;
  int chains=0;bool requested=false,announced=false,confirmed=false,resolving=false,solved=false,cost=false;
  bool target=false,destroyed=false,decided=false,recruited=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0,"Wrong idle");
      if(!requested) {require(e.idle[5].size()==1 && e.idle[5][0].code==exiled_force,"Exiled unavailable");requested=true;d.respond(5);}
      else {
        require(chains==1+int(triggers) && cost && target && destroyed && decided==triggers &&
                recruited==triggers,"Face-down Fortune Lady Light must trigger in Edison");
        require(d.count(0,LOCATION_GRAVE)==1 && d.count(0,LOCATION_MZONE)==0 &&
                d.count(1,LOCATION_GRAVE)==1 && d.code(1,LOCATION_GRAVE,0)==fortune_light &&
                d.count(1,LOCATION_MZONE)==int(triggers) && d.count(1,LOCATION_DECK)==int(!triggers) &&
                d.lp(0)==8000 && d.lp(1)==8000,"Wrong final departure/recruit state");
        if(triggers) require(d.query_value(1,LOCATION_MZONE,0,QUERY_ATTACK)==200,"Wrong recruit stats");
        std::cout<<"Observed: "<<(faceup?"face-up":"face-down")<<" Light destroyed; "
                 <<(triggers?"trigger recruited a Fortune Lady":"no trigger")<<".\n";return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced choice");d.respond(-1);}
      else {
        require(triggers && chains==1 && destroyed && !decided && e.player==1 &&
                !e.forced && e.cards.size()==1 && e.cards[0].code==fortune_light,"Wrong trigger offer");
        decided=true;d.respond(0);
      }
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(triggers && chains==1 && destroyed && !decided && e.player==1 &&
              e.cards.at(0).code==fortune_light,"Wrong optional trigger");decided=true;d.respond(1);
    } else if(e.kind==MSG_CHAINING) {
      require(!announced && e.chain==1 && e.cards.at(0).code==(chains?fortune_light:exiled_force) &&
              (!chains || decided),"Wrong trigger activation");announced=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced && e.minimum==1 && e.maximum==1 && e.cards.size()==1 &&
              e.cards[0].code==fortune_light,"Wrong selection");
      if(chains==0) {require(cost && !confirmed && !target && e.player==0 && e.cards[0].location==LOCATION_MZONE,"Wrong Exiled target");target=true;}
      else require(resolving && e.player==1 && e.cards[0].location==LOCATION_DECK,"Wrong recruit");
      d.respond({1,0});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==exiled_force) {
        require(!chains && announced && !confirmed && !cost && e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_COST|REASON_RELEASE))==(REASON_COST|REASON_RELEASE),"Wrong Exiled cost");cost=true;
      } else if(e.from.location==LOCATION_MZONE) {
        require(!chains && resolving && !destroyed && e.from.code==fortune_light &&
                e.to.location==LOCATION_GRAVE && (e.reason&REASON_DESTROY),"Wrong destruction");destroyed=true;
      } else require(chains==1 && resolving && e.from.code==fortune_light &&
                     e.from.location==LOCATION_DECK && e.to.location==LOCATION_MZONE,"Wrong recruitment");
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(chains==1 && resolving && e.player==1 && e.count==1 && !(e.mask&1),"Wrong placement");d.respond({1,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(chains==1 && resolving && e.cards.at(0).code==fortune_light && (e.mask&POS_FACEUP_ATTACK),"Wrong position");d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONED) {require(chains==1 && resolving && !recruited,"Wrong summon");recruited=true;}
    else if(e.kind==MSG_CHAINED) {require(announced && !confirmed && e.chain==1,"Wrong confirmation");confirmed=true;}
    else if(e.kind==MSG_CHAIN_SOLVING) {require(confirmed && !resolving && e.chain==1,"Wrong resolution");resolving=true;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving && e.chain==1,"Wrong solved link");resolving=false;solved=true;}
    else if(e.kind==MSG_CHAIN_END) {require(solved && !resolving,"Incomplete chain");++chains;announced=confirmed=solved=false;}
    else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_MISSED_EFFECT ||
            e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW) throw std::runtime_error("Unexpected Fortune Lady event");
    return false;
  });
}
