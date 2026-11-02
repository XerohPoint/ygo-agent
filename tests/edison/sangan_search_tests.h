#pragma once
#include "harness.h"

// Test 019 resolves Sangan's search, then tests the searched name's ignition
// effect in the same Main Phase. Searching Dupe instead is the eligibility control.
inline void sangan_search_restriction(certification::Assets& assets, int options,
                                      bool historical, bool search_lonefire, bool trace) {
  using namespace certification;
  const uint32_t searched=search_lonefire?lonefire:dupe;
  const bool allowed=historical||!search_lonefire;
  TestDuel d(assets,0,0,trace,options);
  d.add({exiled_force,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({sangan,0,0,LOCATION_MZONE,1,POS_FACEUP_ATTACK});
  d.add({searched,0,0,LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE});
  d.add({jerry_beans,0,0,LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE});
  if(!search_lonefire) d.add({lonefire,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  int completed=0;
  bool requested=false,announced=false,confirmed=false,resolving=false,solved=false;
  bool exiled_cost=false,target=false,destroyed=false,search_selected=false,added=false;
  bool normal_requested=false,normal_summoning=false,normal_summoned=false;
  bool lonefire_requested=false,lonefire_cost_selected=false,lonefire_cost=false;
  bool recruit_selected=false,recruit_summoning=false,recruited=false;
  auto actor=[&] { return completed==0?exiled_force:completed==1?sangan:lonefire; };
  auto check_zones=[&](bool after_recruit) {
    require(d.phase()==PHASE_MAIN1 && d.lp(0)==8000 && d.lp(1)==8000,"Unexpected phase or LP");
    require(d.count(0,LOCATION_MZONE)==1 &&
            d.code(0,LOCATION_MZONE,0)==(after_recruit?jerry_beans:lonefire) &&
            d.position(0,LOCATION_MZONE,0)==POS_FACEUP_ATTACK,"Wrong final monster");
    require(d.count(0,LOCATION_DECK)==int(!after_recruit),"Unexpected remaining Deck");
    if(!after_recruit) require(d.code(0,LOCATION_DECK,0)==jerry_beans,"Recruit must remain available");
    require(d.count(0,LOCATION_HAND)==int(!search_lonefire),"Unexpected final hand size");
    if(!search_lonefire) require(d.code(0,LOCATION_HAND,0)==dupe,"Search control lost Dupe Frog");
    require(d.count(0,LOCATION_GRAVE)==2+int(after_recruit) &&
            d.code(0,LOCATION_GRAVE,0)==exiled_force && d.code(0,LOCATION_GRAVE,1)==sangan,
            "Setup cost or destroyed Sangan missing");
    if(after_recruit) require(d.code(0,LOCATION_GRAVE,2)==lonefire,"Lonefire cost missing");
    for(int p=0;p<2;++p) {
      require(d.count(p,LOCATION_SZONE)==0 && d.count(p,LOCATION_REMOVED)==0 &&
              d.count(p,LOCATION_EXTRA)==0,"Unexpected S/T, banished, or Extra Deck cards");
      if(p==1) for(int loc:{LOCATION_HAND,LOCATION_DECK,LOCATION_MZONE,LOCATION_GRAVE})
        require(d.count(p,loc)==0,"Unexpected opposing card");
    }
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {
      require(e.player==0 && !requested,"Unexpected turn progression");
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Expected P0 Main Phase");
      if(!requested) {
        int index=-1;
        for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==exiled_force) index=int(i);
        require(index>=0,"Exiled Force unavailable");requested=true;d.respond((index<<16)|5);
      } else if(!normal_requested) {
        require(completed==2 && exiled_cost && target && destroyed && search_selected && added,
                "Sangan search must fully resolve before Normal Summon");
        int index=-1;
        for(size_t i=0;i<e.idle[0].size();++i)
          if(e.idle[0][i].code==lonefire && e.idle[0][i].location==LOCATION_HAND) index=int(i);
        require(index>=0,"Lonefire Normal Summon unavailable after search");
        normal_requested=true;d.respond(index<<16);
      } else if(!lonefire_requested) {
        require(completed==2 && normal_summoning && normal_summoned,"Normal Summon incomplete");
        check_zones(false);
        int index=-1;
        for(size_t i=0;i<e.idle[5].size();++i)
          if(e.idle[5][i].code==lonefire && e.idle[5][i].controller==0 &&
             e.idle[5][i].location==LOCATION_MZONE && e.idle[5][i].sequence==0) index=int(i);
        std::cout << "Observed: Sangan searched " << (search_lonefire?"Lonefire":"Dupe Frog")
                  << "; Normal Summoned Lonefire's ignition effect is "
                  << (index>=0?"available":"absent") << " in the same Main Phase.\n";
        require((index>=0)==allowed,allowed
            ?"Sangan incorrectly prevents the same-turn activation of the searched monster"
            :"Rule 5 Sangan must prevent activation of the searched name");
        if(!allowed) return true;
        lonefire_requested=true;d.respond((index<<16)|5);
      } else {
        require(completed==3 && lonefire_cost_selected && lonefire_cost && recruit_selected &&
                recruit_summoning && recruited,"Allowed Lonefire effect did not finish");
        check_zones(true);
        std::cout << "Observed: allowed Lonefire paid its tribute cost and recruited Jerry Beans Man.\n";
        return true;
      }
    } else if(e.kind==MSG_CHAINING) {
      require(!announced && !solved && e.chain==1 && e.player==0 &&
              e.cards.at(0).code==actor() && e.cards[0].controller==0 &&
              e.cards[0].location==(completed==1?LOCATION_GRAVE:LOCATION_MZONE) &&
              (completed==0?requested:completed==1?destroyed:lonefire_requested),
              "Unexpected activation or trigger chain");
      announced=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced && e.player==0 && e.minimum==1 && e.maximum==1 && e.cards.size()==1,
              "Expected a single legal target, search, cost, or recruit");
      uint32_t want;int loc;
      if(completed==0) {
        require(!confirmed && exiled_cost && !target,"Target must follow Exiled's paid cost");
        want=sangan;loc=LOCATION_MZONE;target=true;
      } else if(completed==1) {
        require(resolving && !search_selected,"Search must occur in Sangan resolution");
        want=searched;loc=LOCATION_DECK;search_selected=true;
      } else if(!confirmed) {
        require(lonefire_requested && !lonefire_cost_selected && !lonefire_cost,"Unexpected Lonefire cost choice");
        want=lonefire;loc=LOCATION_MZONE;lonefire_cost_selected=true;
      } else {
        require(resolving && lonefire_cost && !recruit_selected,"Unexpected recruitment");
        want=jerry_beans;loc=LOCATION_DECK;recruit_selected=true;
      }
      require(e.cards[0].code==want && e.cards[0].controller==0 && e.cards[0].location==loc,
              "Wrong selected card");d.respond({1,0});
    } else if(e.kind==MSG_MOVE) {
      const auto code=e.from.code;
      require(e.from.controller==0 && e.to.controller==0,"Unexpected movement controller");
      if(e.to.location==LOCATION_GRAVE && (code==exiled_force || code==lonefire)) {
        require(announced && !confirmed && !resolving && e.from.location==LOCATION_MZONE &&
                (e.reason&(REASON_COST|REASON_RELEASE))==(REASON_COST|REASON_RELEASE) &&
                !(e.reason&(REASON_DESTROY|REASON_EFFECT)),"Expected self-tribute activation cost");
        if(code==exiled_force) { require(completed==0 && !exiled_cost,"Repeated Exiled cost");exiled_cost=true; }
        else { require(completed==2 && lonefire_cost_selected && !lonefire_cost,"Repeated Lonefire cost");lonefire_cost=true; }
      } else if(code==sangan) {
        require(completed==0 && resolving && target && !destroyed &&
                e.from.location==LOCATION_MZONE && e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_EFFECT|REASON_DESTROY))==(REASON_EFFECT|REASON_DESTROY),
                "Sangan must be destroyed by Exiled's resolving effect");destroyed=true;
      } else if(code==searched && e.to.location==LOCATION_HAND) {
        require(completed==1 && resolving && search_selected && !added &&
                e.from.location==LOCATION_DECK && (e.reason&REASON_EFFECT),"Expected Sangan search movement");added=true;
      } else if(code==lonefire && e.from.location==LOCATION_HAND) {
        require(completed==2 && normal_requested && !normal_summoned && e.to.location==LOCATION_MZONE,
                "Unexpected Lonefire Normal Summon movement");
      } else {
        require(code==jerry_beans && completed==2 && resolving && recruit_selected &&
                e.from.location==LOCATION_DECK && e.to.location==LOCATION_MZONE &&
                (e.reason&REASON_SPSUMMON),"Unexpected card movement");
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected mandatory ordering choice");
      for(const auto& c:e.cards)
        require(normal_summoned && !lonefire_requested && e.player==0 && c.code==lonefire &&
                c.location==LOCATION_MZONE,"Unexpected response candidate");
      // Reach an ordinary idle window; this test does not depend on ignition priority.
      d.respond(-1);
    } else if(e.kind==MSG_CHAINED) {
      require(announced && !confirmed && e.chain==1,"Unexpected chain confirmation");
      if(completed==0) require(exiled_cost && target,"Exiled setup incomplete");
      if(completed==2) require(lonefire_cost,"Lonefire cost unpaid");
      confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && !solved && e.chain==1,"Unexpected resolution");resolving=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving && !solved && e.chain==1,"Unexpected solved link");
      require(completed==0?destroyed:completed==1?added:recruited,"Effect outcome missing");
      resolving=false;solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved && !resolving,"Incomplete chain");++completed;
      announced=confirmed=solved=false;
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(e.player==0 && e.count==1 && !(e.mask&1u) &&
              (resolving?recruit_selected:normal_requested && !normal_summoned),"Unexpected placement");
      d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(e.player==0 && resolving && recruit_selected && e.cards.at(0).code==jerry_beans &&
              (e.mask&POS_FACEUP_ATTACK),"Unexpected position choice");d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SUMMONING) {
      require(normal_requested && !normal_summoning && e.cards.at(0).code==lonefire &&
              e.cards[0].controller==0 && e.cards[0].location==LOCATION_MZONE &&
              e.cards[0].sequence==0,"Unexpected Normal Summon");normal_summoning=true;
    } else if(e.kind==MSG_SUMMONED) {
      require(normal_summoning && !normal_summoned,"Unexpected summon success");normal_summoned=true;
    } else if(e.kind==MSG_SPSUMMONING) {
      require(resolving && recruit_selected && !recruit_summoning && e.cards.at(0).code==jerry_beans &&
              e.cards[0].controller==0 && e.cards[0].location==LOCATION_MZONE &&
              e.cards[0].sequence==0,"Unexpected Special Summon");recruit_summoning=true;
    } else if(e.kind==MSG_SPSUMMONED) {
      require(resolving && recruit_summoning && !recruited,"Unexpected Special Summon success");recruited=true;
    } else if(e.kind==MSG_SELECT_YESNO || e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_TRIBUTE ||
              e.kind==MSG_PAY_LPCOST || e.kind==MSG_DRAW || e.kind==MSG_CHAIN_DISABLED ||
              e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_SELECT_BATTLECMD) {
      throw std::runtime_error("Unexpected action in Sangan search restriction fixture");
    }
    return false;
  });
}
