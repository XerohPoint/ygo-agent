#pragma once
#include "harness.h"

// Ordering checkpoints intentionally stop before trigger effects resolve:
// the installed Sangan script has post-Edison search restrictions.
inline void trigger_ordering(certification::Assets& assets, int options,
                             bool early, bool historical, bool trace, bool opponent_sangan = false) {
  using namespace certification;
  TestDuel d(assets, 0, 0, trace,options);
  d.add({early ? caius : bazoo, 0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE});
  d.add({sangan, uint8_t(opponent_sangan), uint8_t(opponent_sangan), LOCATION_MZONE, uint8_t(opponent_sangan), POS_FACEUP_ATTACK});
  if (early) {
    d.add({blue_eyes, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
  } else {
    d.add({sangan, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
    for (uint8_t p : {0, 1}) {
      d.add({dupe, p, p, LOCATION_MZONE, 1, POS_FACEUP_DEFENSE});
      d.add({treeborn, p, p, LOCATION_DECK, 0, POS_FACEDOWN_DEFENSE});
    }
    d.add({torrential, 0, 0, LOCATION_SZONE, 0, POS_FACEDOWN_DEFENSE});
  }
  if(opponent_sangan) d.add({brain, 0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE});
  using Link = std::pair<uint32_t, int>;
  const std::vector<Link> expected = early
      ? (historical ? std::vector<Link>{{sangan,int(opponent_sangan)},{caius,0}}
                    : std::vector<Link>{{caius,0},{sangan,int(opponent_sangan)}})
      : std::vector<Link>{{torrential,0},{sangan,0},{sangan,1},{dupe,0},{dupe,1}};
  bool requested=false, summoned=false, tribute=false, destroyed=false, target=false, choice=false;
  size_t announced=0, confirmed=0;
  auto names = [](uint32_t c) {
    return c == sangan ? "Sangan" : c == caius ? "Caius" : c == dupe ? "Dupe Frog" : "Torrential Tribute";
  };
  bool controlled = !opponent_sangan, control_requested = false, control_target = false;
  d.start(options);
  d.run([&](const Event& e) {
    if(!controlled) {
      if(e.kind==MSG_SELECT_IDLECMD) {
        require(!control_requested && e.player==0, "Expected Brain Control setup");
        int index=-1;
        for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==brain) index=int(i);
        require(index>=0,"Brain Control activation unavailable");
        control_requested=true; d.respond((index<<16)|5);
      } else if(e.kind==MSG_SELECT_CARD) {
        require(control_requested && !control_target && e.player==0 && e.minimum==1 && e.maximum==1,
                "Unexpected Brain Control target prompt");
        int index=-1;
        for(size_t i=0;i<e.cards.size();++i)
          if(e.cards[i].code==sangan && e.cards[i].controller==1 && e.cards[i].location==LOCATION_MZONE)
            index=int(i);
        require(index>=0,"Opponent's Sangan unavailable");
        control_target=true; d.respond({1,static_cast<byte>(index)});
      } else if(e.kind==MSG_SELECT_CHAIN) {
        require(e.cards.empty() && !e.forced,"Unexpected setup response");
        d.respond(-1);
      } else if(e.kind==MSG_SELECT_PLACE) {
        require(control_requested && e.player==0 && e.count==1, "Expected setup placement");
        require(!(e.mask & (control_target ? 1u : 1u<<8)), "Expected available setup zone");
        d.respond({0,static_cast<byte>(control_target ? LOCATION_MZONE : LOCATION_SZONE),0});
      } else if(e.kind==MSG_CHAINING) {
        require(control_requested && e.chain==1 && e.cards.at(0).code==brain,"Unexpected setup chain");
      } else if(e.kind==MSG_PAY_LPCOST) {
        require(e.player==0 && e.mask==800,"Unexpected setup LP cost");
      } else if(e.kind==MSG_CHAIN_END) {
        require(control_target && d.code(0,LOCATION_MZONE,0)==sangan && d.lp(0)==7200,
                "Brain Control must resolve before Tribute Summon");
        controlled=true;
      }
      return false;
    }

    if (e.kind == MSG_SELECT_IDLECMD) {
      require(!requested && e.player == 0, "Returned to idle before trigger chain checkpoint");
      auto& list=e.idle[0];
      require(list.size()==1 && list[0].code==(early?caius:bazoo), "Expected fixture Normal Summon");
      requested=true; d.respond(0);
    } else if(e.kind == MSG_SELECT_YESNO) {
      require(early && requested && !summoned && e.player==0, "Unexpected yes/no prompt");
      d.respond(1);
    } else if(e.kind == MSG_SELECT_TRIBUTE) {
      require(early && requested && !tribute && e.player==0 && e.minimum==1 &&
              e.maximum==1 && e.cards.size()==1 && e.cards[0].code==sangan &&
              e.cards[0].controller==0 && e.cards[0].location==LOCATION_MZONE,
              "Expected Sangan as the sole Tribute");
      tribute=true; d.respond({1,0});
    } else if(e.kind == MSG_SELECT_PLACE) {
      uint8_t zone=early?0:2;
      require(requested && !summoned && e.player==0 && e.count==1 && !(e.mask & (1u<<zone)),
              "Expected free summon zone");
      d.respond({0,LOCATION_MZONE,zone});
    } else if(e.kind == MSG_SUMMONED) {
      require(requested && !summoned, "Unexpected Summon success");
      summoned=true;
      if(early) require(tribute && d.code(int(opponent_sangan),LOCATION_GRAVE,0)==sangan &&
                         d.code(0,LOCATION_MZONE,0)==caius, "Tribute chronology not established");
    } else if(e.kind == MSG_SELECT_EFFECTYN) {
      require(!early && destroyed && e.cards.size()==1 && e.cards[0].code==dupe,
              "Expected optional Dupe Frog trigger");
      d.respond(1);
    } else if(e.kind == MSG_SELECT_CHAIN) {
      if(e.cards.empty()) { require(!e.forced,"Empty forced trigger prompt"); d.respond(-1); return false; }
      require(summoned, "Effect offered before successful Summon");
      int selected=0;
      if(early) {
        require(!opponent_sangan, "Cross-player triggers must not offer an ordering choice");
        require(e.player==0 && e.forced, "Expected mandatory turn-player triggers");
        if(confirmed==0) {
          std::cout << "Observed: initial mandatory candidates:";
          for(const auto& c:e.cards) std::cout << ' ' << names(c.code);
          std::cout << '\n';
          if(historical) require(e.cards.size()==1 && e.cards[0].code==sangan,
                                 "Early Trigger violated: Sangan must be forced before Caius");
          else {
            require(e.cards.size()==2, "Comparison must offer both mandatory effects for ordering");
            bool saw_sangan=false;
            for(size_t i=0;i<e.cards.size();++i) {
              saw_sangan |= e.cards[i].code==sangan;
              if(e.cards[i].code==caius) {selected=int(i);choice=true;}
            }
            require(saw_sangan && choice, "Comparison did not allow Caius-first ordering");
          }
        } else require(e.cards.size()==1 && e.cards[0].code==expected.at(confirmed).first,
                       "Unexpected remaining mandatory trigger");
      } else {
        require(e.cards.size()==1 && confirmed<expected.size() &&
                e.cards[0].code==expected[confirmed].first && e.player==expected[confirmed].second,
                "SEGOC prompt offered out of mandatory/optional or turn-player order");
      }
      d.respond(selected);
    } else if(e.kind == MSG_SELECT_CARD) {
      require(early && summoned && !target && e.player==0 && e.minimum==1 && e.maximum==1,
              "Unexpected target selection");
      int index=-1;
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==blue_eyes && e.cards[i].controller==1 && e.cards[i].location==LOCATION_MZONE)
          index=int(i);
      require(index>=0, "Caius must target opponent's Blue-Eyes");
      target=true; d.respond({1,static_cast<byte>(index)});
    } else if(e.kind == MSG_CHAINING) {
      require(announced==confirmed && announced<expected.size(), "Unexpected chain announcement");
      auto want=expected[announced];
      int link=early?int(announced+1):(announced==0?1:int(announced));
      require(e.chain==link && e.cards.at(0).code==want.first && e.player==want.second,
              "Trigger chain order differs from expected ordering");
      if(!early && announced>0) require(destroyed, "Triggers started before simultaneous destruction finished");
      ++announced;
    } else if(e.kind == MSG_CHAINED) {
      require(announced==confirmed+1, "Unannounced confirmed link");
      int link=early?int(confirmed+1):(confirmed==0?1:int(confirmed));
      require(e.chain==link, "Unexpected confirmed chain number");
      std::cout << "Observed: CL" << link << " = P" << expected[confirmed].second << ' '
                << names(expected[confirmed].first) << ".\n";
      ++confirmed;
      if(confirmed==expected.size()) {
        if(early) require(target && (historical || opponent_sangan || choice), "Missing Caius target or comparison choice");
        else require(destroyed && d.count(0,LOCATION_MZONE)==0 && d.count(1,LOCATION_MZONE)==0,
                     "SEGOC fixture monsters did not leave the field");
        return true;
      }
    } else if(e.kind == MSG_CHAIN_END) {
      require(!early && confirmed==1 && !destroyed, "Trigger chain ended before ordering checkpoint");
      require(d.count(0,LOCATION_GRAVE)==4 && d.count(1,LOCATION_GRAVE)==2 &&
              d.count(0,LOCATION_MZONE)==0 && d.count(1,LOCATION_MZONE)==0,
              "Torrential must destroy all five monsters before trigger chain");
      destroyed=true;
    }
    return false;
  });
}
