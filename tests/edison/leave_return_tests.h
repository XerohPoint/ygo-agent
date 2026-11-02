#pragma once
#include "harness.h"

enum class ReturnNegation { Chalice, SkillDrain, UnusedChalice };

inline void leave_and_return(certification::Assets& assets, int options,
                             ReturnNegation mode, bool exiled, bool trace) {
  using namespace certification;
  const bool applied=mode!=ReturnNegation::UnusedChalice;
  const bool drain=mode==ReturnNegation::SkillDrain;
  const bool disabled=mode==ReturnNegation::Chalice;
  const bool recruit=!exiled&&!disabled;
  const bool destroy=exiled&&!disabled;
  const uint32_t actor=exiled?exiled_force:lonefire;
  const uint32_t spell=drain?skill_drain:chalice;
  TestDuel d(assets,0,0,trace,options);
  d.add({actor,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({spell,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({call_haunted,0,0,LOCATION_SZONE,1,POS_FACEDOWN_DEFENSE});
  if(exiled) d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  else d.add({jerry_beans,0,0,LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE});

  bool setup_done=!applied,setup_requested=false,spell_flipped=false,setup_target=false,spell_sent=false;
  bool paid=false,requested=false,cost_selected=false,cost_paid=false,target_selected=false;
  bool call_chosen=false,call_flipped=false,revival_target=false,revival_placed=false;
  bool returned=false,revival_announced=false,revived=false,linked=false;
  bool recruit_selected=false,recruit_placed=false,positioned=false,recruit_moved=false;
  bool recruit_announced=false,recruited=false,destroyed=false,effect_disabled=false;
  bool finished=false,unused_offer=false;
  int announced=0,confirmed=0,resolving=0;
  std::vector<int> solved;
  auto links=[&] { return setup_done?2:1; };
  auto code_at=[&](int link) { return !setup_done?spell:link==1?actor:call_haunted; };
  auto check_negator=[&] {
    require(spell_sent==disabled,"Applied Chalice must have left the field");
    if(!disabled) require(d.code(0,LOCATION_SZONE,0)==spell&&
                         (drain?bool(d.position(0,LOCATION_SZONE,0)&POS_FACEUP):
                                d.position(0,LOCATION_SZONE,0)==POS_FACEDOWN_DEFENSE),
                         "Negator must retain its expected state");
  };
  auto check_cost=[&] {
    require(cost_paid&&!returned&&d.count(0,LOCATION_MZONE)==0&&
            d.count(0,LOCATION_GRAVE)==1+int(disabled)&&
            d.code(0,LOCATION_GRAVE,disabled?1:0)==actor,
            "The sole actor must be in the Graveyard before revival");
  };
  auto check_return=[&] {
    require(returned&&revived&&linked&&d.code(0,LOCATION_MZONE,0)==actor&&
            d.position(0,LOCATION_MZONE,0)==POS_FACEUP_ATTACK&&
            d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==(exiled?1000u:500u)&&
            d.count(0,LOCATION_GRAVE)==int(disabled),
            "The same physical monster must return at base ATK before original CL1 resolves");
    require(d.code(0,LOCATION_SZONE,1)==call_haunted&&
            (d.position(0,LOCATION_SZONE,1)&POS_FACEUP),"Call must remain active");
    check_negator();
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {
      require(e.player==0&&!requested,"Unexpected turn progression");
    } else if(e.kind==MSG_NEW_PHASE) {
      require(e.phase==PHASE_DRAW||e.phase==PHASE_STANDBY||e.phase==PHASE_MAIN1,
              "Fixture must finish in the same Main Phase");
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0&&d.phase()==PHASE_MAIN1,"Expected P0 Main Phase");
      if(!setup_done||!requested) {
        require(!announced&&(!setup_done?!setup_requested:!finished),"Chain returned to idle early");
        int index=-1;
        for(size_t i=0;i<e.idle[5].size();++i) {
          const auto& c=e.idle[5][i];
          if(c.code==(setup_done?actor:spell)&&c.controller==0&&c.sequence==0&&
             c.location==(setup_done?LOCATION_MZONE:LOCATION_SZONE)) index=int(i);
          if(!applied&&c.code==chalice) unused_offer=true;
          require(c.code!=call_haunted,"Call cannot revive the actor before it leaves");
        }
        require(index>=0,"Required setup/ignition action missing");
        if(setup_done) {
          check_negator();
          require(d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==
                  (exiled?1000u:500u)+(disabled?400u:0u),"Negator setup must precede ignition");
          requested=true;
        } else setup_requested=true;
        d.respond((index<<16)|5);
      } else {
        require(finished&&cost_paid&&cost_selected==!exiled&&target_selected==exiled&&
                call_chosen&&call_flipped&&revival_target&&revival_placed&&returned&&
                revival_announced&&revived&&linked&&effect_disabled==disabled&&
                recruit_selected==recruit&&recruit_placed==recruit&&positioned==recruit&&
                recruit_moved==recruit&&recruit_announced==recruit&&recruited==recruit&&
                destroyed==destroy&&spell_flipped==applied&&setup_target==disabled&&paid==drain&&
                (applied||unused_offer),"Missing leave-and-return checkpoint");
        check_return();
        require(d.lp(0)==(drain?7000:8000)&&d.lp(1)==8000,"Wrong LP/cost outcome");
        require(d.count(0,LOCATION_MZONE)==1+int(recruit)&&d.count(0,LOCATION_SZONE)==1+int(!disabled),
                "Wrong final field counts");
        if(recruit) require(d.code(0,LOCATION_MZONE,2)==jerry_beans&&
                            d.position(0,LOCATION_MZONE,2)==POS_FACEUP_ATTACK,"Wrong recruited Plant");
        if(disabled) require(d.code(0,LOCATION_GRAVE,0)==chalice,"Wrong remaining Graveyard card");
        require(d.count(0,LOCATION_DECK)==int(!exiled&&disabled)&&d.count(1,LOCATION_DECK)==0,
                "Wrong Deck outcome");
        if(!exiled&&disabled) require(d.code(0,LOCATION_DECK,0)==jerry_beans,"Negated Lonefire must not recruit");
        require(d.count(1,LOCATION_MZONE)==int(exiled&&!destroy)&&d.count(1,LOCATION_GRAVE)==int(destroy),
                "Wrong Exiled Force outcome");
        if(exiled) require(d.code(1,destroy?LOCATION_GRAVE:LOCATION_MZONE,0)==blue_eyes,"Wrong target identity");
        require(d.count(1,LOCATION_SZONE)==0,"Unexpected opposing Spell/Trap");
        for(int p=0;p<2;++p)
          require(d.count(p,LOCATION_HAND)==0&&d.count(p,LOCATION_REMOVED)==0&&d.count(p,LOCATION_EXTRA)==0,
                  "Unexpected hand, banished, or Extra Deck cards");
        std::cout<<"Observed: self-tribute paid, Call CL2 revived the same monster before original CL1; "
                 <<(disabled?"applied Chalice still negated that original effect":
                             drain?"Skill Drain did not retroactively negate the original effect":
                                   "the original effect resolved normally")
                 <<". Base ATK, revival link, resolution order, and final zones verified.\n";
        return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced selection");
      int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        const auto& c=e.cards[i];
        require(e.player==0&&c.controller==0&&c.location==LOCATION_SZONE,"Unexpected candidate origin");
        if(c.code==call_haunted) {
          require(setup_done&&requested&&confirmed==1&&!call_chosen&&cost_paid&&!resolving,
                  "Call must only be offered after the actor's tribute cost");
          check_cost(); call_chosen=true; pick=int(i);
        } else require(c.code==spell&&(!setup_done||!applied),"Unexpected response candidate");
      }
      d.respond(pick);
    } else if(e.kind==MSG_POS_CHANGE) {
      require(e.from.controller==0&&e.from.location==LOCATION_SZONE&&
              e.from.position==POS_FACEDOWN_DEFENSE&&(e.to.position&POS_FACEUP),"Unexpected position change");
      if(e.from.code==call_haunted) {
        require(call_chosen&&announced==1&&confirmed==1&&!call_flipped&&e.from.sequence==1,
                "Unexpected Call activation"); call_flipped=true;
      } else {
        require(!setup_done&&setup_requested&&!announced&&!spell_flipped&&
                e.from.code==spell&&e.from.sequence==0,"Unexpected negator activation"); spell_flipped=true;
      }
    } else if(e.kind==MSG_CHAINING) {
      const auto& c=e.cards.at(0);
      require(!finished&&!resolving&&announced==confirmed&&e.chain==announced+1&&e.chain<=links()&&
              c.code==code_at(e.chain)&&e.player==0&&c.controller==0&&
              c.location==(setup_done&&e.chain==1?LOCATION_MZONE:LOCATION_SZONE)&&
              c.sequence==(setup_done&&e.chain==2?1:0),"Unexpected activation order/location");
      require(!setup_done?setup_requested:e.chain==1?requested:call_chosen,"Unrequested activation");
      ++announced;
    } else if(e.kind==MSG_PAY_LPCOST) {
      require(drain&&!setup_done&&announced==1&&!confirmed&&!paid&&e.player==0&&e.mask==1000,
              "Expected Skill Drain's setup cost"); paid=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced&&e.player==0&&e.minimum==1&&e.maximum==1,"Unexpected card selection");
      uint32_t want; int controller=0,location;
      if(!setup_done) {
        require(disabled&&!confirmed&&!setup_target,"Unexpected negator target");
        setup_target=true; want=actor; location=LOCATION_MZONE;
      } else if(resolving) {
        require(resolving==1&&recruit&&!recruit_selected&&revived,"Unexpected recruitment");
        recruit_selected=true; want=jerry_beans; location=LOCATION_DECK;
      } else if(announced==2) {
        require(confirmed==1&&call_chosen&&!revival_target,"Unexpected revival target");
        check_cost(); revival_target=true; want=actor; location=LOCATION_GRAVE;
        require(e.cards.size()==1,"Only the original actor should be revivable");
      } else if(exiled) {
        require(!confirmed&&cost_paid&&!target_selected,"Exiled target must follow cost");
        target_selected=true; want=blue_eyes; controller=1; location=LOCATION_MZONE;
      } else {
        require(!confirmed&&!cost_paid&&!cost_selected,"Unexpected tribute choice");
        cost_selected=true; want=actor; location=LOCATION_MZONE;
      }
      int index=-1;
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want&&e.cards[i].controller==controller&&e.cards[i].location==location) index=int(i);
      require(index>=0,"Required selection missing"); d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==chalice) {
        require(disabled&&!setup_done&&solved==std::vector<int>{1}&&!spell_sent&&
                e.from.controller==0&&e.from.location==LOCATION_SZONE&&e.to.controller==0&&
                e.to.location==LOCATION_GRAVE&&!(e.reason&(REASON_COST|REASON_DESTROY|REASON_BATTLE)),
                "Expected resolved Chalice cleanup"); spell_sent=true;
      } else if(e.from.code==actor&&e.to.location==LOCATION_GRAVE) {
        require(setup_done&&announced==1&&!confirmed&&!resolving&&!cost_paid&&(exiled||cost_selected)&&
                e.from.controller==0&&e.from.location==LOCATION_MZONE&&e.from.sequence==0&&e.to.controller==0&&
                (e.reason&(REASON_COST|REASON_RELEASE))==(REASON_COST|REASON_RELEASE)&&
                !(e.reason&(REASON_EFFECT|REASON_DESTROY|REASON_BATTLE)),
                "Actor must leave as cost before confirmation/responses"); cost_paid=true;
      } else if(e.from.code==actor) {
        require(resolving==2&&revival_target&&revival_placed&&!returned&&cost_paid&&
                e.from.controller==0&&e.from.location==LOCATION_GRAVE&&e.to.controller==0&&
                e.to.location==LOCATION_MZONE&&e.to.sequence==0&&e.to.position==POS_FACEUP_ATTACK&&
                e.reason==REASON_SPSUMMON,"Call must revive the exact tributed actor at CL2"); returned=true;
      } else if(e.from.code==blue_eyes) {
        require(destroy&&resolving==1&&revived&&target_selected&&!destroyed&&e.from.controller==1&&
                e.from.location==LOCATION_MZONE&&e.to.controller==1&&e.to.location==LOCATION_GRAVE&&
                (e.reason&(REASON_EFFECT|REASON_DESTROY))==(REASON_EFFECT|REASON_DESTROY)&&
                !(e.reason&(REASON_COST|REASON_RELEASE|REASON_BATTLE)),"Wrong target destruction"); destroyed=true;
      } else {
        require(recruit&&resolving==1&&revived&&recruit_selected&&!recruit_moved&&e.from.code==jerry_beans&&
                e.from.controller==0&&e.from.location==LOCATION_DECK&&e.to.controller==0&&
                e.to.location==LOCATION_MZONE&&e.to.sequence==2&&e.reason==REASON_SPSUMMON,
                "Wrong recruitment movement"); recruit_moved=true;
      }
    } else if(e.kind==MSG_CHAINED) {
      require(e.chain==confirmed+1&&announced==e.chain,"Unexpected confirmation");
      if(!setup_done) require(drain?paid:setup_target,"Missing setup cost/target");
      else {
        check_cost();
        require(e.chain==1?target_selected==exiled:revival_target,"Missing monster/revival target");
      }
      ++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(!resolving&&confirmed==links()&&e.chain==links()-int(solved.size()),"Wrong resolution order");
      if(setup_done) {
        if(e.chain==2) check_cost();
        else { require(solved==std::vector<int>{2},"CL2 must fully resolve before original effect"); check_return(); }
      }
      resolving=e.chain;
    } else if(e.kind==MSG_CHAIN_DISABLED) {
      require(setup_done&&disabled&&resolving==1&&e.chain==1&&!effect_disabled&&revived,
              "Only previously applied Chalice should negate original CL1"); effect_disabled=true;
    } else if(e.kind==MSG_CHAIN_NEGATED) {
      throw std::runtime_error("Unexpected activation negation");
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(setup_done&&e.player==0&&e.count==1,"Unexpected placement");
      if(resolving==2) {
        require(revival_target&&!revival_placed&&!(e.mask&1u),"Expected revival in original zone");
        revival_placed=true; d.respond({0,LOCATION_MZONE,0});
      } else {
        require(resolving==1&&recruit&&recruit_selected&&!recruit_placed&&!(e.mask&(1u<<2)),
                "Unexpected recruit placement"); recruit_placed=true; d.respond({0,LOCATION_MZONE,2});
      }
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(recruit&&resolving==1&&recruit_selected&&!positioned&&e.player==0&&
              e.cards.at(0).code==jerry_beans&&(e.mask&POS_FACEUP_ATTACK),"Unexpected summon position");
      positioned=true; d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONING) {
      const auto& c=e.cards.at(0);
      require(c.controller==0&&c.location==LOCATION_MZONE&&c.position==POS_FACEUP_ATTACK,
              "Wrong Special Summon origin/position");
      if(resolving==2) {
        require(returned&&revival_placed&&!revival_announced&&c.code==actor&&c.sequence==0,
                "Expected revival announcement"); revival_announced=true;
      } else {
        require(recruit&&resolving==1&&recruit_moved&&recruit_placed&&positioned&&!recruit_announced&&
                c.code==jerry_beans&&c.sequence==2,"Expected recruitment announcement"); recruit_announced=true;
      }
    } else if(e.kind==MSG_CARD_TARGET) {
      require(resolving==2&&revival_announced&&!linked&&e.from.controller==0&&
              e.from.location==LOCATION_SZONE&&e.from.sequence==1&&e.to.controller==0&&
              e.to.location==LOCATION_MZONE&&e.to.sequence==0,
              "Call must establish its ongoing link to the revived actor"); linked=true;
    } else if(e.kind==MSG_SPSUMMONED) {
      if(resolving==2) {
        require(revival_announced&&linked&&!revived,"Unexpected revival completion"); revived=true;
      } else {
        require(resolving==1&&recruit&&recruit_announced&&!recruited,"Unexpected recruitment completion"); recruited=true;
      }
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving==e.chain,"Unexpected solved link");
      if(setup_done) {
        if(e.chain==2) check_return();
        else require(effect_disabled==disabled&&recruited==recruit&&destroyed==destroy,"Wrong original effect outcome");
      }
      solved.push_back(e.chain); resolving=0;
    } else if(e.kind==MSG_CHAIN_END) {
      require(!resolving&&int(solved.size())==links()&&!finished,"Incomplete chain");
      if(!setup_done) {
        require(spell_flipped&&setup_target==disabled&&paid==drain&&!requested,"Incomplete negator setup");
        check_negator(); setup_done=true; announced=confirmed=0; solved.clear();
      } else finished=true;
    } else if(e.kind==MSG_SELECT_EFFECTYN||e.kind==MSG_SELECT_YESNO||e.kind==MSG_SELECT_TRIBUTE||
              e.kind==MSG_SELECT_BATTLECMD||e.kind==MSG_DRAW||e.kind==MSG_DAMAGE||
              e.kind==MSG_DAMAGE_STEP_START||e.kind==MSG_SUMMONING||e.kind==MSG_FLIPSUMMONING||
              e.kind==MSG_SET) {
      throw std::runtime_error("Unexpected action in leave-and-return fixture");
    }
    return false;
  });
}
