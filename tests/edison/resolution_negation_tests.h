#pragma once
#include "harness.h"

enum class ResolutionCase { FaceUp, FaceDown, Returned, DrainRemoved, DrainChained };

inline void resolution_negation(certification::Assets& assets, int options,
                                ResolutionCase mode, bool trace) {
  using namespace certification;
  const bool late_drain=mode==ResolutionCase::DrainChained;
  const bool disabled=mode==ResolutionCase::FaceUp||late_drain;
  const bool chained=mode!=ResolutionCase::FaceUp;
  const uint32_t response=late_drain?skill_drain:mode==ResolutionCase::Returned?compulsory:
                          mode==ResolutionCase::DrainRemoved?mst:book_moon;
  TestDuel d(assets,0,0,trace,options);
  d.add({dekoichi,0,0,LOCATION_MZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes,0,0,LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE});
  d.add({skill_drain,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  if(!late_drain) d.add({response,0,0,LOCATION_SZONE,1,POS_FACEDOWN_DEFENSE});

  bool setup_done=late_drain, setup_requested=false, paid=false;
  bool flip_requested=false, flipping=false, flipped=false, offered=false, chosen=false;
  bool targeted=false, turned_down=false, returned=false, removed=false, drawn=false;
  bool effect_disabled=false, finished=false;
  int announced=0, confirmed=0, resolving=0;
  std::vector<int> solved;
  auto links=[&] { return setup_done?(chained?2:1):1; };
  auto code_at=[&](int link) { return !setup_done?skill_drain:link==1?dekoichi:response; };
  auto drain_up=[&] {
    require(d.code(0,LOCATION_SZONE,0)==skill_drain&&
            (d.position(0,LOCATION_SZONE,0)&POS_FACEUP),"Skill Drain must remain active");
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {
      require(e.player==0&&!flip_requested,"Unexpected turn progression");
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0&&d.phase()==PHASE_MAIN1,"Expected P0 Main Phase");
      if(!setup_done) {
        require(!setup_requested,"Drain setup did not finish");
        int index=-1;
        for(size_t i=0;i<e.idle[5].size();++i)
          if(e.idle[5][i].code==skill_drain) index=int(i);
        require(index>=0,"Skill Drain activation missing");
        setup_requested=true; d.respond((index<<16)|5);
      } else if(!flip_requested) {
        require(late_drain||paid,"Drain setup cost missing");
        require(e.idle[2].size()==1&&e.idle[2][0].code==dekoichi&&
                d.position(0,LOCATION_MZONE,0)==POS_FACEDOWN_DEFENSE,
                "Expected actual Dekoichi Flip Summon action");
        flip_requested=true; d.respond(2);
      } else {
        require(finished&&flipped&&offered&&chosen==chained&&targeted==(chained&&!late_drain)&&
                effect_disabled==disabled&&drawn==!disabled&&paid&&
                turned_down==(mode==ResolutionCase::FaceDown)&&
                returned==(mode==ResolutionCase::Returned)&&
                removed==(mode==ResolutionCase::DrainRemoved),"Missing resolution checkpoint");
        require(d.lp(0)==7000&&d.lp(1)==8000,"Skill Drain cost must be paid exactly once");
        require(d.count(0,LOCATION_DECK)==int(disabled)&&d.count(1,LOCATION_DECK)==0,
                "Incorrect final Deck counts");
        if(disabled) require(d.code(0,LOCATION_DECK,0)==blue_eyes,"Negated Flip must not draw");
        std::vector<uint32_t> hand,want_hand,grave,want_grave;
        for(int i=0;i<d.count(0,LOCATION_HAND);++i) hand.push_back(d.code(0,LOCATION_HAND,i));
        if(!disabled) want_hand.push_back(blue_eyes);
        if(returned) want_hand.push_back(dekoichi);
        std::sort(hand.begin(),hand.end()); std::sort(want_hand.begin(),want_hand.end());
        require(hand==want_hand&&d.count(1,LOCATION_HAND)==0,"Incorrect final hand identities");
        require(d.count(0,LOCATION_MZONE)==int(!returned)&&d.count(1,LOCATION_MZONE)==0,
                "Incorrect final Monster Zones");
        if(!returned) require(d.code(0,LOCATION_MZONE,0)==dekoichi&&
                               d.position(0,LOCATION_MZONE,0)==
                                 (turned_down?POS_FACEDOWN_DEFENSE:POS_FACEUP_ATTACK),
                               "Incorrect final Dekoichi position");
        require(d.count(0,LOCATION_SZONE)==(removed?0:chained?1:2)&&
                d.count(1,LOCATION_SZONE)==0,"Incorrect final Spell/Trap Zones");
        if(!removed) drain_up();
        if(!chained) require(d.code(0,LOCATION_SZONE,1)==book_moon&&
                             d.position(0,LOCATION_SZONE,1)==POS_FACEDOWN_DEFENSE,
                             "Declined Book of Moon must remain Set");
        for(int i=0;i<d.count(0,LOCATION_GRAVE);++i) grave.push_back(d.code(0,LOCATION_GRAVE,i));
        if(chained&&!late_drain) want_grave.push_back(response);
        if(removed) want_grave.push_back(skill_drain);
        std::sort(grave.begin(),grave.end()); std::sort(want_grave.begin(),want_grave.end());
        require(grave==want_grave&&d.count(1,LOCATION_GRAVE)==0,"Incorrect final Graveyard identities");
        for(int p=0;p<2;++p) require(d.count(p,LOCATION_REMOVED)==0,"Unexpected banishment");
        std::cout<<"Observed: Dekoichi CL1 "<<(disabled?"resolved disabled without drawing":
                                                          "drew after the board changed at CL2")
                 <<"; exact chain order, Drain cost, and final zones verified.\n";
        return true;
      }
    } else if(e.kind==MSG_FLIPSUMMONING) {
      const auto& c=e.cards.at(0);
      require(flip_requested&&!flipping&&c.code==dekoichi&&c.controller==0&&
              c.location==LOCATION_MZONE&&c.sequence==0&&c.position==POS_FACEUP_ATTACK,
              "Unexpected Flip Summon announcement");
      flipping=true;
    } else if(e.kind==MSG_FLIPSUMMONED) {
      require(flipping&&!flipped,"Unexpected Flip Summon completion"); flipped=true;
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced chain selection");
      int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        require(e.player==0&&e.cards[i].controller==0&&e.cards[i].location==LOCATION_SZONE&&
                (e.cards[i].code==response||e.cards[i].code==skill_drain),
                "Unexpected response candidate");
        if(setup_done&&flipped&&confirmed==1&&!finished&&e.cards[i].code==response) {
          require(!offered,"Repeated response offer"); offered=true;
          if(chained) { chosen=true; pick=int(i); }
        }
      }
      d.respond(pick);
    } else if(e.kind==MSG_SELECT_CARD) {
      require(setup_done&&chosen&&announced==2&&confirmed==1&&!targeted&&!late_drain&&
              e.player==0&&e.minimum==1&&e.maximum==1,"Unexpected target prompt");
      const uint32_t target=mode==ResolutionCase::DrainRemoved?skill_drain:dekoichi;
      const int location=target==skill_drain?LOCATION_SZONE:LOCATION_MZONE;
      int index=-1;
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==target&&e.cards[i].controller==0&&
           e.cards[i].location==location&&e.cards[i].sequence==0) index=int(i);
      require(index>=0,"Expected CL2 target missing"); targeted=true;
      d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_CHAINING) {
      require(!finished&&!resolving&&announced==confirmed&&e.chain==announced+1&&e.chain<=links()&&
              e.cards.at(0).code==code_at(e.chain)&&e.player==0,"Unexpected chain activation");
      require(setup_done?(flipped&&(e.chain==1||chosen)):setup_requested,
              "Chain activated before fixture action");
      ++announced;
    } else if(e.kind==MSG_PAY_LPCOST) {
      require(!paid&&announced>confirmed&&code_at(announced)==skill_drain&&e.player==0&&e.mask==1000,
              "Expected one 1000 LP activation cost"); paid=true;
    } else if(e.kind==MSG_CHAINED) {
      require(e.chain==confirmed+1&&announced==e.chain,"Unexpected chain confirmation");
      if(code_at(e.chain)==skill_drain) require(paid,"Unpaid Drain cost");
      if(setup_done&&e.chain==2&&!late_drain) require(targeted,"CL2 target missing");
      ++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(!resolving&&confirmed==links()&&e.chain==links()-int(solved.size()),
              "Chain must resolve in reverse order");
      if(setup_done&&e.chain==1) {
        require(offered&&turned_down==(mode==ResolutionCase::FaceDown)&&
                returned==(mode==ResolutionCase::Returned)&&removed==(mode==ResolutionCase::DrainRemoved),
                "CL2 must change the board before Dekoichi resolves");
        if(!removed) drain_up();
        if(turned_down) require(d.position(0,LOCATION_MZONE,0)==POS_FACEDOWN_DEFENSE,
                                "Dekoichi must already be face-down at CL1 resolution");
        if(returned) require(d.count(0,LOCATION_MZONE)==0&&d.code(0,LOCATION_HAND,0)==dekoichi,
                             "Dekoichi must already be in hand at CL1 resolution");
      }
      resolving=e.chain;
    } else if(e.kind==MSG_CHAIN_DISABLED) {
      require(setup_done&&disabled&&resolving==1&&e.chain==1&&!effect_disabled,
              "Only face-up Dekoichi should resolve disabled"); effect_disabled=true;
    } else if(e.kind==MSG_CHAIN_NEGATED) {
      throw std::runtime_error("Continuous negation must not negate activation");
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving==e.chain,"Solved link mismatch");
      if(setup_done&&e.chain==1)
        require(effect_disabled==disabled&&drawn==!disabled,"Incorrect Dekoichi resolution");
      solved.push_back(e.chain); resolving=0;
    } else if(e.kind==MSG_CHAIN_END) {
      require(!resolving&&int(solved.size())==links()&&!finished,"Incomplete chain");
      if(!setup_done) {
        drain_up(); require(paid&&!flipped,"Setup must resolve before Flip Summon");
        setup_done=true; announced=confirmed=0; solved.clear();
      } else finished=true;
    } else if(e.kind==MSG_POS_CHANGE&&e.from.code==dekoichi) {
      require(mode==ResolutionCase::FaceDown&&resolving==2&&!turned_down&&
              e.from.controller==0&&e.from.location==LOCATION_MZONE&&
              e.from.position==POS_FACEUP_ATTACK&&e.to.position==POS_FACEDOWN_DEFENSE,
              "Book of Moon must turn Dekoichi face-down at CL2"); turned_down=true;
    } else if(e.kind==MSG_MOVE&&e.from.code==dekoichi) {
      require(mode==ResolutionCase::Returned&&resolving==2&&!returned&&
              e.from.controller==0&&e.from.location==LOCATION_MZONE&&e.to.controller==0&&
              e.to.location==LOCATION_HAND&&(e.reason&REASON_EFFECT)&&
              !(e.reason&(REASON_COST|REASON_DESTROY|REASON_BATTLE)),
              "Compulsory must return Dekoichi as an effect at CL2"); returned=true;
    } else if(e.kind==MSG_MOVE&&e.from.code==skill_drain) {
      require(mode==ResolutionCase::DrainRemoved&&resolving==2&&!removed&&
              e.from.controller==0&&e.from.location==LOCATION_SZONE&&e.to.controller==0&&
              e.to.location==LOCATION_GRAVE&&
              (e.reason&(REASON_EFFECT|REASON_DESTROY))==(REASON_EFFECT|REASON_DESTROY)&&
              !(e.reason&(REASON_COST|REASON_BATTLE)),"MST must destroy Drain at CL2"); removed=true;
    } else if(e.kind==MSG_DRAW) {
      require(setup_done&&!disabled&&resolving==1&&!drawn&&e.player==0&&e.count==1&&
              (e.cards.at(0).code&0x7fffffff)==blue_eyes,"Expected exactly Dekoichi's CL1 draw"); drawn=true;
    } else if(e.kind==MSG_SELECT_EFFECTYN||e.kind==MSG_SELECT_YESNO||e.kind==MSG_SELECT_PLACE||
              e.kind==MSG_SELECT_POSITION||e.kind==MSG_SELECT_TRIBUTE||e.kind==MSG_SELECT_BATTLECMD||
              e.kind==MSG_DAMAGE||e.kind==MSG_DAMAGE_STEP_START||e.kind==MSG_SUMMONING||
              e.kind==MSG_SPSUMMONING) {
      throw std::runtime_error("Unexpected action in resolution-negation fixture");
    }
    return false;
  });
}
