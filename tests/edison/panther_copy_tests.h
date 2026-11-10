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
        d.respond(7);
      } else if (turns == 2) {
        for (auto* c : copies)
          require(!d.effect_live(c) && !d.effect_rechargeable(c), "Copied effect outlived its End Phase reset");
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
// The opponent's Dark Panther keeps its own use on its controller's turn.
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
      } else {
        require(turns == 2 && e.player == 1, "Unexpected turn");
        opponent_offers = idle_index(e, 5, 1, 0) >= 0;
        require(opponent_offers == 1, "Opponent's Dark Panther lost its own use");
        std::cout << "Observed: Dark Panther copied Dark Panther; activations in that turn " << activations
                  << (historical ? " (copy not offered again)" : " (stock script offers the copy)")
                  << "; the opponent's Dark Panther is offered on its own turn.\n";
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
