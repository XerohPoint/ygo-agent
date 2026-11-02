#pragma once
#include "harness.h"

// Real turn progression and attack commands exercise delayed Flip processing,
// battle destruction, and optional recruiters inside the Damage Step.
inline void damage_step_triggers(certification::Assets& assets, int options,
                                 bool recruiter, bool accept, bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  const uint32_t defender=recruiter?tomato:dekoichi;
  d.add({defender,0,0,LOCATION_MZONE,0,
         static_cast<uint8_t>(recruiter?POS_FACEUP_ATTACK:POS_FACEDOWN_DEFENSE)});
  d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({recruiter?sangan:blue_eyes,0,0,LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE});
  d.add({jar,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  int turns=0, announced=0, confirmed=0, solved=0;
  bool passed=false, entered=false, requested=false, attacked=false, damage=false;
  bool flipped=false, calculated=false, damaged=false, grave=false, ended=false;
  bool optional=false, resolving=false, chain_end=false, drawn=false, summoned=false;
  bool jar_offered=false, damage_response=false;
  auto decide = [&] {
    require(recruiter && damage && !ended && calculated && damaged && grave &&
            !optional && confirmed==0 && d.code(0,LOCATION_GRAVE,0)==tomato,
            "Recruiter choice must occur after battle destruction reaches the Graveyard");
    optional=true;
    std::cout << "Observed: " << (accept?"accept":"decline")
              << " Mystic Tomato in the Damage Step after its battle destruction.\n";
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {
      require(e.player==turns && turns<2,"Unexpected turn progression"); ++turns;
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      if(e.player==0) {
        require(turns==1 && !passed && e.to_end,"P0 must end turn 1");
        passed=true; d.respond(7);
      } else {
        require(turns==2 && passed && !entered && e.to_battle,"P1 must enter the Battle Phase");
        entered=true; d.respond(6);
      }
    } else if(e.kind==MSG_SELECT_BATTLECMD) {
      require(e.player==1 && entered,"Unexpected battle command player");
      if(!requested) {
        require(e.idle[0].size()==1 && e.idle[0][0].code==blue_eyes,
                "Expected sole Blue-Eyes attacker");
        requested=true; d.respond(1);
      } else {
        const bool activates=!recruiter || accept;
        require(attacked && damage && calculated && grave && ended && jar_offered &&
                damage_response && !resolving && announced==int(activates) &&
                confirmed==int(activates) && solved==int(activates) && chain_end==activates &&
                optional==recruiter && drawn==!recruiter && summoned==(recruiter&&accept),
                "Missing Damage Step or trigger-resolution checkpoint");
        require(d.lp(0)==(recruiter?6400:8000) && d.lp(1)==8000 &&
                d.count(0,LOCATION_GRAVE)==1 && d.code(0,LOCATION_GRAVE,0)==defender &&
                d.count(1,LOCATION_GRAVE)==0 && d.count(1,LOCATION_MZONE)==1 &&
                d.code(1,LOCATION_MZONE,0)==blue_eyes &&
                d.count(0,LOCATION_SZONE)==1 && d.code(0,LOCATION_SZONE,0)==jar &&
                (d.position(0,LOCATION_SZONE,0)&POS_FACEDOWN),
                "Unexpected battle aftermath or Jar activation");
        require(d.count(0,LOCATION_HAND)==int(!recruiter) &&
                d.count(0,LOCATION_DECK)==int(recruiter&&!accept) &&
                d.count(0,LOCATION_MZONE)==int(recruiter&&accept),
                "Unexpected draw, recruitment, or declined-effect result");
        if(!recruiter) require(flipped && !damaged && d.code(0,LOCATION_HAND,0)==blue_eyes,
                               "Dekoichi must draw once despite battle destruction");
        if(recruiter&&accept) require(d.code(0,LOCATION_MZONE,0)==sangan &&
                                      d.position(0,LOCATION_MZONE,0)==POS_FACEUP_ATTACK,
                                      "Tomato must recruit Sangan in Attack Position");
        std::cout << "Observed: Damage Step completed; "
                  << (recruiter?"1600 battle damage; recruiter outcome verified."
                               :"Flip draw resolved before Dekoichi entered the Graveyard.")
                  << " Jar offered outside, excluded inside Damage Step.\n";
        return true;
      }
    } else if(e.kind==MSG_ATTACK) {
      require(requested && !attacked && e.from.controller==1 && e.from.location==LOCATION_MZONE &&
              e.from.sequence==0 && e.to.controller==0 && e.to.location==LOCATION_MZONE &&
              e.to.sequence==0,"Unexpected attack declaration");
      attacked=true;
    } else if(e.kind==MSG_DAMAGE_STEP_START) {
      require(attacked && !damage,"Unexpected Damage Step start"); damage=true;
    } else if(e.kind==MSG_POS_CHANGE && e.from.code==defender) {
      require(!recruiter && damage && !calculated && !flipped &&
              e.from.controller==0 && e.from.location==LOCATION_MZONE &&
              (e.from.position&POS_FACEDOWN) && e.to.position==POS_FACEUP_DEFENSE,
              "Battle flip must precede damage calculation");
      flipped=true;
    } else if(e.kind==MSG_BATTLE) {
      require(damage && !ended && !calculated && (recruiter || flipped) &&
              e.cards.size()==2 && e.cards[0].controller==1 && e.cards[1].controller==0 &&
              e.cards[0].location==LOCATION_MZONE && e.cards[1].location==LOCATION_MZONE &&
              e.battle_stats[0]==3000 && e.battle_stats[2]==1400 &&
              e.battle_stats[3]==(recruiter?1100u:1000u) &&
              !e.battle_destroyed[0] && e.battle_destroyed[1],
              "Expected Blue-Eyes to destroy the defender by battle");
      calculated=true;
    } else if(e.kind==MSG_DAMAGE) {
      require(recruiter && calculated && !grave && !damaged && e.player==0 && e.mask==1600,
              "Expected exactly 1600 battle damage");
      damaged=true;
    } else if(e.kind==MSG_MOVE && e.from.code==defender && e.to.location==LOCATION_GRAVE) {
      require(damage && !ended && calculated && !grave && e.from.location==LOCATION_MZONE &&
              e.from.controller==0 && e.to.controller==0 &&
              (e.reason&(REASON_BATTLE|REASON_DESTROY))==(REASON_BATTLE|REASON_DESTROY) &&
              !(e.reason&REASON_EFFECT) &&
              (recruiter ? damaged&&!optional : drawn&&chain_end),
              "Battle destruction must follow calculation and the appropriate trigger window");
      grave=true;
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(e.player==0 && e.cards.size()==1 && e.cards[0].code==tomato,
              "Unexpected optional effect");
      decide(); d.respond(accept?1:0);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced ordering choice");
      // A P0 response window with cards still in the Deck proves Jar is
      // otherwise usable; exclusion cannot be explained by an empty Deck.
      if(damage && !ended && e.player==0 && d.count(0,LOCATION_DECK)>0) damage_response=true;
      if(e.cards.empty()) d.respond(-1);
      else if(e.cards.size()==1 && e.cards[0].code==tomato) {
        require(e.player==0,"Wrong recruiter controller"); decide(); d.respond(accept?0:-1);
      } else {
        require(e.player==0 && e.cards.size()==1 && e.cards[0].code==jar &&
                (!damage || ended), "Jar of Greed must not be offered in the Damage Step");
        if(attacked && !damage) jar_offered=true;
        d.respond(-1);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(damage && !ended && calculated && announced==0 && e.chain==1 &&
              e.player==0 && e.cards.at(0).code==defender &&
              (recruiter ? grave&&optional&&accept : flipped&&!grave),
              "Trigger activated in the wrong Damage Step window");
      ++announced;
    } else if(e.kind==MSG_CHAINED) {
      require(announced==1 && confirmed==0 && e.chain==1,"Unexpected battle chain confirmation");
      ++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed==1 && !resolving && solved==0 && e.chain==1,"Unexpected battle resolution");
      resolving=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving && e.chain==1,"Unexpected solved battle link");
      resolving=false; ++solved;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved==1 && !chain_end && damage && !ended,"Unexpected battle chain end");
      chain_end=true;
    } else if(e.kind==MSG_DRAW) {
      require(!recruiter && resolving && !grave && !drawn && e.player==0 && e.count==1 &&
              (e.cards.at(0).code&0x7fffffffu)==blue_eyes,
              "Flip draw must resolve after calculation, before battle destruction");
      drawn=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(e.minimum==1 && e.maximum==1 && e.cards.size()==1,"Unexpected battle card selection");
      if(resolving) {
        require(recruiter && accept && e.player==0 && e.cards[0].code==sangan &&
                e.cards[0].location==LOCATION_DECK,"Expected Sangan as sole recruit");
      } else require(requested && !damage && e.player==1 && e.cards[0].code==defender &&
                     e.cards[0].location==LOCATION_MZONE,"Expected sole attack target");
      d.respond({1,0});
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(recruiter && accept && resolving && e.player==0 && e.count==1 && !(e.mask&1u),
              "Unexpected recruitment placement");
      d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SPSUMMONING) {
      require(recruiter && accept && resolving && grave && !summoned &&
              e.cards.at(0).code==sangan && e.cards[0].controller==0,
              "Unexpected recruit announcement");
    } else if(e.kind==MSG_SPSUMMONED) {
      require(recruiter && accept && resolving && !summoned,"Unexpected recruit success"); summoned=true;
    } else if(e.kind==MSG_DAMAGE_STEP_END) {
      require(damage && !ended && grave && (!recruiter || optional) &&
              ((!recruiter || accept) ? chain_end : !announced),
              "Damage Step ended before trigger handling finished");
      ended=true;
    } else if(e.kind==MSG_SELECT_YESNO || e.kind==MSG_SELECT_POSITION ||
              e.kind==MSG_SELECT_TRIBUTE || e.kind==MSG_SUMMONING || e.kind==MSG_PAY_LPCOST) {
      throw std::runtime_error("Unexpected action in Damage Step fixture");
    }
    return false;
  });
}
