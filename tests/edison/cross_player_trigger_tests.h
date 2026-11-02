#pragma once
#include "harness.h"

// Test 018: Dealings draws one player's private Watapon before discarding
// the other player's Broww. P0 always activates Dealings.
inline void cross_player_early_trigger(certification::Assets& assets, int options,
                                      bool historical, bool accept, uint8_t optional_player, bool trace) {
  using namespace certification;
  const uint8_t op=optional_player, mp=1-op;
  const int reward=mp==0?1:2; // Opponent's Dealings gives Broww its additional draw.
  TestDuel d(assets,0,0,trace,options);
  d.add({dealings,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes,op,op,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  d.add({broww,mp,mp,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  d.add({watapon,op,op,LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE});
  for(int i=0;i<reward+1;++i)
    d.add({blue_eyes,mp,mp,LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE});
  const std::vector<uint32_t> expected=!accept?std::vector<uint32_t>{broww}
      :historical?std::vector<uint32_t>{watapon,broww}:std::vector<uint32_t>{broww,watapon};
  bool requested=false,setup=false,decided=false,ended=false,special=false;
  bool drawn[2]={},selected[2]={},discarded[2]={};
  size_t announced=0,confirmed=0,resolved=0;
  int resolving=0,extra_draws=0;
  auto choose_optional=[&] {
    require(setup && !ended && !decided && confirmed==size_t(historical?0:1) &&
            announced==confirmed,"Cross-player optional decision offered in wrong order or repeated");
    require(d.count(op,LOCATION_HAND)==1 && d.code(op,LOCATION_HAND,0)==watapon &&
            (d.position(op,LOCATION_HAND,0)&POS_FACEDOWN),"Watapon must still be private in hand");
    decided=true;
    std::cout << "Observed: P" << int(op) << (accept?" accepts":" declines")
              << " Watapon after " << confirmed << " confirmed triggers.\n";
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0,"Unexpected idle player");
      if(requested) {
        require(ended && decided && confirmed==expected.size() && resolved==expected.size() &&
                extra_draws==reward && special==accept,"Cross-player trigger sequence incomplete");
        for(int p=0;p<2;++p) {
          require(d.lp(p)==8000 && d.count(p,LOCATION_DECK)==0 && d.count(p,LOCATION_SZONE)==0,
                  "Unexpected final LP, deck, or S/T zone");
          require(d.count(p,LOCATION_MZONE)==int(p==op && accept),"Unexpected final monster count");
          std::vector<uint32_t> hand,grave;
          for(int i=0;i<d.count(p,LOCATION_HAND);++i) hand.push_back(d.code(p,LOCATION_HAND,i));
          for(int i=0;i<d.count(p,LOCATION_GRAVE);++i) grave.push_back(d.code(p,LOCATION_GRAVE,i));
          std::vector<uint32_t> want_hand=p==mp?std::vector<uint32_t>(reward+1,blue_eyes)
              :accept?std::vector<uint32_t>{}:std::vector<uint32_t>{watapon};
          std::vector<uint32_t> want_grave{p==mp?broww:blue_eyes};
          if(p==0) want_grave.push_back(dealings);
          for(auto* v:{&hand,&grave,&want_hand,&want_grave}) std::sort(v->begin(),v->end());
          require(hand==want_hand && grave==want_grave,"Final cross-player card identities differ");
        }
        if(accept) require(d.code(op,LOCATION_MZONE,0)==watapon &&
                            d.position(op,LOCATION_MZONE,0)==POS_FACEUP_ATTACK,"Watapon not summoned correctly");
        return true;
      }
      int index=-1;
      for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==dealings) index=int(i);
      require(index>=0,"Dealings unavailable");
      requested=true; d.respond((index<<16)|5);
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(e.count==1,"Unexpected placement count");
      if(!setup) {
        require(requested && e.player==0 && !(e.mask&(1u<<8)),"Expected Dealings placement");
        d.respond({0,LOCATION_SZONE,0});
      } else {
        require(accept && e.player==op && resolving>0 && expected.at(resolving-1)==watapon &&
                !(e.mask&1u),"Unexpected Watapon placement");
        d.respond({op,LOCATION_MZONE,0});
      }
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(setup && accept && e.player==op && resolving>0 && expected.at(resolving-1)==watapon &&
              e.cards.at(0).code==watapon && (e.mask&POS_FACEUP_ATTACK),"Unexpected position choice");
      d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SELECT_CARD) {
      require(!setup && resolving==1 && e.player>=0 && e.player<=1 && drawn[0] && drawn[1] &&
              !selected[e.player] && e.minimum==1 && e.maximum==1,"Unexpected discard choice");
      int index=-1;
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==(e.player==mp?broww:blue_eyes) && e.cards[i].controller==e.player &&
           e.cards[i].location==LOCATION_HAND) index=int(i);
      require(index>=0,"Required discard unavailable");
      selected[e.player]=true;d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_MOVE && e.from.location==LOCATION_HAND && e.to.location==LOCATION_GRAVE) {
      const int p=e.from.controller;
      require(!setup && resolving==1 && p>=0 && p<=1 && drawn[0] && drawn[1] && selected[p] &&
              !discarded[p] && e.to.controller==p && e.from.code==(p==mp?broww:blue_eyes) &&
              (e.reason&(REASON_EFFECT|REASON_DISCARD))==(REASON_EFFECT|REASON_DISCARD) &&
              !(e.reason&REASON_COST),"Expected discard after both draws");
      discarded[p]=true;
    } else if(e.kind==MSG_DRAW) {
      if(!setup) {
        require(resolving==1 && e.player>=0 && e.player<=1 && e.count==1 && !drawn[e.player] &&
                !discarded[0] && !discarded[1] &&
                (e.cards.at(0).code&0x7fffffffu)==(e.player==op?watapon:blue_eyes),
                "Expected both Dealings draws before discards");
        drawn[e.player]=true;
      } else {
        require(resolving>0 && expected.at(resolving-1)==broww && e.player==mp &&
                e.count==reward && extra_draws==0,"Incorrect Broww reward");
        for(const auto& c:e.cards) require((c.code&0x7fffffffu)==blue_eyes,"Unexpected Broww draw identity");
        extra_draws=e.count;
      }
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(e.player==op && e.cards.size()==1 && e.cards[0].code==watapon,"Unexpected optional effect");
      choose_optional();d.respond(accept?1:0);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) { require(!e.forced,"Empty forced prompt");d.respond(-1); }
      else {
        require(!e.forced && e.player==op && e.cards.size()==1 && e.cards[0].code==watapon,
                "Unexpected trigger or ordering choice");
        choose_optional();d.respond(accept?0:-1);
      }
    } else if(e.kind==MSG_CHAINING) {
      if(!setup) {
        require(requested && announced==0 && e.chain==1 && e.player==0 &&
                e.cards.at(0).code==dealings,"Unexpected setup chain");
      } else {
        std::cout << "Observed: CL" << e.chain << " = P" << e.player << ' '
                  << (e.cards.at(0).code==watapon?"Watapon":"Broww") << ".\n";
        require(!ended && announced==confirmed && announced<expected.size() &&
                e.chain==int(announced+1) && e.cards.at(0).code==expected[announced] &&
                e.player==(expected[announced]==watapon?op:mp),"Cross-player Early Trigger order violated");
        require(!historical || decided,"Later mandatory trigger bypassed earlier optional decision");
        if(e.cards[0].code==watapon) require(decided && accept,"Declined Watapon activated");
      }
      ++announced;
    } else if(e.kind==MSG_CHAINED) {
      require(announced==confirmed+1 && e.chain==int(confirmed+1),"Unexpected confirmed link");
      ++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(!resolving && e.chain==int(confirmed-resolved) &&
              (setup?(decided && confirmed==expected.size()):confirmed==1),"Unexpected resolution order");
      resolving=e.chain;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving==e.chain,"Unexpected solved link");resolving=0;++resolved;
    } else if(e.kind==MSG_SPSUMMONING) {
      require(setup && accept && !special && resolving>0 && expected.at(resolving-1)==watapon &&
              e.cards.at(0).code==watapon && e.cards[0].controller==op &&
              e.cards[0].location==LOCATION_MZONE,"Unexpected Special Summon");
    } else if(e.kind==MSG_SPSUMMONED) {
      require(setup && accept && !special && resolving>0 && expected.at(resolving-1)==watapon,
              "Unexpected summon success");special=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(!ended && !resolving && confirmed==resolved,"Incomplete or extra chain");
      if(!setup) {
        require(confirmed==1 && drawn[0] && drawn[1] && discarded[0] && discarded[1] &&
                d.code(mp,LOCATION_GRAVE,0)==broww &&
                d.count(op,LOCATION_HAND)==1 && d.code(op,LOCATION_HAND,0)==watapon &&
                (d.position(op,LOCATION_HAND,0)&POS_FACEDOWN),"Private trigger chronology not established");
        setup=true;announced=confirmed=resolved=0;
      } else {
        require(decided && confirmed==expected.size(),"Trigger chain ended early");ended=true;
      }
    } else if(e.kind==MSG_SELECT_YESNO || e.kind==MSG_SELECT_TRIBUTE ||
              e.kind==MSG_SUMMONING || e.kind==MSG_PAY_LPCOST) {
      throw std::runtime_error("Unexpected action in cross-player fixture");
    }
    return false;
  });
}
