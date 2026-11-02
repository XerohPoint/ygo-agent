#pragma once
#include "harness.h"

enum class PersistentNegation { Chalice, SkillDrain, ChaliceUnused };

inline void persistent_negation(certification::Assets& assets, int options,
                                PersistentNegation mode, bool exiled, bool trace) {
  using namespace certification;
  const bool applied=mode!=PersistentNegation::ChaliceUnused;
  const bool drain=mode==PersistentNegation::SkillDrain;
  const bool disabled=mode==PersistentNegation::Chalice;
  const bool recruit=!exiled&&!disabled;
  const bool destroy=exiled&&!disabled;
  const uint32_t actor=exiled?exiled_force:lonefire;
  const uint32_t spell=drain?skill_drain:chalice;
  TestDuel d(assets,0,0,trace,options);
  d.add({actor,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({spell,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  if(exiled) d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  else d.add({jerry_beans,0,0,LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE});

  bool setup_done=!applied, setup_requested=false, spell_flipped=false, setup_target=false;
  bool drain_paid=false, spell_sent=false, requested=false, cost_selected=false, cost_paid=false;
  bool target_selected=false, recruit_selected=false, placed=false, positioned=false;
  bool recruit_moved=false, summoning=false, summoned=false, destroyed=false, effect_disabled=false;
  bool announced=false, confirmed=false, resolving=false, solved=false, finished=false;
  bool response_window=false, unused_offer=false;
  auto check_spell=[&] {
    require(d.count(0,LOCATION_SZONE)==int(!disabled),"Wrong negator zone count");
    if(!disabled) require(d.code(0,LOCATION_SZONE,0)==spell&&
                          (drain?bool(d.position(0,LOCATION_SZONE,0)&POS_FACEUP):
                                 d.position(0,LOCATION_SZONE,0)==POS_FACEDOWN_DEFENSE),
                          "Negator must retain expected active/Set state");
    require(spell_sent==disabled,"Resolved Chalice must already be in the Graveyard");
  };
  auto check_cost=[&] {
    require(cost_paid&&d.count(0,LOCATION_MZONE)==0&&
            d.count(0,LOCATION_GRAVE)==1+int(disabled)&&
            d.code(0,LOCATION_GRAVE,disabled?1:0)==actor,
            "Activating monster must already be tributed before responses/resolution");
    if(disabled) require(d.code(0,LOCATION_GRAVE,0)==chalice,"Applied Chalice missing from Graveyard");
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {
      require(e.player==0&&!requested,"Unexpected turn progression");
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0&&d.phase()==PHASE_MAIN1,"Expected P0 Main Phase");
      if(!setup_done||!requested) {
        require(!setup_done?!setup_requested:!finished,"Setup/ignition returned to idle early");
        int index=-1;
        for(size_t i=0;i<e.idle[5].size();++i) {
          const auto& c=e.idle[5][i];
          if(c.code==(setup_done?actor:spell)&&c.controller==0&&c.sequence==0&&
             c.location==(setup_done?LOCATION_MZONE:LOCATION_SZONE)) index=int(i);
          if(!applied&&c.code==chalice) unused_offer=true;
        }
        require(index>=0,"Expected activation missing despite negation");
        if(setup_done) {
          check_spell();
          require(d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==
                  (exiled?1000u:500u)+(disabled?400u:0u),
                  "Chalice must have resolved on the face-up actor before its ignition");
          requested=true;
        } else setup_requested=true;
        d.respond((index<<16)|5);
      } else {
        require(finished&&cost_paid&&cost_selected==!exiled&&target_selected==exiled&&
                effect_disabled==disabled&&recruit_selected==recruit&&placed==recruit&&
                positioned==recruit&&recruit_moved==recruit&&summoning==recruit&&summoned==recruit&&
                destroyed==destroy&&spell_flipped==applied&&setup_target==disabled&&drain_paid==drain&&
                response_window&&(applied||unused_offer),"Missing persistent-negation checkpoint");
        check_spell();
        require(d.lp(0)==(drain?7000:8000)&&d.lp(1)==8000,"Wrong cost/LP outcome");
        require(d.count(0,LOCATION_GRAVE)==1+int(disabled)&&
                d.code(0,LOCATION_GRAVE,disabled?1:0)==actor,"Tribute cost must remain paid");
        if(disabled) require(d.code(0,LOCATION_GRAVE,0)==chalice,"Missing resolved Chalice");
        require(d.count(1,LOCATION_GRAVE)==int(destroy)&&d.count(1,LOCATION_MZONE)==int(exiled&&!destroy),
                "Wrong target destruction outcome");
        if(exiled) require(d.code(1,destroy?LOCATION_GRAVE:LOCATION_MZONE,0)==blue_eyes,
                           "Wrong surviving/destroyed target identity");
        require(d.count(0,LOCATION_MZONE)==int(recruit)&&d.count(0,LOCATION_DECK)==int(!exiled&&disabled)&&
                d.count(1,LOCATION_DECK)==0,"Wrong recruitment/Deck outcome");
        if(recruit) require(d.code(0,LOCATION_MZONE,0)==jerry_beans&&
                            d.position(0,LOCATION_MZONE,0)==POS_FACEUP_ATTACK,"Wrong recruited Plant");
        if(!exiled&&disabled) require(d.code(0,LOCATION_DECK,0)==jerry_beans,"Negated effect must not recruit");
        require(d.count(1,LOCATION_SZONE)==0,"Unexpected opposing Spell/Trap");
        for(int p=0;p<2;++p)
          require(d.count(p,LOCATION_HAND)==0&&d.count(p,LOCATION_REMOVED)==0&&
                  d.count(p,LOCATION_EXTRA)==0,"Unexpected hand, banished, or Extra Deck cards");
        std::cout<<"Observed: field ignition activated, self-tribute paid before responses; "
                 <<(disabled?"previously applied Chalice still disabled the effect":
                             drain?"Skill Drain did not disable the departed monster's effect":
                                   "unused Chalice left the effect unnegated")
                 <<". Resolution, retained cost, and exact final zones verified.\n";
        return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");
      for(const auto& c:e.cards)
        require((!setup_done||!applied)&&e.player==0&&c.code==spell&&c.controller==0&&
                c.location==LOCATION_SZONE,"Unexpected response candidate");
      if(requested&&confirmed&&!solved) { check_cost(); response_window=true; }
      d.respond(-1);
    } else if(e.kind==MSG_POS_CHANGE) {
      require(!setup_done&&setup_requested&&!announced&&!spell_flipped&&e.from.code==spell&&
              e.from.controller==0&&e.from.location==LOCATION_SZONE&&e.from.sequence==0&&
              e.from.position==POS_FACEDOWN_DEFENSE&&(e.to.position&POS_FACEUP),
              "Expected negator to turn face-up before setup activation"); spell_flipped=true;
    } else if(e.kind==MSG_CHAINING) {
      const auto& c=e.cards.at(0);
      require(!announced&&!finished&&e.chain==1&&e.player==0&&c.controller==0&&c.sequence==0&&
              c.code==(setup_done?actor:spell)&&c.location==(setup_done?LOCATION_MZONE:LOCATION_SZONE)&&
              (setup_done?requested:setup_requested),"Expected field activation as CL1"); announced=true;
    } else if(e.kind==MSG_PAY_LPCOST) {
      require(drain&&!setup_done&&announced&&!confirmed&&!drain_paid&&e.player==0&&e.mask==1000,
              "Expected Skill Drain's 1000 LP cost"); drain_paid=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced&&e.player==0&&e.minimum==1&&e.maximum==1,"Unexpected card selection");
      uint32_t want; int controller=0,location;
      if(!setup_done) {
        require(disabled&&!confirmed&&!setup_target,"Expected Chalice setup target");
        setup_target=true; want=actor; location=LOCATION_MZONE;
      } else if(resolving) {
        require(recruit&&!recruit_selected&&cost_paid,"Negated effect must not select a recruit");
        recruit_selected=true; want=jerry_beans; location=LOCATION_DECK;
      } else if(exiled) {
        require(!confirmed&&cost_paid&&!target_selected,"Exiled target must follow self-tribute");
        target_selected=true; want=blue_eyes; controller=1; location=LOCATION_MZONE;
      } else {
        require(!confirmed&&!cost_paid&&!cost_selected,"Expected Lonefire self-tribute choice");
        cost_selected=true; want=lonefire; location=LOCATION_MZONE;
      }
      int index=-1;
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want&&e.cards[i].controller==controller&&
           e.cards[i].location==location&&e.cards[i].sequence==0) index=int(i);
      require(index>=0,"Required setup target/cost/effect choice missing");
      d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==chalice) {
        require(disabled&&!setup_done&&solved&&!spell_sent&&e.from.controller==0&&
                e.from.location==LOCATION_SZONE&&e.to.controller==0&&e.to.location==LOCATION_GRAVE&&
                !(e.reason&(REASON_DESTROY|REASON_COST|REASON_BATTLE)),
                "Chalice must go to Graveyard after resolving its setup effect"); spell_sent=true;
      } else if(e.from.code==actor) {
        require(setup_done&&announced&&!confirmed&&!resolving&&!cost_paid&&(exiled||cost_selected)&&
                e.from.controller==0&&e.from.location==LOCATION_MZONE&&e.from.sequence==0&&
                e.to.controller==0&&e.to.location==LOCATION_GRAVE&&
                (e.reason&(REASON_COST|REASON_RELEASE))==(REASON_COST|REASON_RELEASE)&&
                !(e.reason&(REASON_EFFECT|REASON_DESTROY|REASON_BATTLE)),
                "Self-tribute must be paid before confirmation and responses"); cost_paid=true;
      } else if(e.from.code==blue_eyes) {
        require(destroy&&resolving&&target_selected&&!destroyed&&e.from.controller==1&&
                e.from.location==LOCATION_MZONE&&e.to.controller==1&&e.to.location==LOCATION_GRAVE&&
                (e.reason&(REASON_EFFECT|REASON_DESTROY))==(REASON_EFFECT|REASON_DESTROY)&&
                !(e.reason&(REASON_COST|REASON_RELEASE|REASON_BATTLE)),
                "Only unnegated Exiled Force should destroy its target"); destroyed=true;
      } else {
        require(recruit&&resolving&&recruit_selected&&!recruit_moved&&e.from.code==jerry_beans&&
                e.from.controller==0&&e.from.location==LOCATION_DECK&&e.to.controller==0&&
                e.to.location==LOCATION_MZONE&&e.to.sequence==0&&e.reason==REASON_SPSUMMON,
                "Only unnegated Lonefire should move its recruit from Deck"); recruit_moved=true;
      }
    } else if(e.kind==MSG_CHAINED) {
      require(announced&&!confirmed&&e.chain==1,"Unexpected confirmation");
      if(setup_done) { check_cost(); require(target_selected==exiled,"Missing target"); }
      else require(drain?drain_paid:setup_target,"Missing setup cost/target");
      confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed&&!resolving&&!solved&&e.chain==1,"Unexpected resolution");
      if(setup_done) { check_cost(); check_spell(); }
      resolving=true;
    } else if(e.kind==MSG_CHAIN_DISABLED) {
      require(setup_done&&disabled&&resolving&&e.chain==1&&!effect_disabled&&cost_paid,
              "Applied Chalice must disable the field effect after its actor leaves"); effect_disabled=true;
    } else if(e.kind==MSG_CHAIN_NEGATED) {
      throw std::runtime_error("Effect negation must not become activation negation");
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(recruit&&resolving&&recruit_selected&&!placed&&e.player==0&&e.count==1&&!(e.mask&1u),
              "Unexpected recruitment placement"); placed=true; d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(recruit&&resolving&&recruit_selected&&!positioned&&e.player==0&&
              e.cards.at(0).code==jerry_beans&&(e.mask&POS_FACEUP_ATTACK),"Unexpected summon position");
      positioned=true; d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONING) {
      const auto& c=e.cards.at(0);
      require(recruit&&resolving&&placed&&positioned&&recruit_moved&&!summoning&&c.code==jerry_beans&&
              c.controller==0&&c.location==LOCATION_MZONE&&c.sequence==0&&c.position==POS_FACEUP_ATTACK,
              "Unexpected Special Summon"); summoning=true;
    } else if(e.kind==MSG_SPSUMMONED) {
      require(recruit&&resolving&&summoning&&!summoned,"Unexpected summon completion"); summoned=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving&&!solved&&e.chain==1,"Unexpected solved link");
      if(setup_done) require(effect_disabled==disabled&&summoned==recruit&&destroyed==destroy,
                             "Wrong monster resolution");
      resolving=false; solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved&&!resolving&&!finished,"Incomplete chain");
      if(!setup_done) {
        require(!requested&&spell_flipped&&setup_target==disabled&&drain_paid==drain,"Incomplete setup");
        check_spell(); setup_done=true; announced=confirmed=solved=false;
      } else finished=true;
    } else if(e.kind==MSG_SELECT_EFFECTYN||e.kind==MSG_SELECT_YESNO||e.kind==MSG_SELECT_TRIBUTE||
              e.kind==MSG_SELECT_BATTLECMD||e.kind==MSG_DRAW||e.kind==MSG_DAMAGE||
              e.kind==MSG_DAMAGE_STEP_START||e.kind==MSG_SUMMONING||e.kind==MSG_FLIPSUMMONING||
              e.kind==MSG_SET) {
      throw std::runtime_error("Unexpected action in persistent-negation fixture");
    }
    return false;
  });
}
