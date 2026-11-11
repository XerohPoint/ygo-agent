#include "harness.h"
#include "decision_context_tests.h"
#include "ygoenv/ygopro/observation_visibility.h"
#include "trigger_tests.h"
#include "optional_trigger_tests.h"
#include "damage_step_tests.h"
#include "damage_exception_tests.h"
#include "calculation_negation_tests.h"
#include "continuous_negation_tests.h"
#include "resolution_negation_tests.h"
#include "cost_departure_tests.h"
#include "persistent_negation_tests.h"
#include "face_down_reset_tests.h"
#include "leave_return_tests.h"
#include "private_trigger_tests.h"
#include "cross_player_trigger_tests.h"
#include "sangan_search_tests.h"
#include "sangan_repeat_tests.h"
#include "missed_timing_tests.h"
#include "nested_trigger_tests.h"
#include "simultaneous_battle_tests.h"
#include "target_identity_tests.h"
#include "trigger_location_tests.h"
#include "graveyard_negation_tests.h"
#include "return_cost_tests.h"
#include "banish_cost_tests.h"
#include "historical_damage_tests.h"
#include "calculation_chain_tests.h"
#include "revival_negation_tests.h"
#include "layered_negation_tests.h"
#include "cross_link_trigger_tests.h"
#include "brain_target_tests.h"
#include "goyo_material_tests.h"
#include "deck_size_tests.h"
#include "deck_copy_tests.h"
#include "deck_pool_tests.h"
#include "deck_file_tests.h"
#include "control_reset_tests.h"
#include "summon_location_tests.h"
#include "delayed_return_tests.h"
#include "turtle_repeat_tests.h"
#include "brionac_tests.h"
#include "necrovalley_tests.h"
#include "phase_retry_tests.h"
#include "darkness_position_tests.h"
#include "ryko_target_tests.h"
#include "quickdraw_tests.h"
#include "mandatory_phase_tests.h"

#include "prisma_tests.h"

#include "rescue_cat_tests.h"

#include "strike_ninja_tests.h"

#include "redmd_tests.h"

#include "fortune_light_tests.h"

#include "future_fusion_tests.h"

#include "future_fusion_lock_tests.h"

#include "end_dragon_tests.h"

#include "faceup_attack_tests.h"

#include "armory_tests.h"

#include "union_tests.h"

#include "black_garden_tests.h"

#include "mark_rose_tests.h"

#include "dragoon_tests.h"

#include "soul_exchange_tests.h"

#include "my_body_oppression_tests.h"

#include "my_body_damage_tests.h"

#include "summon_window_tests.h"

#include "urgent_tuning_tests.h"

#include "set_window_tests.h"

#include "swap_limit_tests.h"

#include "ladd_sequence_tests.h"

#include "treeborn_equip_tests.h"

#include "script_consistency_tests.h"

#include "swap_after_extra_tests.h"

#include "panther_copy_tests.h"

#include "gold_sarcophagus_tests.h"

using namespace certification;

void first_turn_draw(Assets& assets, int options, bool expected_draw, bool trace) {
  TestDuel d(assets, ygopro::duel_config::opening_hand, ygopro::duel_config::draw_count, trace,options);
  for (int p = 0; p < 2; ++p)
    for (int i = 0; i < 12; ++i)
      d.add({blue_eyes, uint8_t(p), uint8_t(p), LOCATION_DECK, 0, POS_FACEDOWN_DEFENSE});
  require(d.lp(0) == 8000 && d.lp(1) == 8000, "Starting LP differs from 8000");
  int opening[2] = {0, 0};
  int turns = 0, phase = 0, turn_draws = 0;
  d.start(options);
  d.run([&](const Event& e) {
    if (e.kind == MSG_NEW_TURN) {
      require(++turns == 1 && e.player == 0, "Expected P0's first turn");
      require(opening[0] == 5 && opening[1] == 5, "Opening draw events must be 5 each");
      require(d.count(0, LOCATION_HAND) == 5 && d.count(1, LOCATION_HAND) == 5,
              "Opening hand zones must contain 5 each");
    } else if (e.kind == MSG_NEW_PHASE) phase = e.phase;
    else if (e.kind == MSG_DRAW) {
      require(e.player == 0 || e.player == 1, "Invalid drawing player");
      if (!turns) opening[e.player] += e.count;
      else {
        require(turns == 1 && e.player == 0 && phase == PHASE_DRAW && e.count == 1,
                "Expected exactly one P0 draw in first Draw Phase");
        turn_draws += e.count;
      }
    } else if (e.kind == MSG_SELECT_CHAIN) {
      require(e.cards.empty() && !e.forced, "Unexpected effect in draw fixture");
      d.respond(-1);
    } else if (e.kind == MSG_SELECT_IDLECMD) {
      require(e.player == 0 && turns == 1 && phase == PHASE_MAIN1, "Expected first Main Phase 1");
      require(turn_draws == int(expected_draw), "First-turn draw count mismatch: observed " + std::to_string(turn_draws));
      require(d.count(0, LOCATION_HAND) == 5 + int(expected_draw) && d.count(1, LOCATION_HAND) == 5,
              "Hand zone counts differ from draw events");
      require(d.count(0, LOCATION_DECK) == 7 - int(expected_draw), "Deck count mismatch");
      require(d.lp(0) == 8000 && d.lp(1) == 8000, "Unexpected LP change");
      std::cout << "Observed: opening hands 5/5, LP 8000/8000; P0 turn-1 draws "
                << turn_draws << "; hands at Main Phase 1 " << d.count(0, LOCATION_HAND) << "/5.\n";
      return true;
    }
    return false;
  });
}

void ignition_priority(Assets& assets, int options, bool expected_priority, bool trace) {
  // Exact state: no automatic draws, one hand card, one cost monster, and an
  // opponent trap treated as Set on a previous turn. No test-mode rule flags.
  TestDuel d(assets, 0, 0, trace,options);
  d.add({bazoo, 0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes, 0, 0, LOCATION_GRAVE, 0, POS_FACEUP_ATTACK});
  d.add({bottomless, 1, 1, LOCATION_SZONE, 0, POS_FACEDOWN_DEFENSE});
  bool summon_requested = false, summoned = false, ignition_offered = false, paid = false;
  bool trap_offered = false;
  int chains = 0, confirmed = 0;
  std::vector<int> resolving;
  d.start(options);
  d.run([&](const Event& e) {
    if (e.kind == MSG_SELECT_IDLECMD) {
      require(!summon_requested && e.player == 0, "Returned to idle before expected chain completed");
      require(e.idle[0].size() == 1 && e.idle[0][0].code == bazoo, "Bazoo Normal Summon not offered");
      summon_requested = true;
      d.respond(0); // index 0, Normal Summon
    } else if (e.kind == MSG_SELECT_PLACE) {
      require(summon_requested && !summoned && e.player == 0 && e.count == 1 && !(e.mask & 1),
              "Expected free P0 monster zone 0 for Normal Summon");
      d.respond({0, LOCATION_MZONE, 0});
    } else if (e.kind == MSG_SUMMONING) {
      require(summon_requested && e.cards.at(0).code == bazoo, "Unexpected summon");
    } else if (e.kind == MSG_SUMMONED) {
      require(summon_requested && !summoned, "Unexpected summon-success event");
      summoned = true;
      require(d.code(0, LOCATION_MZONE, 0) == bazoo, "Bazoo absent after Summon");
      std::cout << "Observed: P0 successfully Normal Summons Bazoo (1600 ATK).\n";
    } else if (e.kind == MSG_SELECT_CHAIN) {
      require(!e.forced, "Unexpected forced chain");
      if (e.cards.empty()) { d.respond(-1); return false; }
      require(summoned && e.cards.size() == 1, "Unexpected chain candidates");
      auto c = e.cards[0];
      if (c.code == bazoo) {
        require(expected_priority && e.player == 0 && c.controller == 0 && c.location == LOCATION_MZONE &&
                !ignition_offered && !trap_offered && chains == 0,
                "Ignition was not offered before the opponent's Summon response");
        ignition_offered = true;
        std::cout << "Observed: P0 offered Bazoo ignition before P1 response; accepted.\n";
        d.respond(0);
      } else {
        require(c.code == bottomless && e.player == 1 && c.controller == 1 && c.location == LOCATION_SZONE &&
                !trap_offered && chains == int(expected_priority) && confirmed == chains,
                "Bottomless offered at wrong chain position");
        require(!expected_priority || (ignition_offered && paid), "Opponent responded before ignition cost");
        trap_offered = true;
        std::cout << "Observed: P1 offered Bottomless; accepted.\n";
        d.respond(0);
      }
    } else if (e.kind == MSG_SELECT_CARD) {
      require(expected_priority && ignition_offered && !paid && !trap_offered && e.player == 0 &&
              e.minimum == 1 && e.maximum >= 1 && e.cards.size() == 1 &&
              e.cards[0].code == blue_eyes && e.cards[0].controller == 0 && e.cards[0].location == LOCATION_GRAVE,
              "Expected exactly the Graveyard monster as Bazoo's cost");
      paid = true;
      d.respond({1, 0});
    } else if (e.kind == MSG_CHAINING) {
      ++chains;
      auto expected_code = expected_priority && chains == 1 ? bazoo : bottomless;
      require(e.chain == chains && e.cards.at(0).code == expected_code &&
              e.player == (expected_code == bazoo ? 0 : 1), "Wrong chain link/card/player");
    } else if (e.kind == MSG_CHAINED) {
      require(e.chain == ++confirmed && confirmed == chains, "Chain link not confirmed by core");
      auto expected_code = expected_priority && confirmed == 1 ? bazoo : bottomless;
      // MSG_CHAINING announces activation before Lua costs run. MSG_CHAINED
      // confirms the link after the cost has been paid and before responses.
      if (expected_code == bazoo) {
        require(paid && d.count(0, LOCATION_GRAVE) == 0 && d.code(0, LOCATION_REMOVED, 0) == blue_eyes,
                "Bazoo cost was not actually paid before CL1");
      }
      std::cout << "Observed: CL" << chains << " = " << (expected_code == bazoo ? "Bazoo" : "Bottomless Trap Hole") << ".\n";
    } else if (e.kind == MSG_CHAIN_SOLVING) resolving.push_back(e.chain);
    else if (e.kind == MSG_CHAIN_END) {
      require(summoned && trap_offered && chains == (expected_priority ? 2 : 1) && confirmed == chains,
              "Missing expected chain links");
      require(resolving == (expected_priority ? std::vector<int>{2, 1} : std::vector<int>{1}), "Wrong resolution order");
      require(d.count(0, LOCATION_MZONE) == 0 && d.count(1, LOCATION_SZONE) == 0,
              "Expected Bazoo and Bottomless to leave the field");
      require(d.count(0, LOCATION_REMOVED) == (expected_priority ? 2 : 1) &&
              d.code(0, LOCATION_REMOVED, expected_priority ? 1 : 0) == bazoo &&
              d.code(1, LOCATION_GRAVE, 0) == bottomless, "Wrong resolved card zones");
      require(d.count(0, LOCATION_GRAVE) == (expected_priority ? 0 : 1), "Wrong cost state after resolution");
      std::cout << "Observed: chain resolves; Bazoo banished, Bottomless in P1 Graveyard.\n";
      return true;
    }
    return false;
  });
}

void field_spell_replacement(Assets& assets, int options, bool historical,
                             uint8_t old_player, bool setting, bool trace) {
  TestDuel d(assets, 0, 0, trace,options);
  // The C API represents the Field Zone as SZONE sequence 5, not LOCATION_FZONE
  // (a Lua selector too wide for the C API's uint8 location parameter).
  constexpr uint8_t field_zone = 5;
  d.add({yami, old_player, old_player, LOCATION_SZONE, field_zone, POS_FACEUP_ATTACK});
  d.add({forest, 0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE});
  const bool own = old_player == 0;
  const bool removed = own || (historical && !setting);
  bool requested = false, placed = false, old_moved = false, set_seen = false, zone_selected = false;
  bool announced = false, confirmed = false, resolving = false, solved = false, ended = false;
  auto check_old = [&] {
    require(d.code(old_player, LOCATION_SZONE, field_zone) == yami &&
            (d.position(old_player, LOCATION_SZONE, field_zone) & POS_FACEUP),
            "Original Yami must still be face-up in its Field Zone");
  };
  check_old();
  d.start(options);
  d.run([&](const Event& e) {
    if (e.kind == MSG_SELECT_IDLECMD) {
      require(e.player == 0, "Expected P0's Main Phase action");
      if (!requested) {
        check_old();
        const auto& choices = e.idle[setting ? 4 : 5];
        require(choices.size() == 1 && choices[0].code == forest &&
                choices[0].controller == 0 && choices[0].location == LOCATION_HAND,
                "Forest activation/Set action not offered from P0 hand");
        requested = true;
        d.respond(setting ? 4 : 5); // index 0 in Set/activate group
      } else {
        require(placed && (setting ? set_seen && !announced : ended && solved),
                "Returned to idle before the Field Spell action completed");
        require(old_moved == removed, "Wrong Field Spell replacement behavior");
        require(d.count(0, LOCATION_HAND) == 0 && d.code(0, LOCATION_SZONE, field_zone) == forest &&
                (d.position(0, LOCATION_SZONE, field_zone) & (setting ? POS_FACEDOWN : POS_FACEUP)),
                "Forest must occupy P0's Field Zone in the expected position");
        require(d.count(0, LOCATION_SZONE) == 1 &&
                d.count(1, LOCATION_SZONE) == int(!own && !removed), "Wrong final Field Zone occupancy");
        require(d.count(old_player, LOCATION_GRAVE) == int(removed) &&
                d.count(1 - old_player, LOCATION_GRAVE) == 0, "Wrong final Graveyard counts");
        if (removed) require(d.code(old_player, LOCATION_GRAVE, 0) == yami, "Yami missing from owner's Graveyard");
        else check_old();
        require(d.lp(0) == 8000 && d.lp(1) == 8000, "Unexpected LP change");
        std::cout << "Observed: P0 " << (setting ? "Sets" : "activates") << " Forest; "
                  << (own ? "own" : "opponent's") << " Yami "
                  << (removed ? (historical ? "destroyed by rule" : "sent to Graveyard without destruction")
                              : "remains face-up")
                  << (removed ? (own ? " before Forest placement" : " during Forest resolution") : "") << ".\n";
        return true;
      }
    } else if (e.kind == MSG_SELECT_CHAIN) {
      require(e.cards.empty() && !e.forced, "Unexpected chain effect in Field Spell fixture");
      d.respond(-1);
    } else if (e.kind == MSG_MOVE) {
      require(requested, "Unexpected card move before scripted action");
      if (e.from.code == yami) {
        require(removed && !old_moved && e.from.controller == old_player &&
                e.from.location == LOCATION_SZONE && e.from.sequence == field_zone &&
                (e.from.position & POS_FACEUP) && e.to.controller == old_player && e.to.location == LOCATION_GRAVE,
                "Unexpected movement of original Field Spell");
        require((e.reason & REASON_RULE) && bool(e.reason & REASON_DESTROY) == historical &&
                !(e.reason & REASON_EFFECT), "Wrong replacement reason: destruction vs rule send");
        require(own ? !placed && !announced : confirmed && resolving && !ended,
                "Original Field Spell removed at the wrong time");
        old_moved = true;
      } else {
        require(e.from.code == forest && !placed && e.from.controller == 0 && e.from.location == LOCATION_HAND &&
                e.to.controller == 0 && e.to.location == LOCATION_SZONE && e.to.sequence == field_zone &&
                (e.to.position & (setting ? POS_FACEDOWN : POS_FACEUP)), "Unexpected Forest placement");
        require(old_moved == own, "Own Field Spell must leave before placement; opponent's must remain");
        placed = true;
      }
    } else if (e.kind == MSG_SET) {
      const auto& c = e.cards.at(0);
      require(setting && placed && !set_seen && c.code == forest && c.controller == 0 &&
              c.location == LOCATION_SZONE && c.sequence == field_zone && (c.position & POS_FACEDOWN),
              "Unexpected Field Spell Set event");
      set_seen = true;
    } else if (e.kind == MSG_CHAINING) {
      const auto& c = e.cards.at(0);
      require(!setting && placed && !announced && e.chain == 1 && e.player == 0 &&
              c.code == forest && c.controller == 0 && c.location == LOCATION_SZONE && c.sequence == field_zone,
              "Expected only Forest as CL1");
      announced = true;
    } else if (e.kind == MSG_CHAINED) {
      require(announced && !confirmed && e.chain == 1 && old_moved == own, "Wrong activation confirmation timing");
      if (!own) check_old(); // opponent's old Field Spell survives activation
      confirmed = true;
    } else if (e.kind == MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && e.chain == 1, "Unexpected Field Spell resolution");
      resolving = true;
    } else if (e.kind == MSG_CHAIN_SOLVED) {
      require(resolving && !solved && e.chain == 1, "Unexpected resolved chain link");
      solved = true;
    } else if (e.kind == MSG_CHAIN_END) {
      require(solved && !ended && old_moved == removed, "Field Spell replacement incomplete at chain end");
      ended = true;
    } else if (e.kind == MSG_SELECT_PLACE) {
      // This core asks for the Set destination even with only the Field Zone
      // available. Its count=0 convention still consumes one zone response.
      require(setting && requested && !placed && !zone_selected && e.player == 0 &&
              (e.count == 0 || e.count == 1) && e.mask == ~(1u << (8 + field_zone)),
              "Expected only P0's Field Zone as the Set destination");
      zone_selected = true;
      d.respond({0, LOCATION_SZONE, field_zone});
    } else if (e.kind == MSG_SELECT_CARD) {
      throw std::runtime_error("Unexpected selection in fixed Field Zone fixture");
    }
    return false;
  });
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "Usage: edison_rules DATABASE SCRIPT_DIRECTORY [--trace] [--rule=5] [--test=NNN] [--fail-fast]\n";
    return 2;
  }
  bool trace = false, fail_fast = false;
  std::string test_filter;
  int options = ygopro::duel_config::options;
  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--trace") trace = true;
    else if (arg == "--fail-fast") fail_fast = true;
    else if (arg == "--rule=5") options = 5 << 16; // deliberate regression probe
    else if (arg.rfind("--test=", 0) == 0) {
      test_filter = arg.substr(7);
      if (test_filter.size() != 3 || test_filter.find_first_not_of("0123456789") != std::string::npos) {
        std::cerr << "Expected a three-digit test number.\n"; return 2;
      }
    }
    else { std::cerr << "Unknown argument: " << arg << '\n'; return 2; }
  }
  try {
    Assets assets(argv[1], argv[2]);
    int failures = 0, total = 0;
    auto test = [&](const std::string& label, const std::string& expected, auto body) {
      if (fail_fast && failures) return;
      if (!test_filter.empty() &&
          label.rfind("EDISON RULE TEST " + test_filter, 0) != 0 &&
          label.rfind("CONTROL " + test_filter, 0) != 0) return;
      ++total;
      std::cout << '\n' << label << "\nExpected: " << expected << '\n';
      try { body(); std::cout << "RESULT: PASS\n"; }
      catch (const std::exception& e) { ++failures; std::cout << "RESULT: FAIL — " << e.what() << '\n'; }
    };
    std::cout << "Seed: " << seed << "; Edison options: " << options << '\n';
    test("EDISON RULE TEST 001 — FIRST-TURN DRAW", "Opening hands 5/5; P0 draws 1 on turn 1, reaching 6.",
         [&] { first_turn_draw(assets, options, true, trace); });
    test("EDISON RULE TEST 002 — IGNITION PRIORITY", "Turn player's ignition may become CL1 before opponent response (Bottomless CL2).",
         [&] { ignition_priority(assets, options, true, trace); });
    test("CONTROL 001 — RULE 5 FIRST-TURN DRAW", "P0 does not draw on turn 1, staying at 5.",
         [&] { first_turn_draw(assets, 5 << 16, false, trace); });
    test("CONTROL 002 — RULE 5 SUMMON RESPONSE", "No Bazoo ignition prompt; Bottomless becomes CL1.",
         [&] { ignition_priority(assets, 5 << 16, false, trace); });
    for (bool historical : {true, false}) {
      int variant = 0;
      for (uint8_t old_player : {1, 0}) {
        for (bool setting : {false, true}) {
          const bool own = old_player == 0;
          std::string expectation = "P0 " + std::string(setting ? "Sets" : "activates") + " Forest; ";
          if (own)
            expectation += historical ? "own Yami destroyed before Forest placement."
                                      : "own Yami sent to Graveyard before placement, without destruction.";
          else
            expectation += historical && !setting ? "opponent's Yami destroyed only on resolution."
                                                  : "opponent's Yami remains face-up.";
          std::string label = historical ? "EDISON RULE TEST 003" : "CONTROL 003";
          label += char('A' + variant++);
          label += " — FIELD SPELL REPLACEMENT";
          test(label, expectation, [&] {
            field_spell_replacement(assets, historical ? options : 5 << 16,
                                    historical, old_player, setting, trace);
          });
        }
      }
    }
    test("EDISON RULE TEST 004A - SEGOC", "P0 Sangan, P1 Sangan, P0 Dupe Frog, P1 Dupe Frog.",
         [&] { trigger_ordering(assets, options, false, true, trace); });
    test("EDISON RULE TEST 004B - EARLY TRIGGER", "Tributed Sangan forced CL1; Caius CL2.",
         [&] { trigger_ordering(assets, options, true, true, trace); });
    test("CONTROL 004A - RULE 5 SEGOC", "Same four SEGOC groups under Rule 5.",
         [&] { trigger_ordering(assets, 5 << 16, false, false, trace); });
    test("CONTROL 004B - RULE 5 TRIGGER CHOICE", "Both mandatory effects offered; select Caius CL1, Sangan CL2.",
         [&] { trigger_ordering(assets, 5 << 16, true, false, trace); });
    test("EDISON RULE TEST 004C - SEGOC STEP OVER CHRONOLOGY", "Turn-player Caius CL1 (Step 1) before opponent-owned earlier Sangan CL2 (Step 2).",
         [&] { trigger_ordering(assets, options, true, true, trace, true); });
    test("CONTROL 004C - RULE 5 TURN PLAYER FIRST", "Turn-player Caius CL1 before opponent-owned Sangan CL2.",
         [&] { trigger_ordering(assets, 5 << 16, true, false, trace, true); });
    for(bool historical : {true, false}) {
      for(bool accept : {true, false}) {
        std::string label = historical ? "EDISON RULE TEST 005" : "CONTROL 005";
        label += accept ? "A - ACCEPT EARLIER OPTIONAL" : "B - DECLINE EARLIER OPTIONAL";
        std::string expectation = historical ? "Broww CL1 (Step 1) before Watapon decision (Step 3); " : "Rule 5: Broww CL1 before Watapon decision; ";
        expectation += accept ? "Watapon CL2; resolve both." : "decline Watapon, resolve only Broww CL1.";
        test(label, expectation, [&] {
          optional_early_trigger(assets, historical ? options : 5 << 16, accept, trace);
        });
      }
    }
    for(bool historical : {true, false}) {
      for(bool accept : {true, false}) {
        std::string label=historical?"EDISON RULE TEST 006":"CONTROL 006";
        label += accept?"A - DELAYED ACROSS LINKS, ACCEPT":"B - DELAYED ACROSS LINKS, DECLINE";
        test(label, historical ? "Jar CL2 draws Watapon; Dealings CL1 discards Broww; after chain end Broww CL1 (Step 1) before optional Watapon (Step 3)."
                               : "Same resolved setup; Rule 5 confirms Broww before optional Watapon decision.", [&] {
          optional_early_trigger(assets,historical?options:5<<16,accept,trace,true);
        });
      }
      for(int variant=0;variant<3;++variant) {
        std::string label=historical?"EDISON RULE TEST 007":"CONTROL 007";
        label += char('A'+variant);
        label += " - DAMAGE STEP";
        test(label, variant==0 ? "Battle flip, calculation, Dekoichi draw, then battle destruction."
                              : variant==1 ? "1600 damage, Tomato to Graveyard, accept recruiter and summon Sangan."
                                           : "1600 damage, Tomato to Graveyard, decline recruiter; no trigger chain.", [&] {
          damage_step_triggers(assets,historical?options:5<<16,variant!=0,variant!=2,trace);
        });
      }
    }
    const std::array<std::string,6> exceptions = {
      "Rush Recklessly at Damage Step start: attacker gains 700 ATK.",
      "Castle Walls after battle flip, before calculation: defender gains 500 DEF.",
      "Lily during calculation: pay 2000 LP, gain 3000 ATK; ordinary modifiers excluded.",
      "Divine Wrath counters the Flip activation after calculation; discard cost, no draw.",
      "Herald negates the Flip activation after calculation; send two Fairies as cost, no draw.",
      "Decline Divine Wrath after calculation; no cost, Flip draw resolves."};
    for(bool historical : {true,false}) {
      for(int variant=0;variant<6;++variant) {
        std::string label=historical?"EDISON RULE TEST ":"CONTROL ";
        label += variant<3?"008":"009";
        label += char('A'+variant%3);
        label += historical?" - DAMAGE STEP EXCEPTION":" - RULE 5 DAMAGE STEP EXCEPTION";
        test(label,exceptions[variant],[&] {
          damage_step_exception(assets,historical?options:5<<16,static_cast<DamageException>(variant),trace);
        });
      }
    }
    for(bool historical : {true,false}) {
      int variant=0;
      for(bool orange : {false,true}) {
        for(bool accept : {true,false}) {
          std::string label=historical?"EDISON RULE TEST 010":"CONTROL 010";
          label += char('A'+variant++);
          label += historical?" - CALCULATION NEGATION":" - RULE 5 CALCULATION NEGATION";
          std::string expected=orange?"Herald":"Divine Wrath";
          expected += accept?" negates Lily during calculation; 2000 LP remains paid; no battle damage."
                            :" offered and declined during calculation; Lily deals 400 battle damage.";
          test(label,expected,[&] {
            calculation_negation(assets,historical?options:5<<16,orange,accept,trace);
          });
        }
      }
    }
    const std::array<std::string,5> continuous = {
      "Active Skill Drain: Lily may activate/pay 2000 LP, but resolves disabled at 400 ATK.",
      "Leave Skill Drain Set: unavailable during Damage Step; Lily gains 3000 ATK normally.",
      "Activate then destroy Skill Drain before battle: Lily regains normal resolution.",
      "Active Skill Drain: battle Flip effect activates and resolves disabled; no Dekoichi draw.",
      "Active Skill Drain: Mystic Tomato's Graveyard trigger still recruits Sangan."};
    for(bool historical : {true,false}) {
      for(int variant=0;variant<5;++variant) {
        std::string label=historical?"EDISON RULE TEST 011":"CONTROL 011";
        label += char('A'+variant);
        label += historical?" - CONTINUOUS NEGATION":" - RULE 5 CONTINUOUS NEGATION";
        test(label,continuous[variant],[&] {
          continuous_negation(assets,historical?options:5<<16,static_cast<ContinuousCase>(variant),trace);
        });
      }
    }
    const std::array<std::string,5> resolution = {
      "Decline Book of Moon: face-up Dekoichi resolves disabled under active Skill Drain.",
      "Book of Moon CL2: face-down Dekoichi draws at CL1 while Skill Drain remains active.",
      "Compulsory CL2: Dekoichi returns to hand, then its field effect draws at CL1.",
      "MST CL2: destroy active Skill Drain; face-up Dekoichi draws at CL1.",
      "Skill Drain CL2: pay 1000 LP and resolve; Dekoichi CL1 resolves disabled."};
    for(bool historical : {true,false}) {
      for(int variant=0;variant<5;++variant) {
        std::string label=historical?"EDISON RULE TEST 012":"CONTROL 012";
        label += char('A'+variant);
        label += historical?" - NEGATION AT RESOLUTION":" - RULE 5 NEGATION AT RESOLUTION";
        test(label,resolution[variant],[&] {
          resolution_negation(assets,historical?options:5<<16,static_cast<ResolutionCase>(variant),trace);
        });
      }
    }
    for(bool historical : {true,false}) {
      int variant=0;
      for(bool active : {true,false}) {
        for(int kind=0;kind<3;++kind) {
          std::string label=historical?"EDISON RULE TEST 013":"CONTROL 013";
          label += char('A'+variant++);
          label += historical?" - LEAVING AS COST":" - RULE 5 LEAVING AS COST";
          std::string expected=active?"Active Skill Drain: ":"Leave Skill Drain Set: ";
          expected += kind==0?"Lonefire tributes itself before responses and recruits a Plant."
                      :kind==1?(active?"Lonefire tributes another Plant; effect disabled, cost stays paid."
                                     :"Lonefire tributes another Plant and recruits normally.")
                              :"Exiled Force tributes itself before targeting and destroys Blue-Eyes.";
          test(label,expected,[&] {
            cost_departure(assets,historical?options:5<<16,static_cast<CostDeparture>(kind),active,trace);
          });
        }
      }
    }
    for(bool historical : {true,false}) {
      int variant=0;
      for(int mode=0;mode<3;++mode) {
        for(bool exiled : {false,true}) {
          std::string label=historical?"EDISON RULE TEST 014":"CONTROL 014";
          label += char('A'+variant++);
          label += historical?" - PERSISTENT NEGATION":" - RULE 5 PERSISTENT NEGATION";
          std::string expected=mode==0?"Apply Chalice first: ":mode==1?"Active Skill Drain: ":"Leave Chalice Set: ";
          expected += exiled?"Exiled Force":"Lonefire";
          expected += mode==0?" self-tributes but resolves disabled; cost retained."
                             :" self-tributes and resolves normally.";
          test(label,expected,[&] {
            persistent_negation(assets,historical?options:5<<16,
                                static_cast<PersistentNegation>(mode),exiled,trace);
          });
        }
      }
    }
    const std::array<std::string,5> resets = {
      "Dekoichi CL1, Book CL2, Chalice CL3: applied negation resets face-down; draw.",
      "Dekoichi CL1, Chalice CL2; decline Book: effect stays negated, no draw.",
      "Dekoichi CL1, Chalice CL2, Book CL3: face-down target prevents Chalice application; draw.",
      "Apply Chalice, resolve Book, then Flip Summon: base ATK and unnegated draw in same Main Phase.",
      "Activate Skill Drain, resolve Book, then Flip Summon: continuous negation reapplies; no draw."};
    for(bool historical : {true,false}) {
      for(int variant=0;variant<5;++variant) {
        std::string label=historical?"EDISON RULE TEST 015":"CONTROL 015";
        label += char('A'+variant);
        label += historical?" - FACE-DOWN NEGATION RESET":" - RULE 5 FACE-DOWN NEGATION RESET";
        test(label,resets[variant],[&] {
          face_down_reset(assets,historical?options:5<<16,static_cast<FaceDownReset>(variant),trace);
        });
      }
    }
    for(bool historical : {true,false}) {
      int variant=0;
      for(int mode=0;mode<3;++mode) {
        for(bool exiled : {false,true}) {
          std::string label=historical?"EDISON RULE TEST 016":"CONTROL 016";
          label += char('A'+variant++);
          label += historical?" - LEAVE AND RETURN":" - RULE 5 LEAVE AND RETURN";
          std::string expected=mode==0?"Applied Chalice: ":mode==1?"Active Skill Drain: ":"Unused Chalice: ";
          expected += exiled?"Exiled Force":"Lonefire";
          expected += " self-tributes for CL1, Call CL2 revives it before CL1; ";
          expected += mode==0?"original effect remains negated.":"original effect resolves normally.";
          test(label,expected,[&] {
            leave_and_return(assets,historical?options:5<<16,static_cast<ReturnNegation>(mode),exiled,trace);
          });
        }
      }
    }
    // Establish the comparison cases before the Edison hidden-hand cases.
    for(bool across : {false,true}) {
      for(bool accept : {true,false}) {
        std::string label="CONTROL 017";
        label += char('A'+2*int(across)+int(!accept));
        label += " - RULE 5 PRIVATE HAND";
        test(label,"Unrevealed Watapon decision after Broww CL1; resolve accepted/declined outcome.",[&] {
          private_early_trigger(assets,5<<16,accept,trace,across);
        });
      }
    }
    for(bool accept : {true,false}) {
      std::string label=accept?"CONTROL 017E":"CONTROL 017F";
      label += " - EDISON PUBLIC HAND";
      test(label,"Bell reveals Watapon; Broww CL1 (Step 1) before optional decision (Step 3).",[&] {
        optional_early_trigger(assets,options,accept,trace);
      });
    }
    for(bool across : {false,true}) {
      for(bool accept : {true,false}) {
        std::string label="EDISON RULE TEST 017";
        label += char('A'+2*int(across)+int(!accept));
        label += " - PRIVATE HAND SEGOC STEP ORDER";
        test(label,"Broww CL1 (Step 1) precedes private Watapon's optional decision (Step 3), offered once.",[&] {
          private_early_trigger(assets,options,accept,trace,across);
        });
      }
    }
    for(bool historical : {false,true}) {
      for(uint8_t op : {0,1}) {
        for(bool accept : {true,false}) {
          std::string label=historical?"EDISON RULE TEST 018":"CONTROL 018";
          label += char('A'+2*op+int(!accept));
          label += historical?" - CROSS-PLAYER SEGOC STEP ORDER":" - CROSS-PLAYER EARLY TRIGGER";
          test(label,historical?"Mandatory Broww before the opponent's private optional Watapon decision, despite Watapon's earlier event."
                               :"Rule 5: mandatory Broww before opponent's private Watapon decision.",[&] {
            cross_player_early_trigger(assets,historical?options:5<<16,accept,op,trace);
          });
        }
      }
    }
    test("CONTROL 019A - RULE 5 SANGAN SEARCH RESTRICTION",
         "Sangan searches Lonefire; Normal Summon allowed, same-turn ignition activation forbidden.",[&] {
      sangan_search_restriction(assets,5<<16,false,true,trace);
    });
    test("CONTROL 019B - RULE 5 DIFFERENT SEARCHED NAME",
         "Sangan searches Dupe; already-held Lonefire summons, activates, self-tributes, and recruits.",[&] {
      sangan_search_restriction(assets,5<<16,false,false,trace);
    });
    test("CONTROL 019C - EDISON DIFFERENT SEARCHED NAME",
         "Sangan searches Dupe; already-held Lonefire summons, activates, self-tributes, and recruits.",[&] {
      sangan_search_restriction(assets,options,true,false,trace);
    });
    test("EDISON RULE TEST 019A - HISTORICAL SANGAN SEARCH",
         "Sangan searches Lonefire; same-turn Normal Summon and ignition activation remain legal.",[&] {
      sangan_search_restriction(assets,options,true,true,trace);
    });
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 020A - SANGAN REPEATED SEARCH":"CONTROL 020A - RULE 5 SANGAN ONCE PER TURN",
           historical?"Destroy Sangan, search, revive, destroy again: two searches in one turn."
                     :"Same physical Sangan leaves twice; modern limit allows only the first search.",[&] {
        sangan_repeat(assets,historical?options:5<<16,historical,trace);
      });
    }
    for(bool historical : {false,true}) {
      for(int mode=0;mode<3;++mode) {
        std::string label=historical?"EDISON RULE TEST 021":"CONTROL 021";
        label+=char('A'+mode);label+=" - OPTIONAL WHEN MISSED TIMING";
        test(label,mode==0?"Dupe tributed for Caius: no optional search."
                         :mode==1?"Dupe destroyed last: accept and search Treeborn."
                                 :"Dupe destroyed last: decline; Treeborn stays in Deck.",[&] {
          missed_timing(assets,historical?options:5<<16,mode,trace);
        });
      }
    }
    for(bool historical : {false,true}) {
      for(bool accept : {true,false}) {
        std::string label=historical?"EDISON RULE TEST 022":"CONTROL 022";
        label+=accept?'A':'B';label+=" - TRIGGER DURING TRIGGER RESOLUTION";
        test(label,"Dekoichi draws Watapon; its optional trigger waits for a new chain.",[&] {
          nested_trigger(assets,historical?options:5<<16,accept,trace);
        });
      }
    }
    for(bool historical : {false,true}) {
      for(int variant=0;variant<5;++variant) {
        std::string label=historical?"EDISON RULE TEST 023":"CONTROL 023";
        label+=char('A'+variant);label+=" - SIMULTANEOUS BATTLE TRIGGERS";
        test(label,variant==0?"Two Sangan destroyed together: turn player's mandatory search first."
                             :"Two Tomato destroyed together: turn-player optional choice first, all accept/decline combinations.",[&] {
          simultaneous_battle(assets,historical?options:5<<16,variant!=0,variant-1,trace);
        });
      }
    }
    for(bool historical : {false,true}) {
      for(int mode=0;mode<3;++mode) {
        std::string label=historical?"EDISON RULE TEST 024":"CONTROL 024";
        label+=char('A'+mode);label+=" - TARGET INSTANCE AFTER RETURN";
        test(label,mode==0?"Chalice targets Blue-Eyes; no departure, +400 ATK."
                         :mode==1?"Enemy Controller tributes the target; Chalice cannot affect it in the Graveyard."
                                 :"Enemy Controller tributes target, Call revives it before Chalice; returned instance stays at base ATK.",[&] {
          target_identity(assets,historical?options:5<<16,mode,trace);
        });
      }
    }
    for(bool historical : {false,true}) {
      for(bool crow : {false,true}) {
        std::string label=historical?"EDISON RULE TEST 025":"CONTROL 025";
        label+=crow?'B':'A';label+=" - TRIGGER LOCATION AFTER COST";
        test(label,crow?(historical?"Tribute Sangan, Crow banishes it before trigger window; Edison still searches."
                                 :"Rule 5: Sangan leaves its trigger location before activation; no search.")
                      :"Decline Crow; tributed Sangan stays in Graveyard and searches.",[&] {
          trigger_location(assets,historical?options:5<<16,historical,crow,trace);
        });
      }
    }
    for(bool historical : {false,true}) {
      for(int mode=0;mode<4;++mode) {
        std::string label=historical?"EDISON RULE TEST 026":"CONTROL 026";
        label+=char('A'+mode);label+=" - NEW GRAVEYARD EFFECT AFTER FIELD NEGATION";
        test(label,"Apply Chalice, Skill Drain, both, or neither; destroy Sangan and resolve its new Graveyard search.",[&] {
          graveyard_negation(assets,historical?options:5<<16,mode,trace);
        });
      }
    }
    for(bool historical : {false,true}) {
      for(int mode=0;mode<3;++mode) for(bool self : {true,false}) {
        std::string label=historical?"EDISON RULE TEST 027":"CONTROL 027";
        label+=char('A'+mode*2+int(!self));label+=" - RETURN TO HAND AS COST";
        test(label,"Spend ordinary Normal Summon, return self/other for Swap's cost, verify additional Frog summon under Chalice/Drain/neither.",[&] {
          return_cost_negation(assets,historical?options:5<<16,mode,self,trace);
        });
      }
    }
    for(bool historical : {false,true}) {
      for(int mode=0;mode<4;++mode) {
        std::string label=historical?"EDISON RULE TEST 028":"CONTROL 028";
        label+=char('A'+mode);label+=" - BANISH SELF AS COST";
        test(label,"Cyber Valley attack trigger under Chalice/Drain/neither; also decline and take battle damage.",[&] {
          banish_cost_negation(assets,historical?options:5<<16,mode,trace);
        });
      }
    }
    for(bool historical : {false,true}) {
      for(bool blackwing : {false,true}) for(int mode=0;mode<3;++mode) {
        std::string label=historical?"EDISON RULE TEST 029":"CONTROL 029";
        label+=char('A'+int(blackwing)*3+mode);label+=" - HISTORICAL HAND MODIFIER WINDOW";
        test(label,"Honest/Kalut: before-calculation accept, calculation accept, or decline; historical late window only.",[&] {
          historical_damage_modifier(assets,historical?options:5<<16,historical,blackwing,mode,trace);
        });
      }
    }
    test("CONTROL 030A - RULE 5 CALCULATION EXCLUSION","Two Honest remain in hand when waiting for calculation.",[&] {
      single_calculation_chain(assets,5<<16,false,false,false,trace);
    });
    for(bool both : {false,true}) {
      test(both?"CONTROL 030C - RULE 5 EARLY TWO LINKS":"CONTROL 030B - RULE 5 EARLY ONE LINK",
           "Before calculation, activate Honest and accept/decline the second copy's chain response.",[&] {
        single_calculation_chain(assets,5<<16,false,true,both,trace);
      });
      test(both?"EDISON RULE TEST 030B - CALCULATION TWO LINKS":"EDISON RULE TEST 030A - CALCULATION ONE CHAIN",
           "During calculation, accept/decline second Honest as a response; no new chain after resolution.",[&] {
        single_calculation_chain(assets,options,true,false,both,trace);
      });
    }
    for(bool historical : {false,true}) {
      for(int mode=0;mode<4;++mode) {
        std::string label=historical?"EDISON RULE TEST 031":"CONTROL 031";
        label+=char('A'+mode);label+=" - REVIVAL-APPLIED NEGATION";
        test(label,"Debris-revived Lonefire pays self/other tribute; Book resets pending negation; Call is unnegated control.",[&] {
          revival_negation(assets,historical?options:5<<16,mode,trace);
        });
      }
    }
    for(bool historical : {false,true}) {
      for(bool chalice_on : {false,true}) for(bool remove_drain : {false,true}) {
        std::string label=historical?"EDISON RULE TEST 032":"CONTROL 032";
        label+=char('A'+int(chalice_on)*2+int(remove_drain));label+=" - OVERLAPPING NEGATION";
        test(label,"Lonefire tributes another Plant after Drain remains/is destroyed, with/without independently applied Chalice.",[&] {
          layered_negation(assets,historical?options:5<<16,chalice_on,remove_drain,trace);
        });
      }
    }
    for(bool historical : {false,true}) {
      for(int player=0;player<2;++player) for(bool accept : {false,true}) {
        std::string label=historical?"EDISON RULE TEST 033":"CONTROL 033";
        label+=char('A'+player*2+int(accept));label+=" - CROSS-PLAYER ACROSS-LINK TRIGGERS";
        test(label,"Private Watapon drawn by Jar CL2 precedes opposing Broww discarded by Dealings CL1.",[&] {
          cross_player_across_links(assets,historical?options:5<<16,accept,uint8_t(player),trace);
        });
      }
    }
    for(bool historical : {false,true}) {
      for(bool synchro : {false,true}) {
        std::string label=historical?"EDISON RULE TEST 034":"CONTROL 034";
        label+=synchro?'B':'A';label+=" - HISTORICAL BRAIN CONTROL TARGET";
        test(label,"Brain Control targets a normal monster in both rules; a Synchro target only in Edison.",[&] {
          historical_brain_target(assets,historical?options:5<<16,historical,synchro,trace);
        });
      }
    }
    for(bool historical : {false,true}) {
      for(bool earth : {true,false}) {
        std::string label=historical?"EDISON RULE TEST 035":"CONTROL 035";
        label+=earth?'A':'B';label+=" - HISTORICAL GOYO SYNCHRO MATERIAL";
        test(label,"Goyo accepts EARTH Tuner in both rules and WIND Tuner only in Edison.",[&] {
          historical_goyo_material(assets,historical?options:5<<16,historical,earth,trace);
        });
      }
    }
    for(int variant=0;variant<10;++variant) {
      std::string label="EDISON RULE TEST 036";label+=char('A'+variant);
      label+=" - DECK SIZE BOUNDARIES";
      test(label,"Shared production deck construction: Main 40-60, Extra/Side 0-15, matching Rule 5 size rules.",[&] {
        deck_size_case(variant);
      });
    }
    for(bool historical : {false,true}) for(int variant=0;variant<12;++variant) {
      std::string label=historical?"EDISON RULE TEST 037":"CONTROL 037";
      label+=char('A'+variant);label+=" - DECK COPY LIMITS";
      test(label,"March 2010 limits combine Main/Extra/Side and permanent names; Rule 5 control uses the default three-copy cap.",[&] {
        deck_copy_case(assets,historical?1:5,variant);
      });
    }
    for(bool historical : {false,true}) for(int variant=0;variant<18;++variant) {
      std::string label=historical?"EDISON RULE TEST 038":"CONTROL 038";
      label+=char('A'+variant);label+=" - HISTORICAL CARD POOL";
      test(label,"Historical pool, artwork aliases, later permanent-name aliases, tokens, unknown IDs and forbidden cards.",[&] {
        deck_pool_case(assets,historical?1:5,variant);
      });
    }
    for(bool historical : {false,true}) for(int variant=0;variant<16;++variant) {
      std::string label=historical?"EDISON RULE TEST 039":"CONTROL 039";
      label+=char('A'+variant);label+=" - EXTRA DECK LEGALITY";
      test(label,"Edison Extra Deck admits Fusion/Synchro; modern Xyz/Link controls; Main/Side placement and combined limits.",[&] {
        extra_deck_case(assets,historical?1:5,variant);
      });
    }
    for(int variant=0;variant<12;++variant) {
      std::string label="EDISON RULE TEST 040";label+=char('A'+variant);label+=" - PRODUCTION DECK FILE PARSING";
      test(label,"Shared YDK ingestion preserves valid LF/CRLF/BOM/blank/comment files and rejects malformed passcodes/sections.",[&] {
        deck_file_case(variant);
      });
    }
    for(bool historical : {false,true}) for(int mode=0;mode<3;++mode) {
      std::string label=historical?"EDISON RULE TEST 041":"CONTROL 041";
      label+=char('A'+mode);label+=" - CONTROL CHANGE AND FACE-DOWN RESET";
      test(label,"Brain Control: face-up baseline, Book before resolution, or Book afterward; verify End Phase return.",[&] {
        control_face_down(assets,historical?options:5<<16,mode,trace);
      });
    }
    for(bool historical : {false,true}) for(bool destroy_call : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 043":"CONTROL 043";
      label+=destroy_call?'B':'A';label+=" - SUMMON TRIGGER AFTER DEPARTURE";
      test(label,"Call CL2 revives Sacred Crane; MST CL1 destroys Call/Crane or a separate Book; historical trigger-location comparison.",[&] {
        summon_trigger_location(assets,historical?options:5<<16,historical,destroy_call,trace);
      });
    }
    for(bool historical : {false,true}) for(bool faceup : {true,false}) {
      std::string label=historical?"EDISON RULE TEST 044":"CONTROL 044";
      label+=faceup?'A':'B';label+=" - DELAYED END PHASE RETURN";
      test(label,"Caius banishes D.D. Survivor; only a previously face-up Survivor must return during the End Phase.",[&] {
        delayed_return(assets,historical?options:5<<16,faceup,trace);
      });
    }
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 045 - HISTORICAL TURTLE REPEAT":"CONTROL 045 - MODERN TURTLE LIMIT",
           "Pay tribute and resolve damage; Edison can activate again, Rule 5 is once per turn.",[&] {
        historical_turtle_repeat(assets,historical?options:5<<16,historical,trace);
      });
    }
    for(bool historical : {false,true}) for(bool own : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 046":"CONTROL 046";
      label+=own?'B':'A';label+=" - HISTORICAL BRIONAC TARGETS AND REPEAT";
      test(label,"Discard costs; Edison can bounce own cards and activate repeatedly; Rule 5 retains both modern restrictions.",[&] {
        historical_brionac(assets,historical?options:5<<16,historical,own,trace);
      });
    }
    for(bool historical : {false,true}) for(int mode=0;mode<3;++mode) {
      std::string label=historical?"EDISON RULE TEST 047":"CONTROL 047";
      label+=char('A'+mode);label+=" - HISTORICAL NECROVALLEY";
      test(label,"Non-targeting Treeborn revival, targeted Call revival, and a Graveyard banish cost under Necrovalley.",[&] {
        historical_necrovalley(assets,historical?options:5<<16,historical,mode,trace);
      });
    }
    test("CONTROL 048 - MODERN TREEBORN LIMIT","Wrath negates Treeborn's activation; modern once-per-turn prevents retry.",[&] {
      treeborn_retry(assets,5<<16,false,false,trace);
    });
    for(bool accept : {true,false}) {
      test(accept?"EDISON RULE TEST 048A - PHASE RETRY ACCEPT":"EDISON RULE TEST 048B - PHASE RETRY DECLINE",
           "After Wrath negates Treeborn's activation, accept or decline its second same-Standby offer.",[&] {
        treeborn_retry(assets,options,true,accept,trace);
      });
    }
    for(bool historical : {false,true}) for(bool attack : {true,false}) {
      std::string label=historical?"EDISON RULE TEST 049":"CONTROL 049";
      label+=attack?'A':'B';label+=" - DARKNESS APPROACHES POSITION";
      test(label,"Preserve historical Attack/Defense position when set; no face-down attack; permit Flip Summon.",[&] {
        darkness_position(assets,historical?options:5<<16,historical,attack,trace);
      });
    }
    for(bool historical : {false,true}) for(bool destroy : {true,false}) {
      std::string label=historical?"EDISON RULE TEST 050":"CONTROL 050";
      label+=destroy?'A':'B';label+=" - RYKO OPTIONAL TARGET";
      test(label,"Choose or decline Ryko's target at activation in Edison; destroy then mill; modern selection at resolution.",[&] {
        ryko_targeting(assets,historical?options:5<<16,historical,destroy,trace);
      });
    }
    for(bool historical : {false,true}) for(bool negate : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 051":"CONTROL 051";
      label+=negate?'B':'A';label+=" - QUICKDRAW HAND IGNITION";
      test(label,"Edison Quickdraw sends a monster and summons during ignition resolution; Wrath can negate it before any send.",[&] {
        quickdraw_summon(assets,historical?options:5<<16,historical,negate,trace);
      });
    }
    for(bool historical : {false,true}) for(bool spirit : {false,true}) for(bool drain : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 052":"CONTROL 052";
      label+=char('A'+2*int(spirit)+int(drain));label+=" - MANDATORY PHASE RETRY";
      test(label,"Lumina/Spirit retry after activation negation in Edison; resolved Skill Drain negation consumes the phase trigger.",[&] {
        mandatory_phase_retry(assets,historical?options:5<<16,historical,spirit,drain,trace);
      });
    }
    for(bool historical : {false,true}) for(bool set_before : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 053":"CONTROL 053";
      label+=set_before?'B':'A';label+=" - PRISMA RESOLUTION SEND";
      test(label,"Prisma reveals/sends at resolution in Edison; Book of Moon prevents the send, while modern cost remains paid.",[&] {
        prisma_resolution(assets,historical?options:5<<16,historical,set_before,trace);
      });
    }
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 054 - HISTORICAL RESCUE CAT":"CONTROL 054 - MODERN RESCUE CAT",
           "Repeated Cat activations, recruited Koala effects, and End Phase destruction.",[&] {
        rescue_cat_history(assets,historical?options:5<<16,historical,trace);
      });
    }
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 055 - HISTORICAL STRIKE NINJA":"CONTROL 055 - MODERN STRIKE NINJA",
           "Per-copy versus per-name activation limit, DARK banish costs, and temporary End Phase return.",[&] {
        strike_ninja_history(assets,historical?options:5<<16,historical,trace);
      });
    }
    for(bool historical : {false,true}) for(bool procedure : {true,false}) {
      std::string label=historical?"EDISON RULE TEST 056":"CONTROL 056";
      label+=procedure?'A':'B';label+=" - REDMD HISTORICAL LIMITS";
      test(label,"Two REDMD copies: repeated summon procedures or per-copy non-targeting revival effects.",[&] {
        redmd_limits(assets,historical?options:5<<16,historical,procedure,trace);
      });
    }
    for(bool historical : {false,true}) for(bool faceup : {true,false}) {
      std::string label=historical?"EDISON RULE TEST 057":"CONTROL 057";
      label+=faceup?'A':'B';label+=" - FORTUNE LADY FACE-DOWN DEPARTURE";
      test(label,"Fortune Lady Light destroyed by an effect while face-up or face-down; historical leave-field trigger.",[&] {
        fortune_light_departure(assets,historical?options:5<<16,historical,faceup,trace);
      });
    }
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 058 - HISTORICAL FUTURE FUSION":"CONTROL 058 - MODERN FUTURE FUSION",
           "Initial versus first-Standby material send, second-Standby Fusion Summon, and linked destruction.",[&] {
        future_fusion_timing(assets,historical?options:5<<16,historical,trace);
      });
    }
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 058B - FUTURE FUSION SUMMON LOCK":"CONTROL 058B - FUTURE FUSION SUMMON LOCK",
           "Historical Future Fusion cannot activate under Vanity's Fiend; pinned modern activation comparison.",[&] {
        future_fusion_locked(assets,historical?options:5<<16,historical,trace);
      });
    }
    for(bool historical : {false,true}) for(bool light : {false,true}) for(bool leave : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 059":"CONTROL 059";
      label+=char('A'+2*int(light)+int(leave));label+=" - END DRAGON TARGET DEPARTURE";
      test(label,"Dark/Light End lose 500 ATK/DEF even when their target leaves before resolution.",[&] {
        end_dragon_departure(assets,historical?options:5<<16,light,leave,trace);
      });
    }
    for(bool historical : {false,true}) for(bool jade : {false,true}) for(bool faceup : {true,false}) {
      std::string label=historical?"EDISON RULE TEST 060":"CONTROL 060";
      label+=char('A'+2*int(jade)+int(!faceup));label+=" - FACE-UP BEFORE ATTACK";
      test(label,"Cyber Phoenix/Jade Knight historical reward requires being face-up before selection as an attack target.",[&] {
        faceup_before_attack(assets,historical?options:5<<16,historical,jade,faceup,trace);
      });
    }
    for(bool historical : {false,true}) for(bool revive : {true,false}) {
      std::string label=historical?"EDISON RULE TEST 061":"CONTROL 061";
      label+=revive?'A':'B';label+=" - ARMORY ARM DEPARTED VICTIM";
      test(label,"Actually Synchro Summon Colossal, equip opposing attacker, then accept/decline CL2 revival before Armory damage.",[&] {
        armory_departed_victim(assets,historical?options:5<<16,historical,revive,trace);
      });
    }
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 062 - HISTORICAL UNION LIMIT":"CONTROL 062 - MODERN UNION LIMIT",
           "One Union per monster in Edison, modern multiple equips, same-turn reuse exclusion, next-turn unequip.",[&] {
        union_limit(assets,historical?options:5<<16,historical,trace);
      });
    }
    for(bool historical : {false,true}) for(bool facedown : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 063":"CONTROL 063";
      label+=facedown?'B':'A';label+=" - BLACK GARDEN FACE-DOWN SUMMON";
      test(label,"Black Garden reacts to historical face-down Special Summons; face-up ATK/token and modern controls.",[&] {
        black_garden_facedown(assets,historical?options:5<<16,historical,facedown,trace);
      });
    }
    for(bool historical : {false,true}) for(bool cold : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 064":"CONTROL 064";
      label+=cold?'B':'A';label+=" - MARK OF THE ROSE PHASE CHAINS";
      test(label,"End/Standby control changes, with Cold Wave suppressing the historical Standby trigger.",[&] {
        mark_rose_phases(assets,historical?options:5<<16,historical,cold,trace);
      });
    }
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 065 - DESTINY END DRAGOON RETRY":"CONTROL 065 - MODERN DRAGOON LIMIT",
           "Actually Fusion Summon Dragoon, destroy it, negate its first Standby revival, and check a second paid attempt.",[&] {
        dragoon_retry(assets,historical?options:5<<16,historical,trace);
      });
    }
    for(int mode=0;mode<3;++mode) for(bool summon : {false,true}) {
      const bool historical=mode!=0,own=mode==1;
      std::string label=historical?"EDISON RULE TEST 066":"CONTROL 066";
      label+=char('A'+2*mode+int(summon));label+=" - SOUL EXCHANGE OPTIONAL TRIBUTE";
      test(label,"Choose own or opposing tribute for a summon/cost in Edison; modern opposing-target obligation.",[&] {
        soul_exchange_choice(assets,historical?options:5<<16,historical,own,summon,trace);
      });
    }
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 067 - MY BODY VS TRAP EFFECT":"CONTROL 067 - MODERN MY BODY LIMIT",
           "My Body cannot protect a pending inherent summon; can negate a face-up Trap effect destroying an on-field monster in Edison.",[&] {
        my_body_oppression(assets,historical?options:5<<16,false,trace);
        my_body_trap_effect(assets,historical?options:5<<16,historical,trace);
      });
    }
    for(bool historical : {false,true}) for(bool damage : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 068":"CONTROL 068";
      label+=damage?'B':'A';label+=" - MY BODY DAMAGE STEP";
      test(label,"My Body can negate a Main Phase FLIP; Edison excludes its Damage Step activation.",[&] {
        my_body_damage(assets,historical?options:5<<16,historical,damage,trace);
      });
    }
    for(bool historical : {false,true}) for(int mode=0;mode<4;++mode) {
      std::string label=historical?"EDISON RULE TEST 069":"CONTROL 069";
      label+=char('A'+mode);label+=" - SUMMON DURING RESOLUTION";
      test(label,"Judgment ordinary-summon control; Mausoleum/Offering resolution windows and CL2 missed timing.",[&] {
        summon_chain_window(assets,historical?options:5<<16,mode,trace);
      });
    }
    for(bool historical : {false,true}) for(bool negate : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 070":"CONTROL 070";
      label+=negate?'B':'A';label+=" - URGENT TUNING WINDOWS";
      test(label,"Accept/decline Judgment on Urgent Tuning activation; resolution Synchro and Torrential window.",[&] {
        urgent_tuning_window(assets,historical?options:5<<16,negate,trace);
      });
    }
    for(bool historical : {false,true}) for(bool maus : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 071":"CONTROL 071";
      label+=maus?'B':'A';label+=" - SET DURING RESOLUTION";
      test(label,"Mausoleum/Offering Set timing, normal-summon allowance, and absence of summon-response traps.",[&] {
        set_chain_window(assets,historical?options:5<<16,maus,trace);
      });
    }
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 072 - SWAP PER-COPY EXTRA LIMIT":"CONTROL 072 - MODERN SWAP LIMIT",
           "Per-copy return effects versus a shared modern restriction; only one additional Frog Normal Summon.",[&] {
        swap_extra_limit(assets,historical?options:5<<16,historical,trace);
      });
    }
    for(bool historical : {false,true}) for(bool sequential : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 073":"CONTROL 073";
      label+=sequential?'B':'A';label+=" - LADD DESTRUCTION THEN REVIVAL";
      test(label,"Dupe search positive control versus destruction followed by LADD revival and missed timing.",[&] {
        ladd_sequence(assets,historical?options:5<<16,sequential,trace);
      });
    }
    for(bool historical : {false,true}) for(bool equip : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 074":"CONTROL 074";
      label+=equip?'B':'A';label+=" - TREEBORN MONSTER EQUIP";
      test(label,"An actually equipped Union counts as a Spell for Treeborn; unequipped-monster comparison.",[&] {
        treeborn_equipped_monster(assets,historical?options:5<<16,equip,trace);
      });
    }
    for(int pass=0;pass<3;++pass) {
      const int rule=pass==1?5:1;
      std::string label=rule==1?"EDISON RULE TEST 075":"CONTROL 075";
      label+=char('A'+pass);label+=" - SCRIPT LOADER CONSISTENCY";
      test(label,"All pinned script bytes, production default, helper suffix, rule switching/cache isolation, missing files.",[&] {
        script_consistency(assets,rule);
      });
    }
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 076 - SWAP AFTER EXTRA SUMMON":"CONTROL 076 - MODERN SWAP AFTER EXTRA",
           "Historical second-copy return remains usable after spending the additional summon; no third summon.",[&] {
        swap_after_extra(assets,historical?options:5<<16,historical,trace);
      });
    }
    test("EDISON RULE TEST 080 - OBSERVATION KNOWLEDGE BOUNDARIES",
         "A single recipient-scoped reveal does not reveal adjacent cards; searches, facedown cards, and overlays.", [&] {
      using ygopro::observation_card_hidden;
      for (bool opponent : {false,true}) {
        require(observation_card_hidden(LOCATION_DECK,POS_FACEDOWN_DEFENSE,opponent,false,false),
                "Unsearched deck leaked");
        require(!observation_card_hidden(LOCATION_HAND,POS_FACEDOWN_DEFENSE,opponent,true,false),
                "Explicitly revealed card hidden");
        require(observation_card_hidden(LOCATION_HAND,POS_FACEDOWN_DEFENSE,opponent,false,false)==opponent,
                "Unrevealed adjacent hand card visibility");
        require(observation_card_hidden(LOCATION_EXTRA,POS_FACEDOWN_DEFENSE,opponent,false,false)==opponent,
                "Private Extra Deck visibility");
        require(observation_card_hidden(LOCATION_MZONE,POS_FACEDOWN_DEFENSE,opponent,false,false)==opponent,
                "Facedown monster visibility");
        require(!observation_card_hidden(LOCATION_MZONE,POS_FACEUP_ATTACK,opponent,false,false),
                "Public monster hidden");
        require(!observation_card_hidden(LOCATION_OVERLAY|LOCATION_MZONE,POS_FACEDOWN_DEFENSE,opponent,false,false),
                "Public overlay hidden");
      }
      require(!observation_card_hidden(LOCATION_DECK,POS_FACEDOWN_DEFENSE,false,false,true),
              "Own search candidate hidden");
      require(observation_card_hidden(LOCATION_DECK,POS_FACEDOWN_DEFENSE,true,false,true),
              "Opponent search permission leaked");
    });
    test("EDISON RULE TEST 081A - DECISION HINT LIFECYCLE",
         "Hints are recipient-scoped and consumed once; private card-code hints and stale selections are suppressed.",
         [&] { decision_hint_lifecycle(); });
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 081B - COST AND TARGET CONTEXT":"CONTROL 081B - RULE 5 COST AND TARGET CONTEXT",
           "Brionac discard subgroup waits in cost; its return selection waits in activation setup.",
           [&] { decision_brionac_stages(assets,historical?options:5<<16,trace); });
      test(historical?"EDISON RULE TEST 081C - RESOLVING DISCARD CONTEXT":"CONTROL 081C - RULE 5 RESOLVING DISCARD CONTEXT",
           "Dark World Dealings discards during its operation, not as an activation cost.",
           [&] { decision_operation_stage(assets,historical?options:5<<16,trace); });
    }
    for(bool historical : {false,true}) for(bool dragoon : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 089":"CONTROL 089";
      label+=dragoon?'B':'A';label+=dragoon?" - DARK PANTHER COPIES D-END":" - DARK PANTHER COPIES TREEBORN";
      test(label,"A copied Standby effect without a count limit leaves the turn-reset set; after its End Phase reset and deletion, turn starts find only live members.",[&] {
        panther_copy_reset(assets,historical?options:5<<16,historical,dragoon?destiny_dragoon:treeborn,trace);
      });
    }
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 089C - DARK PANTHER COPIES DARK PANTHER":"CONTROL 089C - STOCK DARK PANTHER REPEAT",
           "The copied Dark Panther effect is the one just used, so it is not offered again that turn; the stock script offers it.",[&] {
        panther_copy_panther(assets,historical?options:5<<16,historical,trace);
      });
    }
    for(bool historical : {false,true}) for(bool flip : {false,true}) {
      std::string label=historical?"EDISON RULE TEST 089":"CONTROL 089";
      label+=flip?'E':'D';label+=flip?" - DARK PANTHER FLIPPED AND REUSED":" - TWO DARK PANTHERS";
      test(label,"Two Dark Panthers on one side each use their effect once; one flipped face-down and Flip Summoned can use it again.",[&] {
        panther_own_uses(assets,historical?options:5<<16,flip,trace);
      });
    }
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 089F - DARK PANTHER CHANGES CONTROL":"CONTROL 089F - STOCK DARK PANTHER CONTROL CHANGE",
           "The copied name and effects end when another player takes control; the stock script keeps them until the End Phase.",[&] {
        panther_control_change(assets,historical?options:5<<16,historical,trace);
      });
      test(historical?"EDISON RULE TEST 089G - DARK PANTHER TAKEN IN THE CHAIN":"CONTROL 089G - STOCK DARK PANTHER TAKEN IN THE CHAIN",
           "Enemy Controller chained to the copy takes Dark Panther, so its player no longer controls it when the copy resolves and nothing is copied; the stock script copies for the new controller.",[&] {
        panther_chained_control(assets,historical?options:5<<16,historical,trace);
      });
    }
    for(bool historical : {false,true}) {
      test(historical?"EDISON RULE TEST 090A - GOLD SARCOPHAGUS CARD AFTER ITS EFFECT IS DELETED":"CONTROL 090A - STOCK GOLD SARCOPHAGUS CARD AFTER ITS EFFECT IS DELETED",
           "The card Gold Sarcophagus adds keeps the reset Standby effect as its reason effect until the next turn start deletes that effect, then keeps none; a live reason effect is kept.",[&] {
        gold_sarcophagus_reason(assets,historical?options:5<<16,trace);
      });
    }
    require(total > 0, "No certification cases matched the requested test number");
    std::cout << "\nSUMMARY: " << total - failures << " PASS, " << failures << " FAIL\n";
    return failures ? 1 : 0;
  } catch (const std::exception& e) {
    std::cerr << "RESULT: FAIL — " << e.what() << '\n';
    return 1;
  }
}
