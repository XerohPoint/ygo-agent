#pragma once
#include "harness.h"

enum class DamageException { Rush, Walls, Lily, Wrath, Herald, DeclineWrath };

inline void damage_step_exception(certification::Assets& assets, int options,
                                  DamageException mode, bool trace) {
  using namespace certification;
  const bool negation_case=mode>=DamageException::Wrath;
  const bool negate=mode==DamageException::Wrath || mode==DamageException::Herald;
  const bool orange=mode==DamageException::Herald;
  const bool calculation=mode==DamageException::Lily;
  const uint32_t attacker=negation_case?watapon:calculation?lily:blue_eyes;
  const uint32_t modifier=mode==DamageException::Rush?rush:walls;
  const uint32_t negator=orange?herald:wrath;
  TestDuel d(assets,0,0,trace,options);
  d.add({dekoichi,0,0,LOCATION_MZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({attacker,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({blue_eyes,0,0,LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE});
  d.add({rush,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({walls,0,0,LOCATION_SZONE,1,POS_FACEDOWN_DEFENSE});
  d.add({chalice,0,0,LOCATION_SZONE,2,POS_FACEDOWN_DEFENSE});
  if(!negation_case && !calculation)
    d.add({modifier,0,0,LOCATION_SZONE,3,POS_FACEDOWN_DEFENSE}); // unused legal copy
  if(negation_case) {
    if(orange) d.add({herald,1,1,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
    else d.add({wrath,1,1,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
    d.add({orange?watapon:blue_eyes,1,1,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  }
  std::vector<std::vector<uint32_t>> chains;
  if(!negation_case) chains.push_back({calculation?lily:modifier});
  chains.push_back(negate?std::vector<uint32_t>{dekoichi,negator}:std::vector<uint32_t>{dekoichi});
  int turn=0, chain_no=0, announced=0, confirmed=0, solved=0, resolving=0, costs=0;
  bool passed=false, entered=false, requested=false, attacked=false, damage=false, flipped=false;
  bool battle=false, ended=false, chosen=false, negator_seen=false, negated=false, moved=false;
  bool drawn=false, lp_cost=false, battle_damage=false, late_prompt=false, calc_prompt=false;
  std::array<bool,3> early_offers{};
  auto active_code=[&] { return chains.at(chain_no).at(resolving-1); };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {
      require(turn<2 && e.player==turn,"Unexpected exception-fixture turn"); ++turn;
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      if(e.player==0) {
        require(turn==1 && !passed && e.to_end,"Expected P0 end turn"); passed=true; d.respond(7);
      } else {
        require(turn==2 && passed && !entered && e.to_battle,"Expected P1 Battle Phase");
        entered=true; d.respond(6);
      }
    } else if(e.kind==MSG_SELECT_BATTLECMD) {
      require(e.player==1 && entered,"Unexpected battle command");
      if(!requested) {
        require(e.idle[0].size()==1 && e.idle[0][0].code==attacker,"Expected sole fixture attacker");
        requested=true; d.respond(1);
      } else {
        require(ended && battle && flipped && chain_no==int(chains.size()) && !resolving &&
                drawn==!negate && moved==(!negation_case || negate) && negated==negate &&
                lp_cost==calculation && battle_damage==negation_case &&
                early_offers[0] && early_offers[1] && early_offers[2] && late_prompt &&
                (!calculation || calc_prompt) && (!negation_case || negator_seen),
                "Missing modifier/negation/window checkpoint");
        require(d.lp(0)==8000 && d.lp(1)==(calculation?6000:negation_case?7200:8000) &&
                d.count(0,LOCATION_HAND)==int(!negate) && d.count(0,LOCATION_DECK)==int(negate) &&
                d.count(0,LOCATION_MZONE)==int(negation_case&&!negate) &&
                d.count(1,LOCATION_MZONE)==1 && d.code(1,LOCATION_MZONE,0)==attacker,"Unexpected final LP, draw, or monster state");
        require(d.count(0,LOCATION_SZONE)==3,"Unused modifiers must remain Set");
        for(int zone=0;zone<3;++zone) {
          // The activated original (zone 0 or 1) is replaced in this check by its spare at 3.
          int at=(!negation_case&&!calculation && zone==(mode==DamageException::Rush?0:1))?3:zone;
          require(d.code(0,LOCATION_SZONE,at)==std::array<uint32_t,3>{rush,walls,chalice}[zone] &&
                  (d.position(0,LOCATION_SZONE,at)&POS_FACEDOWN),"Modifier unexpectedly used");
        }
        require(costs==(negate?(orange?2:1):0) && d.count(1,LOCATION_GRAVE)==(negate?2:0) &&
                d.count(1,LOCATION_HAND)==(negation_case&&!negate?1:0) &&
                d.count(1,LOCATION_SZONE)==(negation_case&&!orange&&!negate?1:0),
                "Incorrect negation cost or final negator zones");
        if(drawn) require(d.code(0,LOCATION_HAND,0)==blue_eyes,"Expected Flip draw in hand");
        else require(d.code(0,LOCATION_DECK,0)==blue_eyes,"Negated Flip must leave draw card in Deck");
        if(negation_case&&!negate)
          require(d.code(0,LOCATION_MZONE,0)==dekoichi &&
                  d.position(0,LOCATION_MZONE,0)==POS_FACEUP_DEFENSE &&
                  d.code(1,LOCATION_SZONE,0)==wrath &&
                  (d.position(1,LOCATION_SZONE,0)&POS_FACEDOWN) &&
                  d.code(1,LOCATION_HAND,0)==blue_eyes,"Declined negation changed fixture state");
        if(negate) {
          std::vector<uint32_t> paid{d.code(1,LOCATION_GRAVE,0),d.code(1,LOCATION_GRAVE,1)};
          std::vector<uint32_t> want_paid=orange?std::vector<uint32_t>{herald,watapon}
                                                    :std::vector<uint32_t>{wrath,blue_eyes};
          std::sort(paid.begin(),paid.end()); std::sort(want_paid.begin(),want_paid.end());
          require(paid==want_paid,"Wrong negator/cost identities in Graveyard");
        }
        std::vector<uint32_t> grave;
        for(int i=0;i<d.count(0,LOCATION_GRAVE);++i) grave.push_back(d.code(0,LOCATION_GRAVE,i));
        std::vector<uint32_t> want;
        if(moved) want.push_back(dekoichi);
        if(!negation_case&&!calculation) want.push_back(modifier);
        std::sort(grave.begin(),grave.end()); std::sort(want.begin(),want.end());
        require(grave==want,"Unexpected defender/modifier Graveyard identities");
        if(calculation) require(d.query_value(1,LOCATION_MZONE,0,QUERY_ATTACK)==400,
                                "Lily's 3000 ATK gain must expire after calculation");
        std::cout << "Observed: modifier windows verified; "
                  << (negate?"activation negated, costs paid, no Flip draw."
                      :negation_case?"negation declined, no cost paid, Flip draw resolved."
                      :calculation?"Lily gained 3000 ATK during calculation and returned to 400."
                      :"ATK/DEF modification resolved before calculation; Flip draw resolved.") << '\n';
        return true;
      }
    } else if(e.kind==MSG_ATTACK) {
      require(requested && !attacked && e.from.controller==1 && e.from.location==LOCATION_MZONE &&
              e.from.sequence==0 && e.to.controller==0 && e.to.location==LOCATION_MZONE &&
              e.to.sequence==0,"Unexpected attack"); attacked=true;
    } else if(e.kind==MSG_DAMAGE_STEP_START) {
      require(attacked && !damage,"Unexpected Damage Step"); damage=true;
    } else if(e.kind==MSG_POS_CHANGE && e.from.code==dekoichi) {
      require(damage && !flipped && !battle && e.to.position==POS_FACEUP_DEFENSE,
              "Expected battle flip before calculation"); flipped=true;
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");
      const bool inside=damage&&!ended;
      const bool calc=inside&&d.phase()==PHASE_DAMAGE_CAL;
      if(inside && e.player==0 && battle) late_prompt=true;
      if(calc && e.player==0) calc_prompt=true;
      int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        const auto& c=e.cards[i];
        if(c.code==rush || c.code==walls || c.code==chalice) {
          require(e.player==0 && (!inside || (!calc&&!battle)),
                  "Ordinary modifier/effect negation offered during or after calculation");
          if(inside) early_offers[c.code==rush?0:c.code==walls?1:2]=true;
          bool window=mode==DamageException::Rush?!flipped:flipped;
          if(!negation_case&&!calculation && inside && window && !chosen &&
             c.code==modifier && c.sequence==(mode==DamageException::Rush?0:1)) pick=int(i);
        } else if(c.code==lily) {
          require(calculation && e.player==1 && calc && !battle && !chosen,
                  "Lily must be offered only during calculation");
          pick=int(i);
        } else if(c.code==negator && negation_case) {
          require(e.player==1 && inside && battle && confirmed==1 && announced==1 &&
                  chains.at(chain_no)[0]==dekoichi,"Negation must respond to Flip CL1 after calculation");
          negator_seen=true;
          if(negate) { require(!chosen,"Repeated negation choice"); pick=int(i); }
        } else throw std::runtime_error("Unexpected exception-fixture effect");
      }
      if(pick>=0) chosen=true;
      d.respond(pick);
    } else if(e.kind==MSG_SELECT_CARD) {
      require(e.minimum==1 && e.maximum==1,"Unexpected target/cost count");
      uint32_t want=dekoichi;
      int player=1, location=LOCATION_MZONE;
      if(damage) {
        require(announced>confirmed,"Selection outside announced activation");
        auto code=chains.at(chain_no).at(announced-1);
        if(code==rush || code==walls) { want=code==rush?attacker:dekoichi; player=0; }
        else {
          require(negate && code==negator,"Unexpected cost source");
          want=orange?watapon:blue_eyes; location=LOCATION_HAND;
        }
      }
      require(e.player==player,"Wrong selection player");
      int index=-1;
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want && e.cards[i].location==location) index=int(i);
      require(index>=0,"Expected target/cost missing");
      d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_CHAINING) {
      require(damage && !ended && announced==confirmed && chain_no<int(chains.size()) &&
              announced<int(chains[chain_no].size()) && e.chain==announced+1 &&
              e.cards.at(0).code==chains[chain_no][announced],"Unexpected chain formation");
      uint32_t code=e.cards[0].code;
      require(e.player==(code==lily || code==negator?1:0),"Wrong chain controller");
      require(code==dekoichi?battle&&flipped:code==negator?battle&&chosen:!battle&&chosen,
              "Exception activated in wrong window");
      ++announced;
    } else if(e.kind==MSG_CHAINED) {
      require(announced==confirmed+1 && e.chain==announced,"Unexpected confirmation");
      ++confirmed;
      if(negate && confirmed==2) require(costs==(orange?2:1),"Negation cost must precede CL2 confirmation");
      if(calculation && chain_no==0) require(lp_cost,"Lily cost must precede confirmation");
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(!resolving && confirmed==int(chains.at(chain_no).size()) && e.chain==confirmed-solved,
              "Incorrect reverse resolution"); resolving=e.chain;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving==e.chain && !(negated && e.chain==1),"Unexpected solved link"); resolving=0; ++solved;
    } else if(e.kind==MSG_CHAIN_NEGATED) {
      require(negate && resolving==2 && !negated && e.chain==1,"Expected activation negation of Flip CL1");
      negated=true;
    } else if(e.kind==MSG_CHAIN_DISABLED) {
      throw std::runtime_error("Expected activation negation, not effect-only disabling");
    } else if(e.kind==MSG_CHAIN_END) {
      // The legacy protocol emits SOLVING but no SOLVED for an
      // activation-negated link. Require that exact skip, not a fake resolution.
      if(negated) require(resolving==1 && solved==1 && confirmed==2,
                          "Negated CL1 must be skipped after CL2 resolves");
      else require(!resolving && solved==confirmed,"Incomplete chain resolution");
      require(confirmed==int(chains.at(chain_no).size()),"Incomplete chain formation");
      ++chain_no; announced=confirmed=solved=resolving=0;
    } else if(e.kind==MSG_PAY_LPCOST) {
      require(calculation && !lp_cost && e.player==1 && e.mask==2000 && announced==1,
              "Expected Lily's 2000 LP activation cost"); lp_cost=true;
    } else if(e.kind==MSG_BATTLE) {
      require(damage && flipped && !battle && (negation_case || chain_no==1) &&
              e.battle_stats[0]==(negation_case?200u:calculation?3400u:mode==DamageException::Rush?3700u:3000u) &&
              e.battle_stats[3]==(mode==DamageException::Walls?1500u:1000u) &&
              !e.battle_destroyed[0] && e.battle_destroyed[1]==!negation_case,
              "Modifier did not produce the expected battle calculation"); battle=true;
    } else if(e.kind==MSG_DAMAGE) {
      require(negation_case && battle && !battle_damage && e.player==1 && e.mask==800,
              "Expected 800 damage from Watapon attacking Dekoichi's DEF"); battle_damage=true;
    } else if(e.kind==MSG_DRAW) {
      require(!negate && resolving && active_code()==dekoichi && !drawn && e.player==0 &&
              e.count==1 && (e.cards.at(0).code&0x7fffffffu)==blue_eyes,
              "Unexpected or negated Flip draw"); drawn=true;
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==dekoichi && e.to.location==LOCATION_GRAVE) {
        require(damage && battle && !ended && !moved && e.from.location==LOCATION_MZONE &&
                e.to.controller==0 && (e.reason&REASON_DESTROY),"Unexpected defender destruction");
        if(negate) require(negated && resolving==2 && (e.reason&REASON_EFFECT) &&
                           !(e.reason&REASON_BATTLE),"Negator must destroy Dekoichi by effect");
        else require(!negation_case && drawn && (e.reason&REASON_BATTLE) &&
                     !(e.reason&REASON_EFFECT),"Expected battle destruction after Flip resolution");
        moved=true;
      } else if(e.from.location==LOCATION_HAND && e.to.location==LOCATION_GRAVE) {
        require(negate && e.from.controller==1 && e.to.controller==1 && announced==2 && confirmed==1 &&
                (e.reason&REASON_COST) && !(e.reason&REASON_EFFECT) &&
                (orange?(e.from.code==herald || e.from.code==watapon):e.from.code==blue_eyes),
                "Expected negation payment as cost, not effect");
        require(orange?!(e.reason&REASON_DISCARD):bool(e.reason&REASON_DISCARD),
                "Herald sends; Divine Wrath discards"); ++costs;
      }
    } else if(e.kind==MSG_DAMAGE_STEP_END) {
      require(damage && !ended && chain_no==int(chains.size()),"Damage Step ended too early"); ended=true;
    } else if(e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_SELECT_PLACE ||
              e.kind==MSG_SELECT_TRIBUTE || e.kind==MSG_SELECT_POSITION) {
      throw std::runtime_error("Unexpected exception-fixture prompt");
    }
    return false;
  });
}
