#pragma once
#include "harness.h"
inline void my_body_damage(certification::Assets& assets,int options,bool historical,bool damage_mode,bool trace) {
  using namespace certification;
  const bool allowed=!historical||!damage_mode;
  TestDuel d(assets,0,0,trace,options);
  d.add({man_eater,0,0,LOCATION_MZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({my_body,1,1,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  int chains=0,confirmed=0,resolving=0,paid=0,negated=0;
  bool started=false,entered=false,attack=false,damage=false,battle=false,done=false,ended=false;
  d.start(options);
  auto finish=[&]() {
    require(ended && chains==(allowed?2:1) && paid==(allowed?1500:0) && negated==int(allowed),"Wrong My Body Damage Step eligibility");
    require(d.lp(0)==8000 && d.lp(1)==(allowed?6500:8000) &&
            d.count(0,LOCATION_GRAVE)==1 && d.code(0,LOCATION_GRAVE,0)==man_eater &&
            d.count(1,LOCATION_MZONE)==int(allowed) &&
            d.count(1,LOCATION_GRAVE)==1,"Wrong flip destruction final zones");
    std::cout<<"Observed: "<<(damage_mode?"Damage Step":"Main Phase")<<"; My Body "<<(allowed?"negated FLIP, paid 1500":"unavailable; FLIP destroyed attacker")<<".\n";
    return true;
  };
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      if(!started) {
        require(e.player==0,"Wrong initial player");started=true;
        if(damage_mode) d.respond(7);
        else {require(e.idle[2].size()==1&&e.idle[2][0].code==man_eater,"No Flip Summon");d.respond(2);}
      } else if(damage_mode) {require(e.player==1&&!entered&&e.to_battle,"Wrong battle entry");entered=true;d.respond(6);}
      else return finish();
    } else if(e.kind==MSG_SELECT_BATTLECMD) {
      require(damage_mode&&e.player==1,"Wrong battle command");
      if(!attack) {require(e.idle[0].size()==1&&e.idle[0][0].code==blue_eyes,"Wrong attacker");attack=true;d.respond(1);}
      else {require(done,"Incomplete Damage Step");return finish();}
    } else if(e.kind==MSG_SELECT_CARD) {
      int pick=-1;
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==(chains?blue_eyes:man_eater)) pick=i;
      require(pick>=0&&e.minimum==1&&e.maximum==1,"Missing attack/destruction target");
      if(chains) require(chains==1&&!confirmed&&(!damage_mode||battle),"FLIP target wrong timing");
      d.respond({1,uint8_t(pick)});
    } else if(e.kind==MSG_SELECT_CHAIN) {
      int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        auto c=e.cards[i];
        if(c.code==man_eater) {require(!chains,"Repeated FLIP");pick=i;}
        else {require(c.code==my_body&&allowed&&confirmed==1&&e.player==1,"My Body offered in forbidden window");pick=i;}
      }
      require(!e.forced||pick>=0,"Missing forced FLIP");d.respond(pick);
    } else if(e.kind==MSG_CHAINING) {
      require(e.chain==++chains&&e.cards.at(0).code==(chains==1?man_eater:my_body),"Wrong flip chain");
      if(damage_mode) require(damage&&battle&&!done,"FLIP outside Damage Step");
    } else if(e.kind==MSG_CHAINED) {require(e.chain==++confirmed,"Wrong confirmation");}
    else if(e.kind==MSG_CHAIN_SOLVING) resolving=e.chain;
    else if(e.kind==MSG_CHAIN_NEGATED) {require(allowed&&resolving==2&&e.chain==1,"Wrong FLIP negation");++negated;}
    else if(e.kind==MSG_PAY_LPCOST) {require(allowed&&e.player==1&&e.mask==1500,"Wrong cost");paid+=e.mask;}
    else if(e.kind==MSG_CHAIN_END) ended=true;
    else if(e.kind==MSG_DAMAGE_STEP_START) damage=true;
    else if(e.kind==MSG_BATTLE) {require(damage&&e.battle_destroyed[1],"Wrong battle outcome");battle=true;}
    else if(e.kind==MSG_DAMAGE_STEP_END) {require(battle&&ended,"Early Damage Step end");done=true;}
    else if(e.kind==MSG_SELECT_EFFECTYN||e.kind==MSG_SELECT_YESNO||e.kind==MSG_CHAIN_DISABLED||e.kind==MSG_DAMAGE)
      throw std::runtime_error("Unexpected My Body event");
    return false;
  });
}
