#pragma once
#include "harness.h"

enum class CostDeparture { LonefireSelf, LonefireOther, ExiledSelf };

inline void cost_departure(certification::Assets& assets, int options,
                           CostDeparture mode, bool active_drain, bool trace) {
  using namespace certification;
  const bool exiled=mode==CostDeparture::ExiledSelf;
  const bool self=mode!=CostDeparture::LonefireOther;
  const bool disabled=active_drain&&!self;
  const bool recruit=!exiled&&!disabled;
  const uint32_t actor=exiled?exiled_force:lonefire;
  const uint32_t tribute=self?actor:jerry_beans;
  const uint8_t tribute_zone=self?0:1;
  TestDuel d(assets,0,0,trace,options);
  d.add({actor,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({skill_drain,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  if(exiled) d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  else {
    d.add({jerry_beans,0,0,LOCATION_MZONE,1,POS_FACEUP_ATTACK});
    d.add({jerry_beans,0,0,LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE});
  }
  bool setup_done=!active_drain, setup_requested=false, drain_paid=false, drain_flipped=false;
  bool requested=false, cost_selected=false, cost_paid=false, target_selected=false;
  bool recruit_selected=false, place_selected=false, position_selected=false;
  bool summoning=false, summoned=false, destroyed=false, effect_disabled=false;
  bool announced=false, confirmed=false, resolving=false, solved=false, finished=false;
  bool set_drain_offered=false;
  auto check_drain=[&] {
    require(d.count(0,LOCATION_SZONE)==1&&d.code(0,LOCATION_SZONE,0)==skill_drain&&
            (active_drain?bool(d.position(0,LOCATION_SZONE,0)&POS_FACEUP):
                          d.position(0,LOCATION_SZONE,0)==POS_FACEDOWN_DEFENSE),
            "Skill Drain must retain its active/Set state");
  };
  auto check_cost=[&] {
    require(cost_paid&&d.count(0,LOCATION_GRAVE)==1&&d.code(0,LOCATION_GRAVE,0)==tribute,
            "Tribute cost must already be in the Graveyard");
    require(d.count(0,LOCATION_MZONE)==int(!exiled),"Wrong field count after tribute cost");
    if(!exiled) require(d.code(0,LOCATION_MZONE,self?1:0)==(self?jerry_beans:lonefire)&&
                         d.position(0,LOCATION_MZONE,self?1:0)==POS_FACEUP_ATTACK,
                         "Wrong monster retained after tribute cost");
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {
      require(e.player==0&&!requested,"Unexpected turn progression");
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0&&d.phase()==PHASE_MAIN1,"Expected P0 Main Phase");
      if(!setup_done||!requested) {
        require(!setup_done?!setup_requested:!finished,"Unexpected idle before chain completion");
        const uint32_t want=setup_done?actor:skill_drain;
        int index=-1;
        for(size_t i=0;i<e.idle[5].size();++i)
          if(e.idle[5][i].code==want&&e.idle[5][i].controller==0&&
             e.idle[5][i].location==(setup_done?LOCATION_MZONE:LOCATION_SZONE)&&
             e.idle[5][i].sequence==0) index=int(i);
        require(index>=0,"Required activation missing under Skill Drain");
        if(setup_done) { check_drain(); requested=true; }
        else setup_requested=true;
        d.respond((index<<16)|5);
      } else {
        require(finished&&cost_paid&&cost_selected==!exiled&&target_selected==exiled&&
                effect_disabled==disabled&&summoned==recruit&&summoning==recruit&&
                recruit_selected==recruit&&place_selected==recruit&&position_selected==recruit&&
                destroyed==exiled&&drain_paid==active_drain&&drain_flipped==active_drain&&
                (active_drain||set_drain_offered),
                "Missing cost-departure checkpoint");
        check_drain();
        require(d.lp(0)==(active_drain?7000:8000)&&d.lp(1)==8000,"Unexpected LP or refunded Drain cost");
        require(d.count(0,LOCATION_GRAVE)==1&&d.code(0,LOCATION_GRAVE,0)==tribute&&
                d.count(1,LOCATION_GRAVE)==int(exiled),"Tribute cost must remain paid");
        if(exiled) require(d.code(1,LOCATION_GRAVE,0)==blue_eyes,"Missing destroyed target");
        require(d.count(0,LOCATION_MZONE)==(exiled?0:1+int(recruit))&&
                d.count(1,LOCATION_MZONE)==0,"Wrong final Monster Zones");
        if(!exiled) {
          require(d.code(0,LOCATION_MZONE,self?1:0)==(self?jerry_beans:lonefire)&&
                  d.position(0,LOCATION_MZONE,self?1:0)==POS_FACEUP_ATTACK,
                  "Wrong retained monster at final checkpoint");
          if(recruit) require(d.code(0,LOCATION_MZONE,2)==jerry_beans&&
                              d.position(0,LOCATION_MZONE,2)==POS_FACEUP_ATTACK,"Wrong recruited Plant");
        }
        require(d.count(0,LOCATION_DECK)==int(disabled)&&d.count(1,LOCATION_DECK)==0,
                "Negated recruitment must leave the Deck intact");
        if(disabled) require(d.code(0,LOCATION_DECK,0)==jerry_beans,"Missing undrawn/unrecruited Plant");
        for(int p=0;p<2;++p)
          require(d.count(p,LOCATION_HAND)==0&&d.count(p,LOCATION_REMOVED)==0&&
                  d.count(p,LOCATION_EXTRA)==0,"Unexpected hand, banished, or Extra Deck cards");
        require(d.count(1,LOCATION_SZONE)==0,"Unexpected opposing Spell/Trap");
        std::cout<<"Observed: "<<(self?"activating monster tributed before responses":
                                                    "another Plant tributed; Lonefire stayed face-up")
                 <<"; "<<(disabled?"effect resolved disabled, cost retained":
                            exiled?"Exiled Force destroyed its target":"Lonefire recruited a Plant")
                 <<". Final zones and LP verified.\n";
        return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced chain choice");
      for(const auto& c:e.cards) {
        require((!setup_done||!active_drain)&&e.player==0&&c.code==skill_drain&&c.controller==0&&
                c.location==LOCATION_SZONE,"Unexpected chain candidate");
        if(requested&&confirmed) set_drain_offered=true;
      }
      if(requested&&confirmed&&!solved) check_cost();
      d.respond(-1);
    } else if(e.kind==MSG_CHAINING) {
      const auto& c=e.cards.at(0);
      require(!announced&&!finished&&e.chain==1&&e.player==0&&c.controller==0&&c.sequence==0&&
              c.code==(setup_done?actor:skill_drain)&&
              c.location==(setup_done?LOCATION_MZONE:LOCATION_SZONE)&&
              (setup_done?requested:setup_requested),"Unexpected activation location or link");
      announced=true;
    } else if(e.kind==MSG_POS_CHANGE) {
      require(!setup_done&&setup_requested&&!announced&&!drain_flipped&&
              e.from.code==skill_drain&&e.from.controller==0&&e.from.location==LOCATION_SZONE&&
              e.from.sequence==0&&e.from.position==POS_FACEDOWN_DEFENSE&&(e.to.position&POS_FACEUP),
              "Only Skill Drain should turn face-up before setup activation");
      drain_flipped=true;
    } else if(e.kind==MSG_PAY_LPCOST) {
      require(!setup_done&&announced&&!confirmed&&!drain_paid&&e.player==0&&e.mask==1000,
              "Expected one Skill Drain activation cost"); drain_paid=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(setup_done&&announced&&e.player==0&&e.minimum==1&&e.maximum==1,
              "Unexpected card selection");
      uint32_t want; int controller=0,location,sequence=-1;
      if(resolving) {
        require(recruit&&!recruit_selected&&cost_paid,"Unexpected recruitment selection");
        recruit_selected=true; want=jerry_beans; location=LOCATION_DECK;
        require(e.cards.size()==1,"Expected sole Plant in Deck");
      } else if(exiled) {
        require(!confirmed&&cost_paid&&!target_selected,"Target must be selected after Exiled's cost");
        target_selected=true; want=blue_eyes; controller=1; location=LOCATION_MZONE; sequence=0;
        require(e.cards.size()==1,"Only opposing monster should remain targetable");
      } else {
        require(!confirmed&&!cost_paid&&!cost_selected,"Expected Lonefire tribute-cost selection");
        require(e.cards.size()==2,"Both face-up Plants must be available as cost");
        for(auto code:{lonefire,jerry_beans})
          require(std::count_if(e.cards.begin(),e.cards.end(),[&](const CardRef& c) {
            return c.code==code&&c.controller==0&&c.location==LOCATION_MZONE&&
                   c.sequence==(code==lonefire?0:1);
          })==1,"Cost choices must include Lonefire and the other Plant");
        cost_selected=true; want=tribute; location=LOCATION_MZONE; sequence=tribute_zone;
      }
      int index=-1;
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want&&e.cards[i].controller==controller&&e.cards[i].location==location&&
           (sequence<0||e.cards[i].sequence==sequence)) index=int(i);
      require(index>=0,"Required cost, target, or recruit missing");
      d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_MOVE) {
      if(e.to.location==LOCATION_GRAVE&&e.from.controller==0) {
        require(setup_done&&announced&&!confirmed&&!resolving&&!cost_paid&&
                e.from.code==tribute&&e.from.location==LOCATION_MZONE&&e.from.sequence==tribute_zone&&
                e.to.controller==0&&(e.reason&(REASON_COST|REASON_RELEASE))==(REASON_COST|REASON_RELEASE)&&
                !(e.reason&(REASON_EFFECT|REASON_DESTROY|REASON_BATTLE))&&(exiled||cost_selected),
                "Tribute must leave as activation cost before confirmation/responses");
        cost_paid=true;
      } else if(e.to.location==LOCATION_GRAVE&&e.from.controller==1) {
        require(exiled&&resolving&&cost_paid&&target_selected&&!destroyed&&e.from.code==blue_eyes&&
                e.from.location==LOCATION_MZONE&&e.to.controller==1&&
                (e.reason&(REASON_EFFECT|REASON_DESTROY))==(REASON_EFFECT|REASON_DESTROY)&&
                !(e.reason&(REASON_COST|REASON_RELEASE|REASON_BATTLE)),
                "Target destruction must happen by effect during resolution"); destroyed=true;
      } else {
        require(recruit&&resolving&&recruit_selected&&e.from.code==jerry_beans&&
                e.from.controller==0&&e.from.location==LOCATION_DECK&&e.to.controller==0&&
                e.to.location==LOCATION_MZONE&&e.to.sequence==2&&e.reason==REASON_SPSUMMON&&
                !(e.reason&(REASON_COST|REASON_DESTROY|REASON_BATTLE)),"Unexpected card movement");
      }
    } else if(e.kind==MSG_CHAINED) {
      require(announced&&!confirmed&&e.chain==1,"Unexpected chain confirmation");
      if(setup_done) { check_cost(); require(target_selected==exiled,"Missing Exiled target"); }
      else require(drain_paid,"Drain cost unpaid");
      confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed&&!resolving&&!solved&&e.chain==1,"Unexpected resolution");
      if(setup_done) { check_cost(); check_drain(); }
      resolving=true;
    } else if(e.kind==MSG_CHAIN_DISABLED) {
      require(setup_done&&disabled&&resolving&&e.chain==1&&!effect_disabled,
              "Only Lonefire remaining face-up under Skill Drain should be disabled"); effect_disabled=true;
    } else if(e.kind==MSG_CHAIN_NEGATED) {
      throw std::runtime_error("Skill Drain must not negate activation");
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(recruit&&resolving&&recruit_selected&&!place_selected&&e.player==0&&
              e.count==1&&!(e.mask&(1u<<2)),"Expected recruitment placement");
      place_selected=true; d.respond({0,LOCATION_MZONE,2});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(recruit&&resolving&&recruit_selected&&!position_selected&&e.player==0&&
              e.cards.at(0).code==jerry_beans&&(e.mask&POS_FACEUP_ATTACK),
              "Expected recruited Plant's position"); position_selected=true; d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONING) {
      const auto& c=e.cards.at(0);
      require(recruit&&resolving&&place_selected&&position_selected&&!summoning&&
              c.code==jerry_beans&&c.controller==0&&c.location==LOCATION_MZONE&&c.sequence==2&&
              c.position==POS_FACEUP_ATTACK,"Unexpected Special Summon"); summoning=true;
    } else if(e.kind==MSG_SPSUMMONED) {
      require(recruit&&resolving&&summoning&&!summoned,"Unexpected summon completion"); summoned=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving&&!solved&&e.chain==1,"Unexpected solved link");
      if(setup_done) require(effect_disabled==disabled&&summoned==recruit&&destroyed==exiled,
                             "Incorrect monster effect resolution");
      resolving=false; solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved&&!resolving&&!finished,"Incomplete chain");
      if(!setup_done) {
        require(drain_paid&&!requested,"Drain setup must finish before ignition"); check_drain();
        setup_done=true; announced=confirmed=solved=false;
      } else finished=true;
    } else if(e.kind==MSG_SELECT_EFFECTYN||e.kind==MSG_SELECT_YESNO||e.kind==MSG_SELECT_TRIBUTE||
              e.kind==MSG_SELECT_BATTLECMD||e.kind==MSG_DRAW||e.kind==MSG_DAMAGE||
              e.kind==MSG_DAMAGE_STEP_START||e.kind==MSG_SUMMONING||e.kind==MSG_FLIPSUMMONING||
              e.kind==MSG_SET) {
      throw std::runtime_error("Unexpected action in tribute-cost fixture");
    }
    return false;
  });
}
