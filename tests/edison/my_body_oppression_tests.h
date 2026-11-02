#pragma once
#include "harness.h"
inline void my_body_oppression(certification::Assets& assets,int options,bool historical,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({cyber_dragon,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  d.add({my_body,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({oppression,1,1,LOCATION_SZONE,0,POS_FACEUP_ATTACK});
  int announced=0,confirmed=0,resolving=0,solved=0;
  bool requested=false,summoning=false,summoned=false,opp=false,body=false,opp_paid=false,body_paid=false,negated=false,ended=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0,"Wrong idle");
      if(!requested) {require(e.idle[1].size()==1 && e.idle[1][0].code==cyber_dragon,"Cyber Dragon unavailable");requested=true;d.respond(1);}
      else {
        require(ended && opp_paid && body==historical && body_paid==historical && negated==historical &&
                summoned==historical,"Historical My Body must respond to a face-up Trap effect");
        require(d.lp(0)==(historical?6500:8000) && d.lp(1)==7200 &&
                d.count(0,LOCATION_MZONE)==int(historical) && d.count(0,LOCATION_GRAVE)==1 &&
                d.code(0,LOCATION_GRAVE,0)==(historical?my_body:cyber_dragon) &&
                d.count(1,LOCATION_SZONE)==int(!historical) && d.count(1,LOCATION_GRAVE)==int(historical),
                "Wrong summon/negation final state");
        std::cout<<"Observed: Oppression paid 800; "<<(historical?"My Body paid 1500, negated its effect activation, summon succeeded":"My Body unavailable, summon negated")<<".\n";return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        auto& c=e.cards[i];
        require(c.code==oppression || c.code==my_body,"Wrong response");
        if(c.code==oppression && e.player==1 && summoning && !announced) {require(!opp,"Repeated Oppression");opp=true;pick=i;}
        if(c.code==my_body) {
          require(historical && confirmed==1 && e.player==0 && !body,"Wrong My Body permission");body=true;pick=i;
        }
      }
      d.respond(pick);
    } else if(e.kind==MSG_CHAINING) {
      require(summoning && e.chain==announced+1 && announced==confirmed &&
              e.cards.at(0).code==(announced?my_body:oppression),"Wrong negation chain");++announced;
    } else if(e.kind==MSG_PAY_LPCOST) {
      if(e.player==1) {require(announced==1 && !confirmed && !opp_paid && e.mask==800,"Wrong Oppression cost");opp_paid=true;}
      else {require(historical && announced==2 && confirmed==1 && !body_paid && e.mask==1500,"Wrong My Body cost");body_paid=true;}
    } else if(e.kind==MSG_SELECT_PLACE) {require(requested && !summoning && e.player==0 && !(e.mask&1),"Wrong placement");d.respond({0,LOCATION_MZONE,0});}
    else if(e.kind==MSG_SELECT_POSITION) {require(e.cards.at(0).code==cyber_dragon && (e.mask&POS_FACEUP_ATTACK),"Wrong position");d.respond(POS_FACEUP_ATTACK);}
    else if(e.kind==MSG_SPSUMMONING) {require(requested && !summoning && e.cards.at(0).code==cyber_dragon,"Wrong summon announcement");summoning=true;}
    else if(e.kind==MSG_SPSUMMONED) {require(historical && negated && ended && !summoned,"Wrong summon success");summoned=true;}
    else if(e.kind==MSG_MOVE) {
      if(e.from.code==cyber_dragon && e.from.location==LOCATION_HAND)
        require(requested && e.to.location==LOCATION_MZONE,"Wrong summon move");
      else if(e.from.code==cyber_dragon)
        require(!historical && e.from.location==LOCATION_MZONE && e.to.location==LOCATION_GRAVE && (e.reason&REASON_DESTROY),"Wrong summon negation destruction");
      else require(historical && (e.from.code==oppression || e.from.code==my_body) && e.to.location==LOCATION_GRAVE,"Wrong negation cleanup");
    } else if(e.kind==MSG_CHAINED) {require(e.chain==announced && confirmed+1==announced,"Wrong confirmation");++confirmed;}
    else if(e.kind==MSG_CHAIN_SOLVING) {require(!resolving && e.chain==confirmed-solved,"Wrong reverse resolution");resolving=e.chain;}
    else if(e.kind==MSG_CHAIN_NEGATED) {require(historical && resolving==2 && e.chain==1 && !negated,"Wrong negation");negated=true;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving==e.chain && !(historical&&e.chain==1),"Negated activation must skip");resolving=0;++solved;}
    else if(e.kind==MSG_CHAIN_END) {
      require(historical?(negated&&resolving==1&&solved==1):(!resolving&&solved==1),"Incomplete negation chain");ended=true;
    } else if(e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_SELECT_CARD || e.kind==MSG_SELECT_EFFECTYN ||
              e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW) throw std::runtime_error("Unexpected Oppression event");
    return false;
  });
}

inline void my_body_trap_effect(certification::Assets& assets,int options,bool historical,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({redmd,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({blue_eyes,0,0,LOCATION_GRAVE,0,POS_FACEUP_ATTACK});
  d.add({my_body,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({oppression,1,1,LOCATION_SZONE,0,POS_FACEUP_ATTACK});
  int chains=0,confirmed=0,resolving=0,paid=0,summons=0,negated=0;
  bool started=false,ended=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      if(!started) {require(e.idle[5].size()==1 && e.idle[5][0].code==redmd,"Missing REDMD ignition");started=true;d.respond(5);}
      else {
        require(ended && chains==(historical?3:2) && paid==(historical?2300:800) &&
                summons==int(historical) && negated==int(historical),"Wrong My Body response/result");
        require(d.lp(0)==(historical?6500:8000) && d.lp(1)==7200 &&
                d.count(0,LOCATION_MZONE)==(historical?2:0) &&
                d.count(0,LOCATION_GRAVE)==(historical?1:2) &&
                d.count(1,LOCATION_GRAVE)==int(historical),"Wrong trap-effect final state");
        std::cout<<"Observed: on-field REDMD "<<(historical?"protected by My Body; revival succeeded":"destroyed by Oppression; revival disabled")<<".\n";return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Forced response");int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        auto c=e.cards[i];require(c.code==oppression || c.code==my_body,"Unexpected response");
        if(c.code==oppression && e.player==1 && confirmed==1) pick=i;
        if(c.code==my_body) {require(historical && e.player==0 && confirmed==2,"Wrong My Body window");pick=i;}
      } d.respond(pick);
    } else if(e.kind==MSG_CHAINING) {
      require(e.chain==++chains && e.cards.at(0).code==(chains==1?redmd:chains==2?oppression:my_body),"Wrong chain order");
    } else if(e.kind==MSG_CHAINED) {require(e.chain==++confirmed && confirmed==chains,"Wrong confirmation");}
    else if(e.kind==MSG_PAY_LPCOST) {require((e.player==1&&e.mask==800)||(historical&&e.player==0&&e.mask==1500),"Wrong cost");paid+=e.mask;}
    else if(e.kind==MSG_CHAIN_SOLVING) resolving=e.chain;
    else if(e.kind==MSG_CHAIN_NEGATED) {require(historical&&resolving==3&&e.chain==2,"Wrong negation");++negated;}
    else if(e.kind==MSG_CHAIN_DISABLED) require(!historical&&e.chain==1,"Wrong effect disable");
    else if(e.kind==MSG_SELECT_CARD) {
      require(historical&&resolving==1&&e.cards.size()==1&&e.cards[0].code==blue_eyes,"Wrong revival choice");d.respond({1,0});
    } else if(e.kind==MSG_SELECT_PLACE) {require(historical&&resolving==1&&!(e.mask&2),"Wrong revival zone");d.respond({0,LOCATION_MZONE,1});}
    else if(e.kind==MSG_SELECT_POSITION) d.respond(POS_FACEUP_ATTACK);
    else if(e.kind==MSG_SPSUMMONED) {require(historical&&resolving==1,"Wrong summon timing");++summons;}
    else if(e.kind==MSG_CHAIN_END) ended=true;
    else if(e.kind==MSG_SELECT_EFFECTYN||e.kind==MSG_SELECT_YESNO) throw std::runtime_error("Unexpected choice");
    return false;
  });
}
