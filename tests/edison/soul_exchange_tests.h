#pragma once
#include "harness.h"

inline void soul_exchange_choice(certification::Assets& assets,int options,bool historical,bool own,bool summon,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({soul_exchange,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes,0,0,LOCATION_MZONE,1,POS_FACEUP_ATTACK});
  d.add({jerry_beans,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({summon?caius:catapult_turtle,0,0,uint8_t(summon?LOCATION_HAND:LOCATION_MZONE),0,
         uint8_t(summon?POS_FACEDOWN_DEFENSE:POS_FACEUP_ATTACK)});
  int chains=0;bool requested=false,action=false,announced=false,confirmed=false,resolving=false,solved=false,paid=false;
  bool choice=false;int damage=0;
  auto finish=[&] {
    require(paid && d.count(0,LOCATION_MZONE)==2-int(own) && d.count(1,LOCATION_MZONE)==int(own) &&
            d.count(0,LOCATION_GRAVE)==1+int(own) && d.count(1,LOCATION_GRAVE)==int(!own) &&
            d.lp(0)==8000 && d.lp(1)==8000-(summon?0:own?1500:875),"Wrong tribute outcome");
    if(own) require(choice,"Own tribute was not explicitly chosen");
    std::cout<<"Observed: "<<(summon?"Tribute Summon":"activation cost")<<" used "
             <<(own?"own monster; opposing target retained":"opposing target")<<"; "
             <<(historical?"optional Edison choice":"modern mandatory target")<<".\n";
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0,"Wrong idle");
      if(!requested) {
        int pick=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==soul_exchange) pick=i;
        require(pick>=0,"Soul Exchange unavailable");requested=true;d.respond((pick<<16)|5);
      } else if(!action) {
        require(chains==1,"Soul Exchange incomplete");
        auto& list=e.idle[summon?0:5];int pick=-1;
        for(size_t i=0;i<list.size();++i) if(list[i].code==(summon?caius:catapult_turtle)) pick=i;
        require(pick>=0,"Tribute action unavailable");action=true;d.respond((pick<<16)|(summon?0:5));
      } else {require(!summon && chains==2 && damage==(own?1500:875),"Turtle effect incomplete");finish();return true;}
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(e.cards.empty() && !e.forced,"Unexpected response");d.respond(-1);
    } else if(e.kind==MSG_SELECT_YESNO) {
      require(summon && action && !paid,"Unexpected yes/no");d.respond(1);
    } else if(e.kind==MSG_CHAINING) {
      require(!announced && e.chain==1 && e.cards.at(0).code==(chains?catapult_turtle:soul_exchange),
              "Wrong effect chain");announced=true;
    } else if(e.kind==MSG_SELECT_CARD || e.kind==MSG_SELECT_TRIBUTE) {
      require(e.player==0 && e.minimum==1 && e.maximum==1,"Wrong selection");
      if(!chains) {
        require(announced && !confirmed && e.cards.size()==1 && e.cards[0].code==jerry_beans,"Wrong Soul target");
        d.respond({1,0});
      } else {
        require(action && !paid && !choice,"Wrong tribute window");choice=true;
        bool has_own=false,has_opponent=false;int pick=-1;
        for(size_t i=0;i<e.cards.size();++i) {
          if(e.cards[i].code==blue_eyes && e.cards[i].controller==0) has_own=true;
          if(e.cards[i].code==jerry_beans && e.cards[i].controller==1) has_opponent=true;
          if(e.cards[i].code==(own?blue_eyes:jerry_beans)) pick=i;
        }
        require(has_opponent && has_own==historical,"Edison Soul Exchange tribute must be optional");
        require(pick>=0,"Desired tribute absent");d.respond({1,uint8_t(pick)});
      }
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==soul_exchange) require(e.to.location==LOCATION_GRAVE,"Wrong Spell cleanup");
      else if(e.from.code==caius) require(summon && paid && e.from.location==LOCATION_HAND && e.to.location==LOCATION_MZONE,"Wrong Caius summon");
      else {
        require(action && !paid && e.from.code==(own?blue_eyes:jerry_beans) &&
                e.from.location==LOCATION_MZONE && e.to.location==LOCATION_GRAVE &&
                (e.reason&REASON_RELEASE) && (summon?bool(e.reason&REASON_SUMMON):bool(e.reason&REASON_COST)),
                "Wrong tribute reason/identity");paid=true;
      }
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(summon && paid && e.player==0 && !(e.mask&1),"Wrong summon placement");d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SUMMONED) {require(summon && paid && chains==1,"Wrong summon success");finish();return true;}
    else if(e.kind==MSG_DAMAGE) {require(!summon && resolving && e.player==1 && !damage,"Wrong damage");damage=e.mask;}
    else if(e.kind==MSG_CHAINED) {require(announced && !confirmed && e.chain==1,"Wrong confirmation");confirmed=true;}
    else if(e.kind==MSG_CHAIN_SOLVING) {require(confirmed && !resolving && e.chain==1,"Wrong resolution");resolving=true;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving && e.chain==1,"Wrong solved link");resolving=false;solved=true;}
    else if(e.kind==MSG_CHAIN_END) {require(solved && !resolving,"Incomplete chain");++chains;announced=confirmed=solved=false;}
    else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_DRAW)
      throw std::runtime_error("Unexpected Soul Exchange event");
    return false;
  });
}
