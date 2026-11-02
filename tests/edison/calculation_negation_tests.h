#pragma once
#include "harness.h"

// Test 010: the negation response must be inside PHASE_DAMAGE_CAL, before
// any battle result. Real turn/attack commands reach the timing window.
inline void calculation_negation(certification::Assets& assets, int options,
                                 bool orange, bool accept, bool trace) {
  using namespace certification;
  const uint32_t negator=orange?herald:wrath;
  TestDuel d(assets,0,0,trace,options);
  d.add({blue_eyes,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({lily,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  for(uint8_t zone=0;zone<3;++zone)
    d.add({std::array<uint32_t,3>{rush,walls,chalice}[zone],0,0,LOCATION_SZONE,zone,POS_FACEDOWN_DEFENSE});
  if(orange) d.add({herald,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  else d.add({wrath,0,0,LOCATION_SZONE,3,POS_FACEDOWN_DEFENSE});
  d.add({orange?watapon:blue_eyes,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  const std::vector<uint32_t> chain=accept?std::vector<uint32_t>{lily,negator}:std::vector<uint32_t>{lily};
  int turns=0, announced=0, confirmed=0, solved=0, resolving=0;
  bool passed=false, entered=false, requested=false, attacked=false, damage=false, ended=false;
  bool lily_selected=false, decided=false, paid=false, negated=false, finished=false;
  bool battle=false, damage_dealt=false, lily_destroyed=false, defender_destroyed=false;
  bool calc_response=false;
  std::array<bool,3> early_offers{};
  std::vector<uint32_t> cost_cards;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {
      require(turns<2 && e.player==turns,"Unexpected calculation-fixture turn"); ++turns;
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      if(e.player==0) {
        require(turns==1 && !passed && e.to_end,"Expected P0 to end first turn"); passed=true; d.respond(7);
      } else {
        require(turns==2 && passed && !entered && e.to_battle,"Expected P1 Battle Phase"); entered=true; d.respond(6);
      }
    } else if(e.kind==MSG_SELECT_BATTLECMD) {
      require(entered && e.player==1,"Unexpected battle command");
      if(!requested) {
        require(e.idle[0].size()==1 && e.idle[0][0].code==lily,"Expected sole Lily attacker");
        requested=true; d.respond(1);
      } else {
        require(attacked && damage && ended && lily_selected && decided && paid && finished &&
                calc_response && early_offers[0] && early_offers[1] && early_offers[2] &&
                negated==accept && lily_destroyed==accept && defender_destroyed==!accept &&
                battle==!accept && damage_dealt==!accept,"Missing calculation-negation checkpoint");
        require(d.lp(0)==(accept?8000:7600) && d.lp(1)==6000,
                "Lily's 2000 LP cost must remain paid even when negated");
        require(d.count(0,LOCATION_MZONE)==int(accept) && d.count(1,LOCATION_MZONE)==int(!accept) &&
                d.count(1,LOCATION_HAND)==0 && d.count(1,LOCATION_SZONE)==0 &&
                d.count(1,LOCATION_GRAVE)==int(accept),"Unexpected final field");
        if(accept) require(d.code(0,LOCATION_MZONE,0)==blue_eyes &&
                           d.code(1,LOCATION_GRAVE,0)==lily,"Negation must preserve defender and destroy attacker");
        else require(d.code(1,LOCATION_MZONE,0)==lily &&
                     d.query_value(1,LOCATION_MZONE,0,QUERY_ATTACK)==400,
                     "Lily must survive and lose its temporary ATK boost");
        std::vector<uint32_t> expected_cost;
        if(accept) expected_cost=orange?std::vector<uint32_t>{herald,watapon}:std::vector<uint32_t>{blue_eyes};
        std::sort(cost_cards.begin(),cost_cards.end()); std::sort(expected_cost.begin(),expected_cost.end());
        require(cost_cards==expected_cost,"Wrong negation cost identities");
        std::vector<uint32_t> grave;
        for(int i=0;i<d.count(0,LOCATION_GRAVE);++i) grave.push_back(d.code(0,LOCATION_GRAVE,i));
        auto want_grave=expected_cost;
        if(accept&&!orange) want_grave.push_back(wrath);
        if(!accept) want_grave.push_back(blue_eyes);
        std::sort(grave.begin(),grave.end()); std::sort(want_grave.begin(),want_grave.end());
        require(grave==want_grave && d.count(0,LOCATION_HAND)==(accept?0:orange?2:1) &&
                d.count(0,LOCATION_SZONE)==3+int(!orange&&!accept),"Unexpected cost or negator aftermath");
        for(int zone=0;zone<3;++zone)
          require(d.code(0,LOCATION_SZONE,zone)==std::array<uint32_t,3>{rush,walls,chalice}[zone] &&
                  (d.position(0,LOCATION_SZONE,zone)&POS_FACEDOWN),"Ordinary modifier unexpectedly activated");
        if(!accept) {
          require(d.code(0,LOCATION_HAND,0)==(orange?herald:blue_eyes),"Decline must preserve cost/negator");
          if(orange) require(d.code(0,LOCATION_HAND,1)==watapon,"Decline must preserve Fairy cost");
          else require(d.code(0,LOCATION_SZONE,3)==wrath &&
                       (d.position(0,LOCATION_SZONE,3)&POS_FACEDOWN),"Declined Wrath must stay Set");
        }
        std::cout << "Observed: " << (orange?"Herald":"Divine Wrath") << (accept?" accepted":" declined")
                  << " during calculation; Lily paid 2000 LP; "
                  << (accept?"Lily destroyed by effect, no battle result or damage."
                            :"Lily reached 3400 ATK, dealt 400 damage, then returned to 400 ATK.") << '\n';
        return true;
      }
    } else if(e.kind==MSG_ATTACK) {
      require(requested && !attacked && e.from.controller==1 && e.from.location==LOCATION_MZONE &&
              e.from.sequence==0 && e.to.controller==0 && e.to.location==LOCATION_MZONE &&
              e.to.sequence==0,"Unexpected attack"); attacked=true;
    } else if(e.kind==MSG_DAMAGE_STEP_START) {
      require(attacked && !damage,"Unexpected Damage Step start"); damage=true;
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");
      const bool inside=damage&&!ended;
      const bool calc=inside&&d.phase()==PHASE_DAMAGE_CAL;
      int pick=-1;
      if(calc&&e.player==0) calc_response=true;
      for(size_t i=0;i<e.cards.size();++i) {
        auto code=e.cards[i].code;
        if(code==rush || code==walls || code==chalice) {
          require(e.player==0 && (!inside || (!calc&&!finished)),
                  "Ordinary modifier cannot respond during or after calculation");
          if(inside) early_offers[code==rush?0:code==walls?1:2]=true;
        } else if(code==lily) {
          require(calc && e.player==1 && !lily_selected && !battle && !announced,
                  "Lily must activate once during calculation");
          lily_selected=true; pick=int(i);
        } else if(code==negator) {
          require(calc && e.player==0 && !decided && confirmed==1 && announced==1 &&
                  paid && !battle && !resolving,"Negation must be offered to Lily CL1 inside calculation");
          decided=true;
          if(accept) pick=int(i);
        } else throw std::runtime_error("Unexpected calculation-fixture candidate");
      }
      d.respond(pick);
    } else if(e.kind==MSG_SELECT_CARD) {
      require(e.minimum==1 && e.maximum==1,"Unexpected card selection count");
      uint32_t want=blue_eyes; int owner=1, location=LOCATION_MZONE;
      if(damage) {
        require(accept && announced==2 && confirmed==1 && decided,"Unexpected negation cost prompt");
        owner=0; location=LOCATION_HAND; want=orange?watapon:blue_eyes;
      } else require(requested&&!attacked,"Unexpected attack selection");
      require(e.player==owner,"Wrong selection player");
      int index=-1;
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want && e.cards[i].location==location && e.cards[i].controller==0) index=int(i);
      require(index>=0,"Required target/cost unavailable"); d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_CHAINING) {
      require(damage && !ended && !battle && !finished && announced==confirmed &&
              announced<int(chain.size()) && e.chain==announced+1 &&
              e.cards.at(0).code==chain[announced] && e.player==(announced==0?1:0),
              "Unexpected calculation chain");
      require(announced==0?lily_selected:decided&&accept,"Unchosen activation"); ++announced;
    } else if(e.kind==MSG_PAY_LPCOST) {
      require(announced==1 && !paid && e.player==1 && e.mask==2000,"Expected Lily's 2000 LP cost"); paid=true;
    } else if(e.kind==MSG_CHAINED) {
      require(announced==confirmed+1 && e.chain==announced && paid,"Unexpected confirmation or unpaid Lily");
      ++confirmed;
      if(confirmed==2) require(cost_cards.size()==size_t(orange?2:1),"CL2 confirmed before cost payment");
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(!resolving && decided && confirmed==int(chain.size()) && e.chain==confirmed-solved,
              "Incorrect calculation-chain resolution"); resolving=e.chain;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving==e.chain && !(negated&&e.chain==1),"Negated link must be skipped");
      resolving=0; ++solved;
    } else if(e.kind==MSG_CHAIN_NEGATED) {
      require(accept && resolving==2 && e.chain==1 && !negated,"Expected activation negation of Lily CL1");
      negated=true;
    } else if(e.kind==MSG_CHAIN_DISABLED) {
      throw std::runtime_error("Expected activation negation, not effect-only disabling");
    } else if(e.kind==MSG_CHAIN_END) {
      require(!finished && confirmed==int(chain.size()),"Unexpected chain end");
      if(accept) require(negated && lily_destroyed && resolving==1 && solved==1,
                         "Negated Lily must be skipped after CL2 destroys it");
      else require(!resolving && solved==1,"Unnegated Lily must resolve");
      resolving=0; finished=true;
    } else if(e.kind==MSG_MOVE) {
      if(e.from.location==LOCATION_HAND && e.to.location==LOCATION_GRAVE) {
        require(accept && e.from.controller==0 && e.to.controller==0 && announced==2 && confirmed==1 &&
                (e.reason&REASON_COST) && !(e.reason&REASON_EFFECT) &&
                (orange?(e.from.code==herald||e.from.code==watapon):e.from.code==blue_eyes),
                "Unexpected negation cost move");
        require(orange?!(e.reason&REASON_DISCARD):bool(e.reason&REASON_DISCARD),
                "Herald sends; Wrath discards"); cost_cards.push_back(e.from.code);
      } else if(e.from.location==LOCATION_MZONE && e.to.location==LOCATION_GRAVE) {
        if(e.from.code==lily) {
          require(accept && negated && resolving==2 && !battle && !lily_destroyed &&
                  e.from.controller==1 && e.to.controller==1 &&
                  (e.reason&(REASON_EFFECT|REASON_DESTROY))==(REASON_EFFECT|REASON_DESTROY) &&
                  !(e.reason&REASON_BATTLE),"Negator must destroy Lily before battle calculation");
          lily_destroyed=true;
        } else {
          require(!accept && e.from.code==blue_eyes && battle && damage_dealt && !defender_destroyed &&
                  e.from.controller==0 && e.to.controller==0 &&
                  (e.reason&(REASON_BATTLE|REASON_DESTROY))==(REASON_BATTLE|REASON_DESTROY) &&
                  !(e.reason&REASON_EFFECT),"Unnegated Lily must destroy defender by battle");
          defender_destroyed=true;
        }
      }
    } else if(e.kind==MSG_BATTLE) {
      require(!accept && finished && !battle && e.cards.at(0).controller==1 && e.cards.at(1).controller==0 &&
              e.battle_stats[0]==3400 && e.battle_stats[2]==3000 &&
              !e.battle_destroyed[0] && e.battle_destroyed[1],"Unexpected or canceled battle result");
      battle=true;
    } else if(e.kind==MSG_DAMAGE) {
      require(!accept && battle && !damage_dealt && e.player==0 && e.mask==400,
              "Canceled attack must not deal damage"); damage_dealt=true;
    } else if(e.kind==MSG_DAMAGE_STEP_END) {
      require(damage && finished && !ended && (accept?lily_destroyed:defender_destroyed),
              "Damage Step ended before expected outcome"); ended=true;
    } else if(e.kind==MSG_DRAW || e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO ||
              e.kind==MSG_SELECT_PLACE || e.kind==MSG_SELECT_TRIBUTE || e.kind==MSG_SELECT_POSITION) {
      throw std::runtime_error("Unexpected action or attack replay during calculation-negation test");
    }
    return false;
  });
}
