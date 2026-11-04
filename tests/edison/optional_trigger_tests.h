#pragma once
#include "harness.h"

// Test 005 draws Watapon during Dealings; Test 006 draws it with Jar at CL2
// before Dealings discards Broww at CL1. Bell keeps the hand trigger public.
// Broww is SEGOC Step 1 and Watapon Step 3, so under both rules Broww is CL1 despite its later event.
inline void optional_early_trigger(certification::Assets& assets, int options,
                                   bool accept, bool trace, bool across_links = false) {
  using namespace certification;
  TestDuel d(assets, 0, 0, trace,options);
  d.add({bell, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_DEFENSE});
  d.add({dealings, 0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE});
  d.add({broww, 0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes, 0, 0, LOCATION_DECK, SEQ_DECKTOP, POS_FACEDOWN_DEFENSE});
  if(across_links) {
    d.add({jar, 0, 0, LOCATION_SZONE, 1, POS_FACEDOWN_DEFENSE});
    d.add({blue_eyes, 0, 0, LOCATION_DECK, SEQ_DECKTOP, POS_FACEDOWN_DEFENSE});
  }
  d.add({watapon, 0, 0, LOCATION_DECK, SEQ_DECKTOP, POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes, 1, 1, LOCATION_DECK, SEQ_DECKTOP, POS_FACEDOWN_DEFENSE});
  const std::vector<uint32_t> expected = !accept ? std::vector<uint32_t>{broww}
                                                 : std::vector<uint32_t>{broww, watapon};
  const size_t setup_links=across_links?2:1;
  bool jar_requested=false, jar_drawn=false;
  bool requested=false, setup=false, decided=false, ended=false, special=false;
  bool drawn[2]={false,false}, discarded[2]={false,false}, selected[2]={false,false};
  size_t announced=0, confirmed=0, resolved=0;
  int resolving=0, extra_draws=0;
  auto name = [](uint32_t code) { return code==watapon ? "Watapon" : "Broww"; };
  auto choose_optional = [&] {
    require(setup && !decided && !ended, "Optional trigger absent, repeated, or outside trigger window");
    require(confirmed==1 && announced==confirmed,
            "Optional decision (Step 3) must follow mandatory Broww (Step 1)");
    bool public_watapon=false;
    for(int i=0;i<d.count(0,LOCATION_HAND);++i)
      if(d.code(0,LOCATION_HAND,i)==watapon && (d.position(0,LOCATION_HAND,i)&POS_FACEUP))
        public_watapon=true;
    require(d.count(0,LOCATION_HAND)==(across_links?2:1) && public_watapon,
            "Watapon must remain publicly revealed and eligible");
    decided=true;
    std::cout << "Observed: " << (accept ? "accept" : "decline")
              << " Watapon after " << confirmed << " confirmed trigger links.\n";
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0, "Unexpected idle player");
      if(requested) {
        require(ended && decided && confirmed==expected.size() && resolved==expected.size() &&
                extra_draws==1 && special==accept, "Incomplete trigger chain or optional decision");
        require(d.lp(0)==8000 && d.lp(1)==8000 && d.count(0,LOCATION_DECK)==0 &&
                d.count(1,LOCATION_DECK)==0 && d.count(1,LOCATION_HAND)==0,
                "Unexpected LP, deck, or opponent hand state");
        require(d.count(0,LOCATION_HAND)==(accept?1:2)+int(across_links) && d.count(0,LOCATION_MZONE)==int(accept) &&
                d.count(0,LOCATION_GRAVE)==2+int(across_links) && d.code(0,LOCATION_GRAVE,0)==broww && d.count(0,LOCATION_SZONE)==0 &&
                d.count(1,LOCATION_GRAVE)==1 && d.code(1,LOCATION_GRAVE,0)==blue_eyes &&
                d.count(1,LOCATION_MZONE)==1 && d.code(1,LOCATION_MZONE,0)==bell,
                "Final fixture zones differ");
        std::vector<uint32_t> hand, grave, expected_hand(1+int(across_links),blue_eyes);
        if(!accept) expected_hand.push_back(watapon);
        for(int i=0;i<d.count(0,LOCATION_HAND);++i) hand.push_back(d.code(0,LOCATION_HAND,i));
        for(int i=0;i<d.count(0,LOCATION_GRAVE);++i) grave.push_back(d.code(0,LOCATION_GRAVE,i));
        std::vector<uint32_t> expected_grave{broww,dealings};
        if(across_links) expected_grave.push_back(jar);
        for(auto* list : {&hand,&grave,&expected_hand,&expected_grave}) std::sort(list->begin(),list->end());
        require(hand==expected_hand && grave==expected_grave,"Unexpected final hand or Graveyard identities");
        if(accept) require(d.code(0,LOCATION_MZONE,0)==watapon &&
                           d.position(0,LOCATION_MZONE,0)==POS_FACEUP_ATTACK,
                           "Accepted Watapon must resolve to the field");
        return true;
      }
      int index=-1;
      for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==dealings) index=int(i);
      require(index>=0 && d.code(0,LOCATION_DECK,1+int(across_links))==watapon, "Expected Dealings and Watapon on top");
      requested=true; d.respond((index<<16)|5);
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(requested && e.player==0 && e.count==1, "Unexpected placement");
      if(!setup) {
        require(!(e.mask&(1u<<8)), "Dealings requires S/T zone 0");
        d.respond({0,LOCATION_SZONE,0});
      } else {
        require(accept && resolving>0 && expected.at(resolving-1)==watapon && !(e.mask&1u),
                "Watapon placement outside its resolution");
        d.respond({0,LOCATION_MZONE,0});
      }
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(setup && accept && resolving>0 && expected.at(resolving-1)==watapon &&
              e.player==0 && e.cards.at(0).code==watapon && (e.mask&POS_FACEUP_ATTACK),
              "Unexpected battle position selection");
      d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SELECT_CARD) {
      require(!setup && resolving==1 && e.player>=0 && e.player<=1 &&
              drawn[0] && drawn[1] && !selected[e.player] && e.minimum==1 && e.maximum==1,
              "Expected Dealings discard after both draws");
      uint32_t want=e.player==0?broww:blue_eyes;
      int index=-1;
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want && e.cards[i].controller==e.player &&
           e.cards[i].location==LOCATION_HAND) index=int(i);
      require(index>=0, "Required discard unavailable");
      selected[e.player]=true; d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_MOVE && e.to.location==LOCATION_GRAVE &&
              (e.from.code==broww || e.from.code==blue_eyes)) {
      int p=e.from.controller;
      require(!setup && p>=0 && p<=1 && drawn[0] && drawn[1] && !discarded[p] &&
              e.from.location==LOCATION_HAND && e.to.controller==p &&
              e.from.code==(p==0?broww:blue_eyes) &&
              (e.reason&(REASON_EFFECT|REASON_DISCARD))==(REASON_EFFECT|REASON_DISCARD) &&
              !(e.reason&REASON_COST), "Expected later discard by effect, not cost");
      discarded[p]=true;
    } else if(e.kind==MSG_DRAW) {
      require(e.player>=0 && e.player<=1 && e.count==1, "Unexpected draw count");
      uint32_t code=e.cards.at(0).code&0x7fffffffu;
      if(!setup && across_links && resolving==2) {
        require(jar_requested && !jar_drawn && !drawn[0] && !drawn[1] &&
                e.player==0 && code==watapon,"Jar CL2 must draw Watapon first");
        jar_drawn=true;
      } else if(!setup) {
        require((!across_links || jar_drawn) && resolving==1 && !drawn[e.player] && !discarded[0] && !discarded[1] &&
                code==(e.player==0 && !across_links?watapon:blue_eyes), "Dealings must draw before discarding");
        drawn[e.player]=true;
      } else {
        require(resolving>0 && expected.at(resolving-1)==broww && e.player==0 &&
                code==blue_eyes && extra_draws==0, "Broww must draw exactly one filler");
        ++extra_draws;
      }
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(e.player==0 && e.cards.size()==1 && e.cards[0].code==watapon,
              "Expected only Watapon as optional trigger");
      choose_optional(); d.respond(accept?1:0);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) { require(!e.forced,"Empty forced prompt"); d.respond(-1); }
      else if(!setup && across_links) {
        require(!e.forced && e.player==0 && e.cards.size()==1 && e.cards[0].code==jar,
                "Triggers must wait until the original chain finishes");
        if(requested && confirmed==1 && !jar_requested) {
          jar_requested=true; d.respond(0);
        } else d.respond(-1);
      } else {
        require(!e.forced && e.player==0 && e.cards.size()==1 && e.cards[0].code==watapon,
                "Unexpected effect or mandatory ordering choice");
        choose_optional(); d.respond(accept?0:-1);
      }
    } else if(e.kind==MSG_CHAINING) {
      if(!setup) {
        require(requested && announced==confirmed && announced<setup_links &&
                e.chain==int(announced+1) && e.cards.at(0).code==(announced==0?dealings:jar) &&
                e.player==0 && (announced==0 || jar_requested), "Unexpected setup chain");
      } else {
        require(!ended && announced==confirmed && announced<expected.size() &&
                e.chain==int(announced+1) && e.cards.at(0).code==expected[announced] &&
                e.player==0, "Earlier optional/later mandatory chain order violated");
        if(e.cards[0].code==broww) require(!decided, "Broww must be on the Chain before the optional decision");
        if(e.cards[0].code==watapon) require(decided && accept, "Declined optional activated");
      }
      ++announced;
    } else if(e.kind==MSG_CHAINED) {
      require(announced==confirmed+1 && e.chain==int(confirmed+1), "Unexpected confirmed chain link");
      if(setup) std::cout << "Observed: CL" << e.chain << " = P0 " << name(expected.at(confirmed)) << ".\n";
      ++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(!resolving && (setup || confirmed==setup_links) && e.chain==int(confirmed-resolved) &&
              (!setup || (decided && confirmed==expected.size())),
              "Chain resolved before all trigger choices or in wrong order");
      resolving=e.chain;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving==e.chain, "Unexpected solved chain link");
      resolving=0; ++resolved;
    } else if(e.kind==MSG_SPSUMMONING) {
      require(setup && accept && !special && resolving>0 && expected.at(resolving-1)==watapon &&
              e.cards.at(0).code==watapon && e.cards[0].controller==0 &&
              e.cards[0].location==LOCATION_MZONE, "Unexpected Special Summon");
    } else if(e.kind==MSG_SPSUMMONED) {
      require(setup && accept && !special && resolving>0 && expected.at(resolving-1)==watapon,
              "Unexpected Special Summon success");
      special=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(!resolving && confirmed==resolved && !ended, "Incomplete or extra chain");
      if(!setup) {
        require(confirmed==setup_links && (!across_links || jar_drawn) &&
                drawn[0] && drawn[1] && discarded[0] && discarded[1] &&
                d.code(0,LOCATION_GRAVE,0)==broww,
                "Dealings chronology not established");
        if(across_links)
          std::cout << "Observed: Jar CL2 drew Watapon, then Dealings CL1 discarded Broww; "
                       "both triggers waited for the original chain to end.\n";
        setup=true; announced=confirmed=resolved=0;
      } else {
        require(decided && confirmed==expected.size(), "Trigger chain ended early");
        ended=true;
      }
    } else if(e.kind==MSG_SELECT_YESNO || e.kind==MSG_SELECT_TRIBUTE ||
              e.kind==MSG_SUMMONING || e.kind==MSG_PAY_LPCOST) {
      throw std::runtime_error("Unexpected action in optional-trigger fixture");
    }
    return false;
  });
}
