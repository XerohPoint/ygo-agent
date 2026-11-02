#pragma once
#include "harness.h"

inline void simultaneous_battle(certification::Assets& assets, int options,
                                bool optional, int accept_mask, bool trace) {
  using namespace certification;
  const uint32_t fighter=optional?tomato:sangan, reward=optional?sangan:dupe;
  const auto accepts=[&](int p) { return !optional || bool(accept_mask&(1<<p)); };
  TestDuel d(assets,0,0,trace,options);
  for(uint8_t p:{0,1}) {
    d.add({fighter,p,p,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
    d.add({reward,p,p,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  }
  std::vector<int> owners;
  for(int p:{1,0}) if(accepts(p)) owners.push_back(p);
  int turns=0,announced=0,confirmed=0,solved=0,resolving=0;
  bool passed=false,entered=false,requested=false,attacked=false,damage=false,calculated=false,ended=false,chain_end=false;
  bool grave[2]={},decided[2]={},selected[2]={},received[2]={},summoned[2]={};
  auto decide=[&](int p) {
    require(optional && p>=0 && p<=1 && damage && !ended && grave[0] && grave[1] &&
            !decided[p] && (p==1 || decided[1]),"Optional battle triggers must be offered turn player first");
    require(confirmed==(p==1?0:int(accepts(1))),"Optional battle decision offered in wrong chain order");
    decided[p]=true;
  };
  auto resolving_owner=[&] {
    require(resolving>0 && resolving<=int(owners.size()),"No resolving battle trigger");
    return owners[resolving-1];
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {
      require(e.player==turns && turns<2,"Unexpected turn progression");++turns;
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      if(e.player==0) {require(turns==1 && !passed && e.to_end,"P0 must end first turn");passed=true;d.respond(7);}
      else {require(turns==2 && passed && !entered && e.to_battle,"P1 must enter Battle Phase");entered=true;d.respond(6);}
    } else if(e.kind==MSG_SELECT_BATTLECMD) {
      require(e.player==1 && entered,"Unexpected battle command");
      if(!requested) {
        require(e.idle[0].size()==1 && e.idle[0][0].code==fighter,"Expected sole attacker");requested=true;d.respond(1);
      } else {
        require(attacked && calculated && grave[0] && grave[1] && ended &&
                announced==int(owners.size()) && confirmed==announced && solved==confirmed &&
                chain_end==!owners.empty(),"Simultaneous battle sequence incomplete");
        for(int p=0;p<2;++p) {
          require(decided[p]==optional && selected[p]==accepts(p) && received[p]==accepts(p) &&
                  summoned[p]==(optional && accepts(p)),"Missing trigger decision or outcome");
          require(d.lp(p)==8000 && d.count(p,LOCATION_GRAVE)==1 && d.code(p,LOCATION_GRAVE,0)==fighter &&
                  d.count(p,LOCATION_DECK)==int(!accepts(p)) && d.count(p,LOCATION_HAND)==int(!optional) &&
                  d.count(p,LOCATION_MZONE)==int(optional && accepts(p)) &&
                  d.count(p,LOCATION_SZONE)==0 && d.count(p,LOCATION_REMOVED)==0 &&
                  d.count(p,LOCATION_EXTRA)==0,"Unexpected final battle state");
          require(d.code(p,!accepts(p)?LOCATION_DECK:optional?LOCATION_MZONE:LOCATION_HAND,0)==reward,
                  "Wrong final reward identity");
        }
        std::cout<<"Observed: both monsters destroyed together; "<<owners.size()
                 <<" triggers ordered P1 (turn player) before P0 and resolved in reverse order.\n";
        return true;
      }
    } else if(e.kind==MSG_ATTACK) {
      require(requested && !attacked && e.from.controller==1 && e.to.controller==0 &&
              e.from.location==LOCATION_MZONE && e.to.location==LOCATION_MZONE,"Wrong attack");attacked=true;
    } else if(e.kind==MSG_DAMAGE_STEP_START) {
      require(attacked && !damage,"Unexpected Damage Step");damage=true;
    } else if(e.kind==MSG_BATTLE) {
      require(damage && !calculated && e.cards.size()==2 && e.cards[0].controller==1 &&
              e.cards[1].controller==0 && e.battle_stats[0]==(optional?1400u:1000u) &&
              e.battle_stats[2]==e.battle_stats[0] && e.battle_destroyed[0] && e.battle_destroyed[1],
              "Equal ATK monsters must both be destroyed");calculated=true;
    } else if(e.kind==MSG_MOVE) {
      int p=e.from.controller;
      require(p>=0 && p<=1 && e.to.controller==p,"Unexpected movement controller");
      if(e.from.code==fighter && e.to.location==LOCATION_GRAVE) {
        require(damage && calculated && !ended && !grave[p] && e.from.location==LOCATION_MZONE &&
                (e.reason&(REASON_BATTLE|REASON_DESTROY))==(REASON_BATTLE|REASON_DESTROY),
                "Expected simultaneous battle destruction");grave[p]=true;
      } else {
        require(p==resolving_owner() && selected[p] && !received[p] && e.from.code==reward &&
                e.from.location==LOCATION_DECK && e.to.location==(optional?LOCATION_MZONE:LOCATION_HAND),
                "Unexpected trigger reward movement");received[p]=true;
      }
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(e.cards.size()==1 && e.cards[0].code==fighter,"Unexpected optional effect");
      decide(e.player);d.respond(accepts(e.player)?1:0);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced prompt");d.respond(-1);}
      else {
        require(optional && !e.forced && e.cards.size()==1 && e.cards[0].code==fighter,"Unexpected trigger prompt");
        decide(e.player);d.respond(accepts(e.player)?0:-1);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(damage && !ended && grave[0] && grave[1] && announced==confirmed &&
              announced<int(owners.size()) && e.chain==announced+1 && e.player==owners[announced] &&
              e.cards.at(0).code==fighter && (!optional || decided[e.player]),"Simultaneous battle trigger order violated");
      ++announced;
    } else if(e.kind==MSG_CHAINED) {
      require(e.chain==confirmed+1 && announced==confirmed+1,"Unexpected confirmed link");++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(!resolving && confirmed==int(owners.size()) && e.chain==confirmed-solved,
              "Unexpected battle resolution order");resolving=e.chain;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(e.minimum==1 && e.maximum==1 && e.cards.size()==1,"Unexpected battle selection");
      if(resolving) {
        int p=resolving_owner();
        require(e.player==p && !selected[p] && e.cards[0].code==reward &&
                e.cards[0].controller==p && e.cards[0].location==LOCATION_DECK,"Wrong reward selection");selected[p]=true;
      } else require(requested && !damage && e.player==1 && e.cards[0].code==fighter &&
                     e.cards[0].controller==0 && e.cards[0].location==LOCATION_MZONE,"Wrong attack target");
      d.respond({1,0});
    } else if(e.kind==MSG_SELECT_PLACE) {
      int p=resolving_owner();
      require(optional && selected[p] && e.player==p && e.count==1 && !(e.mask&1u),"Wrong recruitment placement");
      d.respond({static_cast<byte>(p),LOCATION_MZONE,0});
    } else if(e.kind==MSG_SPSUMMONING) {
      int p=resolving_owner();
      require(optional && received[p] && !summoned[p] && e.cards.at(0).code==sangan &&
              e.cards[0].controller==p && e.cards[0].position==POS_FACEUP_ATTACK,"Unexpected recruit");
    } else if(e.kind==MSG_SPSUMMONED) {
      int p=resolving_owner();require(optional && received[p] && !summoned[p],"Unexpected summon success");summoned[p]=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving==e.chain && received[resolving_owner()],"Missing battle trigger outcome");resolving=0;++solved;
    } else if(e.kind==MSG_CHAIN_END) {
      require(!resolving && solved==int(owners.size()) && !chain_end,"Incomplete battle chain");chain_end=true;
    } else if(e.kind==MSG_DAMAGE_STEP_END) {
      require(damage && !ended && grave[0] && grave[1] && (!optional || (decided[0] && decided[1])) &&
              solved==int(owners.size()),"Damage Step ended before triggers completed");ended=true;
    } else if(e.kind==MSG_DAMAGE || e.kind==MSG_DRAW || e.kind==MSG_PAY_LPCOST ||
              e.kind==MSG_SELECT_YESNO || e.kind==MSG_SELECT_POSITION || e.kind==MSG_MISSED_EFFECT)
      throw std::runtime_error("Unexpected event in simultaneous battle fixture");
    return false;
  });
}
