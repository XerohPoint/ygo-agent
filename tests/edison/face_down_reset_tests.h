#pragma once
#include "harness.h"

enum class FaceDownReset { PendingReset, StayFaceUp, BeforeApplication, ChaliceReflip, DrainReflip };

inline void face_down_reset(certification::Assets& assets, int options,
                            FaceDownReset mode, bool trace) {
  using namespace certification;
  const bool reflip=mode>=FaceDownReset::ChaliceReflip;
  const bool drain=mode==FaceDownReset::DrainReflip;
  const bool disabled=mode==FaceDownReset::StayFaceUp||drain;
  const bool use_book=mode!=FaceDownReset::StayFaceUp;
  const uint32_t negator=drain?skill_drain:chalice;
  std::vector<std::vector<uint32_t>> plan;
  if(reflip) plan={{negator},{book_moon},{dekoichi}};
  else if(mode==FaceDownReset::PendingReset) plan={{dekoichi,book_moon,chalice}};
  else if(mode==FaceDownReset::BeforeApplication) plan={{dekoichi,chalice,book_moon}};
  else plan={{dekoichi,chalice}};
  TestDuel d(assets,0,0,trace,options);
  d.add({dekoichi,0,0,LOCATION_MZONE,0,
         static_cast<uint8_t>(reflip?POS_FACEUP_ATTACK:POS_FACEDOWN_DEFENSE)});
  d.add({blue_eyes,0,0,LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE});
  d.add({negator,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({book_moon,0,0,LOCATION_SZONE,1,POS_FACEDOWN_DEFENSE});
  size_t chain_no=0;
  int announced=0,confirmed=0,resolving=0;
  std::vector<int> solved;
  std::array<bool,4> targeted{};
  bool setup_requested=false,response_requested=false,flip_requested=false,flipping=false,flipped=false;
  bool negator_flipped=false,book_flipped=false,negator_sent=false,book_sent=false;
  bool negator_resolved=false,chalice_applied=false,turned_down=false,book_resolved=false;
  bool paid=false,drawn=false,effect_disabled=false,book_offered=false;
  auto current=[&]() -> const std::vector<uint32_t>& {
    require(chain_no<plan.size(),"Unexpected extra chain"); return plan[chain_no];
  };
  auto card_at=[&](int link) {
    require(link>=1&&link<=int(current().size()),"Unexpected link index");
    return current()[link-1];
  };
  auto check_monster=[&](uint8_t position,uint32_t atk) {
    require(d.count(0,LOCATION_MZONE)==1&&d.code(0,LOCATION_MZONE,0)==dekoichi&&
            d.position(0,LOCATION_MZONE,0)==position,"Wrong Dekoichi position/identity");
    if(position&POS_FACEUP)
      require(d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==atk,"Wrong ATK after applying/resetting Chalice");
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {
      require(e.player==0&&!flip_requested,"Unexpected turn progression");
    } else if(e.kind==MSG_NEW_PHASE) {
      require(e.phase==PHASE_DRAW||e.phase==PHASE_STANDBY||e.phase==PHASE_MAIN1,
              "Reset must occur in the same Main Phase, not expire in the End Phase");
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0&&d.phase()==PHASE_MAIN1,"Expected P0 Main Phase");
      if(chain_no==plan.size()) {
        require(flipped&&negator_resolved&&negator_flipped&&
                chalice_applied==(!drain&&mode!=FaceDownReset::BeforeApplication)&&
                turned_down==use_book&&book_resolved==use_book&&book_flipped==use_book&&
                book_sent==use_book&&negator_sent==!drain&&paid==drain&&
                drawn==!disabled&&effect_disabled==disabled&&book_offered,
                "Missing face-down reset checkpoint");
        check_monster(!reflip&&use_book?POS_FACEDOWN_DEFENSE:POS_FACEUP_ATTACK,
                      mode==FaceDownReset::StayFaceUp?1800:1400);
        require(d.lp(0)==(drain?7000:8000)&&d.lp(1)==8000,"Wrong LP/cost state");
        require(d.count(0,LOCATION_HAND)==int(!disabled)&&d.count(0,LOCATION_DECK)==int(disabled),
                "Wrong Flip draw outcome");
        require(d.code(0,disabled?LOCATION_DECK:LOCATION_HAND,0)==blue_eyes,"Wrong drawn/undrawn identity");
        require(d.count(0,LOCATION_SZONE)==int(drain)+int(!use_book),"Wrong final Spell/Trap count");
        if(drain) require(d.code(0,LOCATION_SZONE,0)==skill_drain&&
                          (d.position(0,LOCATION_SZONE,0)&POS_FACEUP),"Skill Drain must remain active");
        if(!use_book) require(d.code(0,LOCATION_SZONE,1)==book_moon&&
                             d.position(0,LOCATION_SZONE,1)==POS_FACEDOWN_DEFENSE,
                             "Declined Book must remain Set");
        std::vector<uint32_t> grave,want;
        for(int i=0;i<d.count(0,LOCATION_GRAVE);++i) grave.push_back(d.code(0,LOCATION_GRAVE,i));
        if(!drain) want.push_back(chalice);
        if(use_book) want.push_back(book_moon);
        std::sort(grave.begin(),grave.end()); std::sort(want.begin(),want.end());
        require(grave==want,"Unexpected Graveyard identities");
        for(int loc:{LOCATION_HAND,LOCATION_DECK,LOCATION_MZONE,LOCATION_SZONE,LOCATION_GRAVE,
                     LOCATION_REMOVED,LOCATION_EXTRA}) require(d.count(1,loc)==0,"Unexpected opposing cards");
        require(d.count(0,LOCATION_REMOVED)==0&&d.count(0,LOCATION_EXTRA)==0,"Unexpected removed/Extra Deck cards");
        std::cout<<"Observed: "<<(disabled?(drain?"Skill Drain reapplied after Flip Summon; no draw":
                                                "Dekoichi stayed face-up under Chalice; no draw"):
                                      mode==FaceDownReset::BeforeApplication?
                                                "Book resolved first, preventing Chalice from applying; draw":
                                      reflip?"Chalice reset; Flip Summon at base ATK drew normally":
                                             "applied Chalice reset within the pending chain; draw")
                 <<". Chain order, positions, final zones, and LP verified.\n";
        return true;
      }
      if(reflip&&chain_no<2) {
        require(!setup_requested&&!announced,"Setup returned to idle early");
        if(chain_no==1) {
          require(negator_resolved&&!turned_down,"Negator must resolve before Book activation");
          check_monster(POS_FACEUP_ATTACK,drain?1400:1800);
        }
        int index=-1;
        for(size_t i=0;i<e.idle[5].size();++i)
          if(e.idle[5][i].code==current()[0]) index=int(i);
        require(index>=0,"Required setup activation missing");
        setup_requested=true; d.respond((index<<16)|5);
      } else {
        require(!flip_requested&&(reflip?turned_down&&book_resolved:true),"Unexpected Flip Summon request");
        check_monster(POS_FACEDOWN_DEFENSE,1400);
        require(e.idle[2].size()==1&&e.idle[2][0].code==dekoichi,"Expected legal Flip Summon action");
        flip_requested=true; d.respond(2);
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced chain selection");
      int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        const auto& c=e.cards[i];
        require(e.player==0&&c.controller==0&&c.location==LOCATION_SZONE&&
                (c.code==negator||c.code==book_moon),"Unexpected response candidate");
        if(c.code==book_moon) book_offered=true;
        if(!reflip&&chain_no<plan.size()&&flipped&&confirmed>0&&
           confirmed<int(current().size())&&c.code==card_at(confirmed+1)) {
          require(!response_requested&&announced==confirmed&&!resolving,"Repeated response selection");
          response_requested=true; pick=int(i);
        }
      }
      d.respond(pick);
    } else if(e.kind==MSG_FLIPSUMMONING) {
      const auto& c=e.cards.at(0);
      require(flip_requested&&!flipping&&c.code==dekoichi&&c.controller==0&&
              c.location==LOCATION_MZONE&&c.sequence==0&&c.position==POS_FACEUP_ATTACK,
              "Unexpected Flip Summon announcement"); flipping=true;
    } else if(e.kind==MSG_FLIPSUMMONED) {
      require(flipping&&!flipped,"Unexpected Flip Summon completion"); flipped=true;
    } else if(e.kind==MSG_POS_CHANGE) {
      if(e.from.code==dekoichi) {
        require(use_book&&resolving&&card_at(resolving)==book_moon&&!turned_down&&
                e.from.controller==0&&e.from.location==LOCATION_MZONE&&e.from.sequence==0&&
                e.from.position==POS_FACEUP_ATTACK&&e.to.position==POS_FACEDOWN_DEFENSE,
                "Book must turn the actor face-down during its resolution");
        if(mode==FaceDownReset::PendingReset||mode==FaceDownReset::ChaliceReflip)
          require(chalice_applied,"Reset must follow completed Chalice resolution");
        if(mode==FaceDownReset::BeforeApplication)
          require(!negator_resolved,"Pre-application control must Set before Chalice resolves");
        turned_down=true;
      } else {
        require(e.from.controller==0&&e.from.location==LOCATION_SZONE&&
                e.from.position==POS_FACEDOWN_DEFENSE&&(e.to.position&POS_FACEUP)&&
                e.from.code==card_at(announced+1),"Unexpected Spell/Trap flip");
        if(e.from.code==negator) { require(!negator_flipped,"Repeated negator activation"); negator_flipped=true; }
        else { require(e.from.code==book_moon&&!book_flipped,"Repeated Book activation"); book_flipped=true; }
      }
    } else if(e.kind==MSG_CHAINING) {
      const auto& c=e.cards.at(0);
      require(announced==confirmed&&!resolving&&e.chain==announced+1&&c.code==card_at(e.chain)&&
              e.player==0&&c.controller==0&&
              c.location==(c.code==dekoichi?LOCATION_MZONE:LOCATION_SZONE),
              "Unexpected chain card/order/location");
      require(c.code==dekoichi?flipped:(e.chain==1?setup_requested:response_requested),
              "Activation without requested action");
      ++announced;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced==confirmed+1&&!resolving&&card_at(announced)!=dekoichi&&
              card_at(announced)!=skill_drain&&!targeted[announced]&&e.player==0&&
              e.minimum==1&&e.maximum==1&&e.cards.size()==1&&e.cards[0].code==dekoichi&&
              e.cards[0].controller==0&&e.cards[0].location==LOCATION_MZONE&&
              e.cards[0].sequence==0&&(e.cards[0].position&POS_FACEUP),
              "Expected face-up Dekoichi as the sole spell target");
      targeted[announced]=true; d.respond({1,0});
    } else if(e.kind==MSG_PAY_LPCOST) {
      require(drain&&announced==1&&!confirmed&&card_at(1)==skill_drain&&!paid&&
              e.player==0&&e.mask==1000,"Expected Skill Drain's setup cost"); paid=true;
    } else if(e.kind==MSG_CHAINED) {
      require(e.chain==confirmed+1&&announced==e.chain,"Unexpected chain confirmation");
      auto code=card_at(e.chain);
      if(code==book_moon||code==chalice) require(targeted[e.chain],"Missing spell target");
      if(code==skill_drain) require(paid,"Missing Drain cost");
      ++confirmed; response_requested=false;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(!resolving&&confirmed==int(current().size())&&
              e.chain==int(current().size()-solved.size()),"Resolution must be in reverse link order");
      if(card_at(e.chain)==dekoichi)
        require(negator_resolved&&turned_down==use_book,"Monster resolved before required setup/Book");
      resolving=e.chain;
    } else if(e.kind==MSG_CHAIN_DISABLED) {
      require(disabled&&resolving==1&&card_at(1)==dekoichi&&e.chain==1&&!effect_disabled,
              "Only the expected face-up Flip effect should be disabled"); effect_disabled=true;
    } else if(e.kind==MSG_CHAIN_NEGATED) {
      throw std::runtime_error("Face-down reset fixture must not negate activation");
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving==e.chain,"Unexpected solved link");
      const auto code=card_at(e.chain);
      if(code==negator) {
        require(!negator_resolved,"Repeated negator resolution"); negator_resolved=true;
        if(!drain) {
          if(mode==FaceDownReset::BeforeApplication) require(turned_down,"Book must precede failed Chalice application");
          else { require(!turned_down,"Chalice must apply before Book"); chalice_applied=true; }
        }
      } else if(code==book_moon) {
        require(turned_down&&!book_resolved,"Book did not Set the monster"); book_resolved=true;
      } else require(effect_disabled==disabled&&drawn==!disabled,"Wrong Flip resolution");
      solved.push_back(e.chain); resolving=0;
    } else if(e.kind==MSG_CHAIN_END) {
      require(!resolving&&int(solved.size())==int(current().size()),"Incomplete chain");
      ++chain_no; announced=confirmed=0; solved.clear(); targeted.fill(false); setup_requested=false;
    } else if(e.kind==MSG_MOVE) {
      require(e.from.controller==0&&e.from.location==LOCATION_SZONE&&e.to.controller==0&&
              e.to.location==LOCATION_GRAVE&&!(e.reason&(REASON_COST|REASON_DESTROY|REASON_BATTLE)),
              "Only resolved spells may leave the field");
      if(e.from.code==chalice) {
        require(!drain&&negator_resolved&&!negator_sent,"Unexpected Chalice cleanup"); negator_sent=true;
      } else {
        require(e.from.code==book_moon&&book_resolved&&!book_sent,"Unexpected Book cleanup"); book_sent=true;
      }
    } else if(e.kind==MSG_DRAW) {
      require(!disabled&&resolving==1&&card_at(1)==dekoichi&&!drawn&&e.player==0&&e.count==1&&
              (e.cards.at(0).code&0x7fffffff)==blue_eyes,"Expected exactly one Flip draw"); drawn=true;
    } else if(e.kind==MSG_SELECT_EFFECTYN||e.kind==MSG_SELECT_YESNO||e.kind==MSG_SELECT_PLACE||
              e.kind==MSG_SELECT_POSITION||e.kind==MSG_SELECT_TRIBUTE||e.kind==MSG_SELECT_BATTLECMD||
              e.kind==MSG_DAMAGE||e.kind==MSG_DAMAGE_STEP_START||e.kind==MSG_SUMMONING||
              e.kind==MSG_SPSUMMONING||e.kind==MSG_SET) {
      throw std::runtime_error("Unexpected action in face-down reset fixture");
    }
    return false;
  });
}
