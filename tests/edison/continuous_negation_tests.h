#pragma once
#include "harness.h"

enum class ContinuousCase { LilyNegated, LilyNoDrain, LilyRestored, FlipNegated, Graveyard };

inline void continuous_negation(certification::Assets& assets, int options,
                               ContinuousCase mode, bool trace) {
  using namespace certification;
  const bool lily_case=mode<=ContinuousCase::LilyRestored;
  const bool activate_drain=mode!=ContinuousCase::LilyNoDrain;
  const bool remove_drain=mode==ContinuousCase::LilyRestored;
  const bool active=activate_drain&&!remove_drain;
  const bool disabled=mode==ContinuousCase::LilyNegated || mode==ContinuousCase::FlipNegated;
  const bool recruiter=mode==ContinuousCase::Graveyard;
  const uint32_t defender=lily_case?blue_eyes:recruiter?tomato:dekoichi;
  const uint32_t attacker=lily_case?lily:blue_eyes;
  const uint32_t effect_card=lily_case?lily:defender;
  TestDuel d(assets,0,0,trace,options);
  d.add({defender,0,0,LOCATION_MZONE,0,
         static_cast<uint8_t>(mode==ContinuousCase::FlipNegated?POS_FACEDOWN_DEFENSE:POS_FACEUP_ATTACK)});
  d.add({attacker,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({skill_drain,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  if(remove_drain) d.add({mst,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  if(!lily_case) d.add({recruiter?sangan:blue_eyes,0,0,LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE});
  std::vector<uint32_t> chains;
  if(activate_drain) chains.push_back(skill_drain);
  if(remove_drain) chains.push_back(mst);
  const size_t setup_count=chains.size();
  chains.push_back(effect_card);
  size_t chain_no=0;
  bool announced=false, confirmed=false, resolving=false, solved=false, setup_requested=false;
  bool drain_paid=false, drain_removed=false, removal_target=false, lily_paid=false, effect_selected=false;
  bool passed=false, entered=false, requested=false, attacked=false, damage=false, battle=false, ended=false;
  bool effect_disabled=false, defender_moved=false, attacker_moved=false, damage_dealt=false;
  bool flipped=false, recruited=false, drain_offered=false, damage_prompt=false;
  int turns=0;
  auto current=[&] { require(chain_no<chains.size(),"Unexpected extra chain"); return chains[chain_no]; };
  auto choose_effect=[&] {
    require(damage&&!ended&&!effect_selected&&chain_no==setup_count,"Unexpected effect decision");
    if(lily_case) require(d.phase()==PHASE_DAMAGE_CAL&&!battle,
                          "Lily must remain activatable during calculation");
    else require(recruiter&&battle&&defender_moved&&d.code(0,LOCATION_GRAVE,0)==tomato,
                 "Tomato must trigger from the Graveyard");
    effect_selected=true;
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {
      require(turns<2&&e.player==turns,"Unexpected turn"); ++turns;
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      if(e.player==0) {
        require(turns==1&&!passed,"Unexpected P0 idle");
        if(chain_no<setup_count) {
          require(!setup_requested,"Setup activation returned to idle early");
          int index=-1;
          for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==current()) index=int(i);
          require(index>=0,"Required setup activation missing");
          setup_requested=true; d.respond((index<<16)|5);
        } else {
          require(e.to_end&&drain_paid==activate_drain&&drain_removed==remove_drain,
                  "Setup must finish before passing turn");
          passed=true; d.respond(7);
        }
      } else {
        require(turns==2&&passed&&!entered&&e.to_battle,"Expected P1 battle entry");
        entered=true; d.respond(6);
      }
    } else if(e.kind==MSG_SELECT_BATTLECMD) {
      require(entered&&e.player==1,"Unexpected battle command");
      if(!requested) {
        require(e.idle[0].size()==1&&e.idle[0][0].code==attacker,"Expected sole fixture attacker");
        requested=true; d.respond(1);
      } else {
        require(attacked&&damage&&battle&&ended&&chain_no==chains.size()&&!resolving&&
                effect_disabled==disabled&&lily_paid==lily_case&&
                effect_selected==(lily_case||recruiter)&&recruited==recruiter&&
                defender_moved==!(lily_case&&disabled)&&attacker_moved==(lily_case&&disabled)&&
                damage_dealt==(lily_case||recruiter)&&damage_prompt&&
                (activate_drain||drain_offered),"Missing continuous-negation checkpoint");
        int p0lp=activate_drain?7000:8000;
        if(lily_case&&!disabled) p0lp-=400;
        if(recruiter) p0lp-=1600;
        require(d.lp(0)==p0lp&&d.lp(1)==(lily_case?(disabled?3400:6000):8000),
                "Negated effects must retain paid costs and correct battle damage");
        require(d.count(0,LOCATION_MZONE)==int((lily_case&&disabled)||recruiter)&&
                d.count(1,LOCATION_MZONE)==int(!(lily_case&&disabled))&&
                d.count(0,LOCATION_HAND)==0&&d.count(1,LOCATION_HAND)==0&&
                d.count(0,LOCATION_DECK)==int(mode==ContinuousCase::FlipNegated),
                "Incorrect field, hand, or Deck outcome");
        if(recruiter) require(d.code(0,LOCATION_MZONE,0)==sangan&&
                              d.position(0,LOCATION_MZONE,0)==POS_FACEUP_ATTACK,
                              "Skill Drain must not prevent Tomato's Graveyard recruitment");
        if(lily_case&&disabled) require(d.code(0,LOCATION_MZONE,0)==blue_eyes,"Defender should survive");
        if(lily_case&&!disabled) require(d.code(1,LOCATION_MZONE,0)==lily&&
                         d.query_value(1,LOCATION_MZONE,0,QUERY_ATTACK)==400,"Lily's boost must expire");
        if(mode==ContinuousCase::FlipNegated)
          require(flipped&&d.code(0,LOCATION_DECK,0)==blue_eyes,"Negated Flip must not draw");
        require(d.count(0,LOCATION_SZONE)==int(!remove_drain)&&d.count(1,LOCATION_SZONE)==0,
                "Unexpected Spell/Trap aftermath");
        if(!remove_drain) require(d.code(0,LOCATION_SZONE,0)==skill_drain&&
                          bool(d.position(0,LOCATION_SZONE,0)&POS_FACEUP)==active,
                          "Skill Drain must retain expected face-up/Set state");
        for(int p=0;p<2;++p) {
          std::vector<uint32_t> grave,want;
          for(int i=0;i<d.count(p,LOCATION_GRAVE);++i) grave.push_back(d.code(p,LOCATION_GRAVE,i));
          if(p==0) {
            if(remove_drain) want={skill_drain,mst};
            if(defender_moved) want.push_back(defender);
          } else if(attacker_moved) want.push_back(attacker);
          std::sort(grave.begin(),grave.end()); std::sort(want.begin(),want.end());
          require(grave==want,"Unexpected Graveyard identities");
        }
        std::cout<<"Observed: "<<(disabled?"effect activated and resolved disabled; no activation negation."
                                  :recruiter?"Graveyard trigger resolved while Skill Drain remained active."
                                  :remove_drain?"removing Skill Drain restored Lily's 3000 ATK gain."
                                               :"Set Skill Drain excluded from Damage Step; Lily resolved normally.")
                 <<" Costs, battle result, and final zones verified.\n";
        return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced ordering");
      if(damage&&!ended&&e.player==0) damage_prompt=true;
      int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        auto code=e.cards[i].code;
        if(code==skill_drain||code==mst) {
          require(e.player==0&&(!damage||ended),"Skill Drain/MST cannot activate in the Damage Step");
          if(code==skill_drain&&attacked&&!damage) drain_offered=true;
        } else {
          require(code==effect_card&&e.player==(lily_case?1:0),"Unexpected monster candidate");
          choose_effect(); pick=int(i);
        }
      }
      d.respond(pick);
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(recruiter&&e.player==0&&e.cards.size()==1&&e.cards[0].code==tomato,
              "Expected optional Graveyard trigger"); choose_effect(); d.respond(1);
    } else if(e.kind==MSG_SELECT_CARD) {
      require(e.minimum==1&&e.maximum==1,"Unexpected target count");
      uint32_t want; int player,location,controller=0;
      if(chain_no<setup_count) {
        require(current()==mst&&announced&&!confirmed&&!removal_target,"Unexpected setup target");
        removal_target=true; want=skill_drain; player=0; location=LOCATION_SZONE;
      } else if(resolving) {
        require(recruiter&&current()==tomato,"Unexpected effect selection");
        want=sangan; player=0; location=LOCATION_DECK;
      } else {
        require(requested&&!damage,"Unexpected attack target");
        want=defender; player=1; location=LOCATION_MZONE;
      }
      require(e.player==player,"Wrong selection player");
      int index=-1;
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want&&e.cards[i].controller==controller&&e.cards[i].location==location) index=int(i);
      require(index>=0,"Required target/recruit missing"); d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(e.player==0&&e.count==1,"Unexpected placement");
      if(chain_no<setup_count) {
        require(current()==mst&&setup_requested&&!(e.mask&(1u<<9)),"Expected MST placement");
        d.respond({0,LOCATION_SZONE,1});
      } else {
        require(recruiter&&resolving&&!(e.mask&1u),"Expected Sangan placement"); d.respond({0,LOCATION_MZONE,0});
      }
    } else if(e.kind==MSG_CHAINING) {
      require(!announced&&e.chain==1&&e.cards.at(0).code==current()&&
              e.player==int(chain_no==setup_count&&lily_case),"Unexpected chain activation");
      if(chain_no<setup_count) require(setup_requested&&!damage,"Setup activated in wrong window");
      else require(damage&&!ended&&(lily_case?effect_selected&&!battle:recruiter?effect_selected&&defender_moved:
                                   flipped&&battle&&!defender_moved),"Monster activated in wrong window");
      announced=true;
    } else if(e.kind==MSG_PAY_LPCOST) {
      if(current()==skill_drain) {
        require(announced&&!confirmed&&!drain_paid&&e.player==0&&e.mask==1000,"Expected Drain cost");
        drain_paid=true;
      } else {
        require(current()==lily&&announced&&!confirmed&&!lily_paid&&e.player==1&&e.mask==2000,
                "Expected Lily cost despite continuous negation"); lily_paid=true;
      }
    } else if(e.kind==MSG_CHAINED) {
      require(announced&&!confirmed&&e.chain==1,"Unexpected chain confirmation");
      if(current()==skill_drain) require(drain_paid,"Skill Drain cost unpaid");
      if(current()==lily) require(lily_paid,"Lily cost unpaid");
      confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed&&!resolving&&!solved&&e.chain==1,"Unexpected resolution"); resolving=true;
    } else if(e.kind==MSG_CHAIN_DISABLED) {
      require(disabled&&chain_no==setup_count&&resolving&&e.chain==1&&!effect_disabled,
              "Only the face-up monster effect should be disabled"); effect_disabled=true;
    } else if(e.kind==MSG_CHAIN_NEGATED) {
      throw std::runtime_error("Skill Drain must not negate activation");
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving&&e.chain==1,"Unexpected solved link");
      if(chain_no==setup_count) require(effect_disabled==disabled,"Missing or incorrect effect negation");
      resolving=false; solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved&&!resolving,"Incomplete chain");
      if(current()==skill_drain) require(d.lp(0)==7000&&
                      (d.position(0,LOCATION_SZONE,0)&POS_FACEUP),"Skill Drain must resolve before battle");
      if(current()==mst) require(removal_target&&drain_removed,"MST must remove Drain before battle");
      ++chain_no; announced=confirmed=solved=setup_requested=false;
    } else if(e.kind==MSG_ATTACK) {
      require(requested&&!attacked&&e.from.controller==1&&e.from.location==LOCATION_MZONE&&
              e.from.sequence==0&&e.to.controller==0&&e.to.location==LOCATION_MZONE&&
              e.to.sequence==0,"Unexpected attack"); attacked=true;
    } else if(e.kind==MSG_DAMAGE_STEP_START) {
      require(attacked&&!damage,"Unexpected Damage Step start"); damage=true;
    } else if(e.kind==MSG_POS_CHANGE&&e.from.code==dekoichi) {
      require(mode==ContinuousCase::FlipNegated&&damage&&!battle&&!flipped&&
              e.to.position==POS_FACEUP_DEFENSE,"Expected battle flip"); flipped=true;
    } else if(e.kind==MSG_BATTLE) {
      require(damage&&!battle&&(lily_case?chain_no==chains.size():chain_no==setup_count)&&
              e.battle_stats[0]==(lily_case?(disabled?400u:3400u):3000u)&&
              e.battle_stats[2]==(lily_case?3000u:1400u)&&
              e.battle_destroyed[0]==(lily_case&&disabled)&&
              e.battle_destroyed[1]==!(lily_case&&disabled),"Wrong battle result under continuous negation");
      battle=true;
    } else if(e.kind==MSG_DAMAGE) {
      require(battle&&!damage_dealt&&(lily_case||recruiter)&&
              e.player==(lily_case&&disabled?1:0)&&e.mask==(lily_case?(disabled?2600u:400u):1600u),
              "Wrong battle damage"); damage_dealt=true;
    } else if(e.kind==MSG_MOVE&&e.to.location==LOCATION_GRAVE) {
      if(e.from.code==skill_drain) {
        require(remove_drain&&current()==mst&&resolving&&!drain_removed&&e.from.location==LOCATION_SZONE&&
                (e.reason&(REASON_EFFECT|REASON_DESTROY))==(REASON_EFFECT|REASON_DESTROY),
                "Expected MST effect destruction of Drain"); drain_removed=true;
      } else if(e.from.location==LOCATION_MZONE) {
        require(damage&&battle&&!ended&&(e.reason&(REASON_BATTLE|REASON_DESTROY))==(REASON_BATTLE|REASON_DESTROY)&&
                !(e.reason&REASON_EFFECT),"Expected battle destruction, not effect destruction");
        if(e.from.controller==1) {
          require(lily_case&&disabled&&e.from.code==lily&&!attacker_moved,"Unexpected attacker destruction");
          attacker_moved=true;
        } else {
          require(e.from.code==defender&&!defender_moved,"Unexpected defender destruction");
          if(mode==ContinuousCase::FlipNegated) require(effect_disabled&&chain_no==chains.size(),
                                                       "Flip must resolve disabled before battle destruction");
          defender_moved=true;
        }
      }
    } else if(e.kind==MSG_SPSUMMONING) {
      require(recruiter&&resolving&&!recruited&&e.cards.at(0).code==sangan&&e.cards[0].controller==0,
              "Unexpected recruit");
    } else if(e.kind==MSG_SPSUMMONED) {
      require(recruiter&&resolving&&!recruited,"Unexpected Special Summon"); recruited=true;
    } else if(e.kind==MSG_DAMAGE_STEP_END) {
      require(damage&&battle&&!ended&&chain_no==chains.size(),"Damage Step ended before resolution"); ended=true;
    } else if(e.kind==MSG_DRAW||e.kind==MSG_SELECT_YESNO||e.kind==MSG_SELECT_TRIBUTE||
              e.kind==MSG_SELECT_POSITION) {
      throw std::runtime_error("Unexpected draw or prompt in continuous-negation fixture");
    }
    return false;
  });
}
