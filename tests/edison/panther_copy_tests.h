#pragma once
#include "harness.h"

// Neo-Spacian Dark Panther copies a face-up opposing monster's effects until the End Phase.
// Test 089 covers the turn-reset set when the copied effect's count limit is removed after
// registration (historical Treeborn Frog and D-END), and the once-per-turn use when it copies
// another Dark Panther. Idle response: (index << 16) | group, with group 5 to activate, 2 to
// change position or Flip Summon, and 7 to go to the End Phase.
namespace panther_detail {
using namespace certification;
inline int idle_index(const Event& e, int group, uint8_t player, uint8_t sequence) {
  for (size_t i = 0; i < e.idle[group].size(); ++i) {
    const auto& c = e.idle[group][i];
    if (c.controller == player && c.location == LOCATION_MZONE && c.sequence == sequence) return int(i);
  }
  return -1;
}
inline int pick(const Event& e, uint8_t controller, uint32_t code) {
  for (size_t i = 0; i < e.cards.size(); ++i)
    if (e.cards[i].controller == controller && e.cards[i].code == code) return int(i);
  return -1;
}
}  // namespace panther_detail

// 089A/B: Dark Panther copies Treeborn Frog (A) or D-END (B). The copy is reset in the End Phase
// and deleted at the next turn start. Every turn-reset member must stay live with a limit.
inline void panther_copy_reset(certification::Assets& assets, int options, bool historical,
                               uint32_t target, bool trace) {
  using namespace certification;
  using namespace panther_detail;
  TestDuel d(assets, 0, 0, trace, options);
  d.add({dark_panther, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
  d.add({target, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
  int turns = 0, chains = 0, resolving = 0;
  bool activated = false, copied = false;
  std::vector<effect*> copies;
  d.start(options);
  d.run([&](const Event& e) {
    if (e.kind == MSG_NEW_TURN) ++turns;
    else if (e.kind == MSG_SELECT_IDLECMD) {
      d.check_rechargeable();
      require(e.player == (turns == 2 ? 1 : 0) && d.phase() == PHASE_MAIN1, "Unexpected idle prompt");
      if (turns == 1 && !activated) {
        int i = idle_index(e, 5, 0, 0);
        require(i >= 0, "Dark Panther not offered");
        activated = true;
        d.respond((i << 16) | 5);
      } else if (turns == 1) {
        require(copied && chains == 1, "Dark Panther did not resolve once");
        copies = d.copied_effects(0, 0, EVENT_PHASE + PHASE_STANDBY);
        require(copies.size() == 1, "Expected one copied Standby Phase effect");
        for (auto* c : copies)
          require(c->is_flag(EFFECT_FLAG_COUNT_LIMIT) == !historical && d.effect_rechargeable(c) == !historical,
                  "Copied Standby effect's limit and turn-reset membership disagree");
        require(d.query_value(0, LOCATION_MZONE, 0, QUERY_ALIAS) == target, "Dark Panther did not take the copied name");
        d.respond(7);
      } else if (turns == 2) {
        for (auto* c : copies)
          require(!d.effect_live(c) && !d.effect_rechargeable(c), "Copied effect outlived its End Phase reset");
        require(d.query_value(0, LOCATION_MZONE, 0, QUERY_ALIAS) == dark_panther, "Dark Panther kept the copied name");
        d.respond(7);
      } else {
        require(turns == 3, "Unexpected turn");
        std::cout << "Observed: Dark Panther copied " << (target == treeborn ? "Treeborn Frog" : "D-END")
                  << "; the copied Standby effect " << (historical ? "had no limit and was not" : "kept its limit and was")
                  << " in the turn-reset set; after its reset and deletion, two turn starts found every member live.\n";
        return true;
      }
    } else if (e.kind == MSG_SELECT_CARD) {
      require(turns == 1 && activated && !resolving, "Unexpected card prompt");
      int i = pick(e, 1, target);
      require(i >= 0, "Copy target missing");
      d.respond({1, uint8_t(i)});
    } else if (e.kind == MSG_SELECT_CHAIN) {
      require(!e.forced, "Unexpected forced chain");
      d.respond(-1);
    } else if (e.kind == MSG_CHAINING) {
      require(e.cards.at(0).code == dark_panther && e.chain == 1 && turns == 1, "Unexpected activation");
      ++chains;
    } else if (e.kind == MSG_CHAIN_SOLVING) resolving = e.chain;
    else if (e.kind == MSG_CHAIN_SOLVED) { resolving = 0; copied = true; }
    else if (e.kind == MSG_SELECT_EFFECTYN || e.kind == MSG_SELECT_YESNO || e.kind == MSG_SELECT_OPTION ||
             e.kind == MSG_CHAIN_NEGATED || e.kind == MSG_CHAIN_DISABLED)
      throw std::runtime_error("Unexpected Dark Panther copy event");
    return false;
  });
}

// 089C: Dark Panther copies the opponent's Dark Panther. In Edison the copied effect is the one
// it just used, so it cannot be activated again that turn; the stock script offers it again.
// The opponent's Dark Panther keeps its own use on its controller's turn, and the used one is offered
// again on its controller's next turn, so a flag that never resets would fail.
inline void panther_copy_panther(certification::Assets& assets, int options, bool historical, bool trace) {
  using namespace certification;
  using namespace panther_detail;
  TestDuel d(assets, 0, 0, trace, options);
  d.add({dark_panther, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
  d.add({dark_panther, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
  int turns = 0, activations = 0, solved = 0, opponent_offers = 0;
  d.start(options);
  d.run([&](const Event& e) {
    if (e.kind == MSG_NEW_TURN) ++turns;
    else if (e.kind == MSG_SELECT_IDLECMD) {
      d.check_rechargeable();
      if (turns == 1) {
        require(e.player == 0 && solved == activations, "Unexpected idle prompt");
        int i = idle_index(e, 5, 0, 0);
        if (activations == 0) {
          require(i >= 0, "Dark Panther not offered");
          d.respond((i << 16) | 5);
        } else if (historical) {
          require(activations == 1 && i < 0, "Copied Dark Panther effect offered again in the same turn");
          d.respond(7);
        } else if (activations == 1) {
          require(i >= 0, "Stock script control: copied effect not offered again");
          d.respond((i << 16) | 5);
        } else {
          require(activations == 2, "Unexpected activation count");
          d.respond(7);
        }
      } else if (turns == 2) {
        require(e.player == 1, "Unexpected turn");
        opponent_offers = idle_index(e, 5, 1, 0) >= 0;
        require(opponent_offers == 1, "Opponent's Dark Panther lost its own use");
        d.respond(7);
      } else {
        require(turns == 3 && e.player == 0, "Unexpected turn");
        require(idle_index(e, 5, 0, 0) >= 0, "Used Dark Panther not offered on its controller's next turn");
        std::cout << "Observed: Dark Panther copied Dark Panther; activations in that turn " << activations
                  << (historical ? " (copy not offered again)" : " (stock script offers the copy)")
                  << "; the opponent's Dark Panther is offered on its own turn, and the used one on the next.\n";
        return true;
      }
    } else if (e.kind == MSG_SELECT_CARD) {
      require(turns == 1, "Unexpected card prompt");
      int i = pick(e, 1, dark_panther);
      require(i >= 0, "Opposing Dark Panther missing");
      d.respond({1, uint8_t(i)});
    } else if (e.kind == MSG_SELECT_CHAIN) {
      // The stock control stacks one End Phase reset per activation; the player orders them.
      require(!e.forced || (!historical && turns == 1 && d.phase() == PHASE_END), "Unexpected forced chain");
      d.respond(e.forced ? 0 : -1);
    } else if (e.kind == MSG_CHAINING) {
      require(e.cards.at(0).code == dark_panther && e.cards.at(0).controller == 0 && e.chain == 1,
              "Unexpected activation");
      ++activations;
    } else if (e.kind == MSG_CHAIN_SOLVED) ++solved;
    else if (e.kind == MSG_SELECT_EFFECTYN || e.kind == MSG_SELECT_YESNO || e.kind == MSG_SELECT_OPTION ||
             e.kind == MSG_CHAIN_NEGATED || e.kind == MSG_CHAIN_DISABLED)
      throw std::runtime_error("Unexpected Dark Panther event");
    return false;
  });
  require(activations == (historical ? 1 : 2) && solved == activations, "Wrong number of Dark Panther activations");
}

// 089D: two Dark Panthers on one side each use their own effect once. 089E: a Dark Panther that
// is flipped face-down after using its effect and Flip Summoned in the same turn can use it again.
inline void panther_own_uses(certification::Assets& assets, int options, bool flip, bool trace) {
  using namespace certification;
  using namespace panther_detail;
  TestDuel d(assets, 0, 0, trace, options);
  d.add({dark_panther, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
  if (flip) d.add({book_moon, 0, 0, LOCATION_SZONE, 0, POS_FACEDOWN_DEFENSE});
  else d.add({dark_panther, 0, 0, LOCATION_MZONE, 1, POS_FACEUP_ATTACK});
  d.add({bazoo, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
  int step = 0, activations = 0, solved = 0, resolving = 0;
  uint32_t chaining = 0;
  bool book_used = false, face_down = false, flipped = false;
  d.start(options);
  d.run([&](const Event& e) {
    if (e.kind == MSG_NEW_TURN) require(e.player == 0 && step == 0, "Unexpected turn");
    else if (e.kind == MSG_SELECT_IDLECMD) {
      d.check_rechargeable();
      require(e.player == 0 && d.phase() == PHASE_MAIN1, "Unexpected idle prompt");
      int first = idle_index(e, 5, 0, 0);
      if (!flip) {
        int second = idle_index(e, 5, 0, 1);
        if (step == 0) {
          require(first >= 0 && second >= 0, "Both Dark Panthers must be offered");
          ++step; d.respond((first << 16) | 5);
        } else if (step == 1) {
          require(solved == 1 && first < 0 && second >= 0, "Used Dark Panther offered, or the other one blocked");
          ++step; d.respond((second << 16) | 5);
        } else {
          require(solved == 2 && first < 0 && second < 0, "A Dark Panther offered after its use");
          std::cout << "Observed: two Dark Panthers on one side each activated once; neither was offered again.\n";
          return true;
        }
      } else if (step == 0) {
        require(first >= 0, "Dark Panther not offered");
        ++step; d.respond((first << 16) | 5);
      } else if (step == 1) {
        require(solved == 1 && first < 0, "Used Dark Panther offered again");
        int book = -1;
        for (size_t i = 0; i < e.idle[5].size(); ++i)
          if (e.idle[5][i].code == book_moon) book = int(i);
        require(book >= 0, "Book of Moon not offered");
        ++step; d.respond((book << 16) | 5);
      } else if (step == 2) {
        require(book_used && face_down && d.position(0, LOCATION_MZONE, 0) == POS_FACEDOWN_DEFENSE,
                "Dark Panther not face-down");
        int i = idle_index(e, 2, 0, 0);
        require(i >= 0, "Flip Summon not offered");
        ++step; d.respond((i << 16) | 2);
      } else {
        require(flipped && d.position(0, LOCATION_MZONE, 0) == POS_FACEUP_ATTACK, "Dark Panther not face-up again");
        require(first >= 0, "Dark Panther flipped face-up again was not offered its effect");
        std::cout << "Observed: Dark Panther used its effect, was flipped face-down by Book of Moon and Flip Summoned; "
                     "its effect was offered again.\n";
        return true;
      }
    } else if (e.kind == MSG_SELECT_CARD) {
      require(!resolving, "Unexpected card prompt");
      int i = pick(e, 1, bazoo);
      if (flip && step == 2) {  // Book of Moon targets this player's own Dark Panther.
        i = -1;
        for (size_t k = 0; k < e.cards.size(); ++k)
          if (e.cards[k].controller == 0 && e.cards[k].location == LOCATION_MZONE) i = int(k);
      }
      require(i >= 0, "Target missing");
      d.respond({1, uint8_t(i)});
    } else if (e.kind == MSG_SELECT_CHAIN) {
      require(!e.forced, "Unexpected forced chain");
      d.respond(-1);
    } else if (e.kind == MSG_CHAINING) {
      chaining = e.cards.at(0).code;
      if (chaining == book_moon) book_used = true;
      else { require(chaining == dark_panther, "Unexpected activation"); ++activations; }
    } else if (e.kind == MSG_CHAIN_SOLVING) resolving = e.chain;
    else if (e.kind == MSG_CHAIN_SOLVED) { resolving = 0; if (chaining == dark_panther) ++solved; }
    else if (e.kind == MSG_POS_CHANGE) {
      if (e.to.position & POS_FACEDOWN) face_down = true;
    } else if (e.kind == MSG_FLIPSUMMONED) flipped = true;
    else if (e.kind == MSG_SELECT_EFFECTYN || e.kind == MSG_SELECT_YESNO || e.kind == MSG_SELECT_OPTION ||
             e.kind == MSG_CHAIN_NEGATED || e.kind == MSG_CHAIN_DISABLED)
      throw std::runtime_error("Unexpected Dark Panther event");
    return false;
  });
}

namespace panther_detail {
// What one Dark Panther resolution attached to the card, against the effects it had before: the usage flag
// (Edison only), the name change, the copied effects and the End Phase cleanup effect. Any other effect the
// card owns fails; effects other cards place on it are skipped.
struct PantherAdded {
  std::vector<effect*> flag, name, copies, cleanup;
};
inline bool same_effects(std::vector<effect*> a, std::vector<effect*> b) {
  std::sort(a.begin(), a.end());
  std::sort(b.begin(), b.end());
  return a == b;
}
inline PantherAdded added_effects(const std::vector<effect*>& before, const std::vector<effect*>& now) {
  PantherAdded out;
  for (auto* peffect : now) {
    if (std::find(before.begin(), before.end(), peffect) != before.end()) continue;
    // The control change's own effects on the card (Creature Swap's or Enemy Controller's) are not the copy's.
    if (peffect->owner != peffect->handler) continue;
    if (peffect->code == (EFFECT_FLAG_EFFECT | certification::dark_panther)) out.flag.push_back(peffect);
    else if (peffect->copy_id != 0) out.copies.push_back(peffect);
    else if (peffect->code == EFFECT_CHANGE_CODE) out.name.push_back(peffect);
    else if (peffect->code == EVENT_PHASE + PHASE_END) out.cleanup.push_back(peffect);
    else throw std::runtime_error("Dark Panther holds an unexpected effect of its own, code " + std::to_string(peffect->code));
  }
  return out;
}
}  // namespace panther_detail

// 089F: Dark Panther copies Bazoo, then Creature Swap gives it to the opponent. The copied name and
// effects apply only while the copying player controls the card, so in Edison the name change, every
// copied effect and the End Phase cleanup leave the card, and only the usage flag stays. The stock
// script keeps all three until the End Phase.
inline void panther_control_change(certification::Assets& assets, int options, bool historical, bool trace) {
  using namespace certification;
  using namespace panther_detail;
  TestDuel d(assets, 0, 0, trace, options);
  d.add({dark_panther, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
  d.add({creature_swap, 0, 0, LOCATION_SZONE, 0, POS_FACEDOWN_DEFENSE});
  d.add({bazoo, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
  int step = 0, solved = 0, swaps = 0, places = 0, panther_zone = -1;
  std::vector<effect*> before;
  PantherAdded copy;
  d.start(options);
  d.run([&](const Event& e) {
    if (e.kind == MSG_NEW_TURN) require(e.player == 0 && step == 0, "Unexpected turn");
    else if (e.kind == MSG_SELECT_IDLECMD) {
      d.check_rechargeable();
      require(e.player == 0 && d.phase() == PHASE_MAIN1, "Unexpected idle prompt");
      if (step == 0) {
        before = d.card_effects(0, 0);
        int i = idle_index(e, 5, 0, 0);
        require(i >= 0, "Dark Panther not offered");
        ++step; d.respond((i << 16) | 5);
      } else if (step == 1) {
        require(solved == 1 && d.query_value(0, LOCATION_MZONE, 0, QUERY_ALIAS) == bazoo, "Dark Panther did not copy Bazoo");
        copy = added_effects(before, d.card_effects(0, 0));
        require(copy.flag.size() == (historical ? 1u : 0u) && copy.name.size() == 1 && !copy.copies.empty() &&
                    copy.cleanup.size() == 1,
                "Dark Panther's copy did not attach its name change, copied effects and End Phase cleanup");
        int swap = -1;
        for (size_t i = 0; i < e.idle[5].size(); ++i)
          if (e.idle[5][i].code == creature_swap) swap = int(i);
        require(swap >= 0, "Creature Swap not offered");
        ++step; d.respond((swap << 16) | 5);
      } else {
        require(swaps == 1 && solved == 2 && places == 2 && panther_zone >= 0, "Creature Swap did not resolve");
        require(d.code(1, LOCATION_MZONE, panther_zone) == dark_panther, "The opponent does not control Dark Panther");
        const uint32_t name = d.query_value(1, LOCATION_MZONE, panther_zone, QUERY_ALIAS);
        const auto kept = added_effects(before, d.card_effects(1, panther_zone));
        if (historical) {
          require(name == dark_panther, "The copied name outlived the control change");
          require(kept.name.empty(), "The name change outlived the control change");
          require(kept.copies.empty(), "A copied effect outlived the control change");
          require(kept.cleanup.empty(), "The End Phase cleanup outlived the control change");
          require(same_effects(kept.flag, copy.flag), "Dark Panther's usage flag did not stay");
        } else {
          require(name == bazoo && same_effects(kept.name, copy.name) && same_effects(kept.copies, copy.copies) &&
                      same_effects(kept.cleanup, copy.cleanup),
                  "Stock script control: the copy ended on the control change");
        }
        std::cout << "Observed: Dark Panther copied Bazoo (" << copy.copies.size()
                  << " copied effect) and was given away by Creature Swap; under the opponent its name is "
                  << (name == bazoo ? "still Bazoo" : "Dark Panther") << " and it keeps " << kept.copies.size()
                  << " copied effect, " << kept.name.size() << " name change and " << kept.cleanup.size()
                  << " End Phase cleanup.\n";
        return true;
      }
    } else if (e.kind == MSG_SELECT_CARD) {
      int i = -1;
      if (step == 1) i = pick(e, 1, bazoo);
      else if (e.player == 0) i = pick(e, 0, dark_panther);
      else i = pick(e, 1, bazoo);
      require(i >= 0, "Selection missing");
      d.respond({1, uint8_t(i)});
    } else if (e.kind == MSG_SELECT_CHAIN) {
      require(!e.forced, "Unexpected forced chain");
      d.respond(-1);
    } else if (e.kind == MSG_SELECT_PLACE) {
      // The swapping player places both monsters. Set bits mark unavailable zones: bits 0-4 its own Monster
      // Zones, bits 16-20 the opponent's. The first prompt places Bazoo, the second Dark Panther.
      require(step == 2 && places < 2 && e.player == 0, "Unexpected zone prompt");
      const bool own = (e.mask & 0x1f) != 0x1f;
      const uint32_t zones = own ? e.mask : e.mask >> 16;
      int seq = 0;
      while (seq < 5 && (zones >> seq & 1)) ++seq;
      require(seq < 5 && own == (places == 0), "No free Monster Zone on the expected side");
      if (!own) panther_zone = seq;
      ++places;
      d.respond({uint8_t(own ? 0 : 1), uint8_t(LOCATION_MZONE), uint8_t(seq)});
    } else if (e.kind == MSG_CHAIN_SOLVED) ++solved;
    else if (e.kind == MSG_SWAP) ++swaps;
    else if (e.kind == MSG_SELECT_EFFECTYN || e.kind == MSG_SELECT_YESNO || e.kind == MSG_SELECT_OPTION ||
             e.kind == MSG_CHAIN_NEGATED || e.kind == MSG_CHAIN_DISABLED)
      throw std::runtime_error("Unexpected Creature Swap event");
    return false;
  });
}

// 089G: Dark Panther targets Bazoo, and the opponent chains Enemy Controller, Tributing Blue-Eyes, to take
// Dark Panther. When the copy resolves, its player no longer controls Dark Panther, so in Edison nothing is
// copied and only the usage flag stays. The stock script gives the opponent's new Dark Panther the copy.
inline void panther_chained_control(certification::Assets& assets, int options, bool historical, bool trace) {
  using namespace certification;
  using namespace panther_detail;
  TestDuel d(assets, 0, 0, trace, options);
  d.add({dark_panther, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
  d.add({bazoo, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
  d.add({blue_eyes, 1, 1, LOCATION_MZONE, 1, POS_FACEUP_ATTACK});
  d.add({enemy_controller, 1, 1, LOCATION_SZONE, 0, POS_FACEDOWN_DEFENSE});
  int step = 0, chains = 0, solved = 0, panther_zone = -1;
  bool targeted = false, chained = false, chose = false, tributed = false, stole = false;
  std::vector<effect*> before;
  d.start(options);
  d.run([&](const Event& e) {
    if (e.kind == MSG_NEW_TURN) require(e.player == 0 && step == 0, "Unexpected turn");
    else if (e.kind == MSG_SELECT_IDLECMD) {
      d.check_rechargeable();
      require(e.player == 0 && d.phase() == PHASE_MAIN1, "Unexpected idle prompt");
      if (step == 0) {
        before = d.card_effects(0, 0);
        int i = idle_index(e, 5, 0, 0);
        require(i >= 0, "Dark Panther not offered");
        ++step; d.respond((i << 16) | 5);
      } else {
        require(chained && chose && tributed && stole && chains == 2 && solved == 2 && panther_zone >= 0,
                "The chain did not resolve as set up");
        require(d.code(1, LOCATION_MZONE, panther_zone) == dark_panther, "The opponent does not control Dark Panther");
        const uint32_t name = d.query_value(1, LOCATION_MZONE, panther_zone, QUERY_ALIAS);
        const auto added = added_effects(before, d.card_effects(1, panther_zone));
        if (historical) {
          require(name == dark_panther, "The copied name applied under the opponent");
          require(added.name.empty() && added.copies.empty() && added.cleanup.empty(), "The copy applied under the opponent");
          require(added.flag.size() == 1, "Dark Panther's use was not recorded");
        } else {
          require(name == bazoo && added.name.size() == 1 && !added.copies.empty() && added.cleanup.size() == 1,
                  "Stock script control: the copy did not apply under the opponent");
        }
        std::cout << "Observed: Enemy Controller took Dark Panther in the chain; after its copy resolved, its name under the opponent is "
                  << (name == bazoo ? "Bazoo" : "Dark Panther") << " with " << added.copies.size() << " copied effect.\n";
        return true;
      }
    } else if (e.kind == MSG_SELECT_CARD || e.kind == MSG_SELECT_TRIBUTE) {
      int i = -1;
      if (!targeted) {
        require(e.kind == MSG_SELECT_CARD && e.player == 0, "Unexpected first selection");
        i = pick(e, 1, bazoo); targeted = true;
      } else if (!tributed) {
        require(chose && e.player == 1, "Unexpected Tribute selection");
        i = pick(e, 1, blue_eyes); tributed = true;
      } else {
        require(!stole && e.kind == MSG_SELECT_CARD && e.player == 1, "Unexpected Enemy Controller target");
        i = pick(e, 0, dark_panther); stole = true;
      }
      require(i >= 0, "Selection missing");
      d.respond({1, uint8_t(i)});
    } else if (e.kind == MSG_SELECT_CHAIN) {
      require(!e.forced, "Unexpected forced chain");
      int i = -1;
      if (targeted && !chained && e.player == 1)
        for (size_t k = 0; k < e.cards.size(); ++k)
          if (e.cards[k].code == enemy_controller) i = int(k);
      if (i >= 0) chained = true;
      d.respond(i);
    } else if (e.kind == MSG_SELECT_OPTION) {
      // Enemy Controller offers the position change and the control effect, in that order.
      require(chained && !chose && e.player == 1 && e.options.size() == 2 && e.options[1] == enemy_controller * 16 + 1,
              "Unexpected Enemy Controller option");
      chose = true;
      d.respond(1);
    } else if (e.kind == MSG_SELECT_PLACE) {
      require(stole && panther_zone < 0 && e.player == 1, "Unexpected zone prompt");
      const uint32_t zones = e.mask & 0x1f;
      int seq = 0;
      while (seq < 5 && (zones >> seq & 1)) ++seq;
      require(seq < 5, "No free Monster Zone");
      panther_zone = seq;
      d.respond({uint8_t(1), uint8_t(LOCATION_MZONE), uint8_t(seq)});
    } else if (e.kind == MSG_CHAINING) ++chains;
    else if (e.kind == MSG_CHAIN_SOLVED) ++solved;
    else if (e.kind == MSG_SELECT_EFFECTYN || e.kind == MSG_SELECT_YESNO || e.kind == MSG_CHAIN_NEGATED ||
             e.kind == MSG_CHAIN_DISABLED)
      throw std::runtime_error("Unexpected Enemy Controller event");
    return false;
  });
}
