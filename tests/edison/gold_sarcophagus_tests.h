#pragma once
#include "harness.h"

// Gold Sarcophagus banishes a card from the Deck and adds it to the hand during its controller's second
// Standby Phase after activation, through a player effect that resets at that Standby Phase. The card keeps
// that effect as its reason effect, and the core deletes reset effects at the next turn start. Test 090
// checks that the card's pointer follows the effect: kept while the effect is live, cleared when it is
// deleted. In 090B the card is discarded for hand size before the deletion, so the effect has moved to the
// card's temp state. The run loop checks every card's reason effects after each process call. Idle response:
// (index << 16) | group, with group 5 to activate and 7 to go to the End Phase.
namespace gold_sarcophagus_detail {
using namespace certification;
inline int hand_or_set_index(const Event& e, uint32_t code) {
  for (size_t i = 0; i < e.idle[5].size(); ++i)
    if (e.idle[5][i].controller == 0 && e.idle[5][i].code == code) return int(i);
  return -1;
}
}  // namespace gold_sarcophagus_detail

inline void gold_sarcophagus_reason(certification::Assets& assets, int options, bool discard, bool trace) {
  using namespace certification;
  using namespace gold_sarcophagus_detail;
  TestDuel d(assets, 0, 0, trace, options);
  d.add({gold_sarcophagus, 0, 0, LOCATION_SZONE, 0, POS_FACEDOWN_DEFENSE});
  d.add({call_haunted, 0, 0, LOCATION_DECK, 0, POS_FACEDOWN_DEFENSE});
  // Six cards that cannot be played without Tributes, so the added card makes seven at turn 5's End Phase.
  if (discard)
    for (int i = 0; i < 6; ++i) d.add({blue_eyes, 0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE});
  int turns = 0, chains = 0, resolving = 0, solved = 0, banished = 0, added = 0, discarded = 0;
  bool activated = false;
  const effect* activation = nullptr;
  const effect* delayed = nullptr;
  d.start(options);
  d.run([&](const Event& e) {
    if (e.kind == MSG_NEW_TURN) {
      require(e.player == turns % 2 && turns < 6, "Unexpected turn");
      ++turns;
    } else if (e.kind == MSG_SELECT_IDLECMD) {
      require(e.player == (turns + 1) % 2 && d.phase() == PHASE_MAIN1, "Unexpected idle prompt");
      if (turns == 1 && !activated) {
        int i = hand_or_set_index(e, gold_sarcophagus);
        require(i >= 0, "Gold Sarcophagus not offered");
        activated = true;
        d.respond((i << 16) | 5);
        return false;
      }
      if (turns <= 4) {
        // Banished by the activation effect, a permanent effect of Gold Sarcophagus that never resets.
        require(chains == 1 && solved == 1 && banished == 1 && added == 0, "Gold Sarcophagus did not resolve once");
        card* pcard = d.card_in(0, LOCATION_REMOVED, call_haunted);
        require(pcard->current.reason_effect != nullptr, "Banished card has no reason effect");
        if (!activation) activation = pcard->current.reason_effect;
        require(pcard->current.reason_effect == activation && d.effect_live(activation) && !d.effect_reset(activation) &&
                    activation->owner->data.code == gold_sarcophagus && (activation->type & EFFECT_TYPE_ACTIVATE),
                "A live reason effect changed at a turn start");
      } else if (turns == 5) {
        // Added in this Standby Phase by the player effect, which reset there and is deleted at the next turn start.
        require(added == 1 && d.count(0, LOCATION_REMOVED) == 0, "Gold Sarcophagus did not add the card");
        card* pcard = d.card_in(0, LOCATION_HAND, call_haunted);
        delayed = pcard->current.reason_effect;
        require(delayed && delayed != activation && d.effect_live(delayed) && d.effect_reset(delayed) &&
                    delayed->owner->data.code == gold_sarcophagus && delayed->code == EVENT_PHASE + PHASE_STANDBY &&
                    (delayed->type & EFFECT_TYPE_FIELD) && (delayed->type & EFFECT_TYPE_CONTINUOUS),
                "Added card does not name the reset Standby effect");
        require(pcard->temp.reason_effect == activation, "Added card lost the activation effect it was banished by");
      } else {
        require(turns == 6 && e.player == 1 && delayed && !d.effect_live(delayed) && d.effect_live(activation),
                "The turn start deleted the wrong effects");
        if (discard) {
          require(discarded == 1, "The added card was not discarded");
          card* pcard = d.card_in(0, LOCATION_GRAVE, call_haunted);
          require(pcard->current.reason_effect == nullptr && pcard->temp.reason_effect == nullptr,
                  "Discarded card kept the deleted Standby effect");
          std::cout << "Observed: the card Gold Sarcophagus added was discarded for hand size, which moved the reset "
                       "Standby effect to its temp state; the next turn start deleted that effect and cleared it there.\n";
        } else {
          card* pcard = d.card_in(0, LOCATION_HAND, call_haunted);
          require(pcard->current.reason_effect == nullptr, "Added card kept the deleted Standby effect");
          require(pcard->temp.reason_effect == activation, "The turn start cleared a pointer to the live activation effect");
          std::cout << "Observed: the banished card named Gold Sarcophagus's activation effect through three turn starts; "
                       "the card it added named the reset Standby effect until the next turn start deleted that effect, "
                       "then named none, and still kept the live activation effect as its earlier reason.\n";
        }
        return true;
      }
      d.respond(7);
    } else if (e.kind == MSG_SELECT_CARD) {
      if (turns == 1) {
        require(resolving == 1 && e.player == 0 && e.cards.size() == 1 && e.cards[0].code == call_haunted,
                "Unexpected card prompt");
        d.respond({1, 0});
      } else {
        require(discard && turns == 5 && d.phase() == PHASE_END && e.player == 0 && e.minimum == 1 && e.maximum == 1 &&
                    e.cards.size() == 7, "Unexpected card prompt");
        int pick = -1;
        for (size_t i = 0; i < e.cards.size(); ++i)
          if (e.cards[i].code == call_haunted) pick = int(i);
        require(pick >= 0, "Added card not offered for the discard");
        d.respond({1, uint8_t(pick)});
      }
    } else if (e.kind == MSG_SELECT_CHAIN) {
      require(!e.forced, "Unexpected forced chain");
      d.respond(-1);
    } else if (e.kind == MSG_CHAINING) {
      require(turns == 1 && e.chain == 1 && e.cards.at(0).code == gold_sarcophagus, "Unexpected activation");
      ++chains;
    } else if (e.kind == MSG_CHAIN_SOLVING) resolving = e.chain;
    else if (e.kind == MSG_CHAIN_SOLVED) { resolving = 0; ++solved; }
    else if (e.kind == MSG_MOVE && e.from.code == call_haunted) {
      if (e.to.location == LOCATION_REMOVED) {
        require(turns == 1 && resolving == 1 && e.from.location == LOCATION_DECK, "Wrong banishment");
        ++banished;
      } else if (e.to.location == LOCATION_HAND) {
        require(turns == 5 && d.phase() == PHASE_STANDBY && e.from.location == LOCATION_REMOVED && (e.reason & REASON_EFFECT),
                "Wrong addition to the hand");
        ++added;
      } else {
        require(discard && turns == 5 && d.phase() == PHASE_END && e.from.location == LOCATION_HAND &&
                    e.to.location == LOCATION_GRAVE && (e.reason & REASON_DISCARD), "Wrong discard");
        card* pcard = d.card_in(0, LOCATION_GRAVE, call_haunted);
        require(pcard->current.reason_effect == nullptr && pcard->temp.reason_effect == delayed && d.effect_live(delayed),
                "The discard did not move the Standby effect to the temp state");
        ++discarded;
      }
    } else if (e.kind == MSG_SELECT_EFFECTYN || e.kind == MSG_SELECT_YESNO || e.kind == MSG_SELECT_OPTION ||
               e.kind == MSG_CHAIN_NEGATED || e.kind == MSG_CHAIN_DISABLED)
      throw std::runtime_error("Unexpected Gold Sarcophagus event");
    return false;
  });
}

// 090C: Dark Panther copies the opponent's Destiny HERO - Doom Lord, and the copied effect banishes the opponent's
// Blue-Eyes until its controller's second Standby Phase (REASON_TEMPORARY). The copy resets at that End Phase and the
// next turn start deletes it, so the banished card's reason effect is cleared. Duel.ReturnToField compares the
// banishing effect's owner with the returning effect's owner; both are Dark Panther, and the card must return.
// Idle group 5 lists Dark Panther once per effect, told apart by description.
inline void gold_sarcophagus_doom_lord(certification::Assets& assets, int options, bool trace) {
  using namespace certification;
  TestDuel d(assets, 0, 0, trace, options);
  d.add({dark_panther, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
  d.add({blue_eyes, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
  d.add({doom_lord, 1, 1, LOCATION_MZONE, 1, POS_FACEUP_ATTACK});
  int turns = 0, step = 0, banished = 0, returned = 0;
  const effect* copy = nullptr;
  d.start(options);
  d.run([&](const Event& e) {
    if (e.kind == MSG_NEW_TURN) {
      require(e.player == turns % 2 && turns < 5, "Unexpected turn");
      ++turns;
    } else if (e.kind == MSG_SELECT_IDLECMD) {
      require(e.player == (turns + 1) % 2 && d.phase() == PHASE_MAIN1, "Unexpected idle prompt");
      if (turns == 1 && step < 2) {
        uint32_t want = (step == 0 ? dark_panther : doom_lord) * 16;
        int pick = -1;
        for (size_t i = 0; i < e.idle[5].size(); ++i)
          if (e.idle[5][i].controller == 0 && e.idle[5][i].code == dark_panther && e.idle[5][i].description == want) pick = int(i);
        require(pick >= 0, step == 0 ? "Dark Panther not offered" : "Copied Doom Lord effect not offered");
        ++step;
        d.respond((pick << 16) | 5);
        return false;
      }
      if (turns == 1) {
        require(banished == 1, "Copied Doom Lord effect did not banish Blue-Eyes");
        card* pcard = d.card_in(1, LOCATION_REMOVED, blue_eyes);
        copy = pcard->current.reason_effect;
        require(copy && d.effect_live(copy) && copy->copy_id != 0 && copy->owner->data.code == dark_panther &&
                    (pcard->current.reason & REASON_TEMPORARY), "Banished card does not name the copied effect");
      } else if (turns < 5) {
        card* pcard = d.card_in(1, LOCATION_REMOVED, blue_eyes);
        require(returned == 0 && !d.effect_live(copy) && pcard->current.reason_effect == nullptr &&
                    (pcard->current.reason & REASON_TEMPORARY), "The deleted copy was not cleared from the banished card");
      } else {
        require(returned == 1 && d.count(1, LOCATION_REMOVED) == 0 && d.cards_in(1, LOCATION_MZONE, blue_eyes).size() == 1,
                "Blue-Eyes did not return");
        std::cout << "Observed: the copied Doom Lord effect banished Blue-Eyes; after the next turn start deleted the copy, "
                     "the card named no reason effect, and Doom Lord's return, owned by Dark Panther, still brought it back "
                     "on turn 5.\n";
        return true;
      }
      d.respond(7);
    } else if (e.kind == MSG_SELECT_CARD) {
      require(turns == 1 && e.player == 0 && (step == 1 || step == 2), "Unexpected card prompt");
      uint32_t want = step == 1 ? doom_lord : blue_eyes;
      int pick = -1;
      for (size_t i = 0; i < e.cards.size(); ++i)
        if (e.cards[i].code == want && e.cards[i].controller == 1) pick = int(i);
      require(pick >= 0, "Target not offered");
      d.respond({1, uint8_t(pick)});
    } else if (e.kind == MSG_SELECT_CHAIN) {
      require(!e.forced, "Unexpected forced chain");
      d.respond(-1);
    } else if (e.kind == MSG_SELECT_PLACE) {
      require(turns == 5 && e.player == 1 && e.count == 1, "Unexpected placement");
      int slot = 0;
      while (slot < 5 && (e.mask & (1u << slot))) ++slot;
      require(slot < 5, "No free monster zone");
      d.respond({1, LOCATION_MZONE, uint8_t(slot)});
    } else if (e.kind == MSG_MOVE && e.from.code == blue_eyes) {
      if (e.to.location == LOCATION_REMOVED) {
        require(turns == 1 && e.from.location == LOCATION_MZONE && (e.reason & REASON_TEMPORARY), "Wrong banishment");
        ++banished;
      } else {
        require(turns == 5 && d.phase() == PHASE_STANDBY && e.from.location == LOCATION_REMOVED && e.to.location == LOCATION_MZONE,
                "Wrong return");
        ++returned;
      }
    } else if (e.kind == MSG_SELECT_EFFECTYN || e.kind == MSG_SELECT_YESNO || e.kind == MSG_SELECT_OPTION ||
               e.kind == MSG_CHAIN_NEGATED || e.kind == MSG_CHAIN_DISABLED)
      throw std::runtime_error("Unexpected Doom Lord event");
    return false;
  });
}

// 090D: Rescue Cat recruits two Sea Koala and destroys them at the End Phase through a player effect with no reset.
// That effect resets itself at the next End Phase, once no recruit is left, and the turn start after that deletes it.
// The Koala in the GY then name no reason effect. Rescue Cat, sent to the GY as its own cost, still names its own
// live effect at that turn start, and the Koala still name it as their Special Summon reason.
inline void gold_sarcophagus_rescue_cat(certification::Assets& assets, int options, bool trace) {
  using namespace certification;
  TestDuel d(assets, 0, 0, trace, options);
  d.add({rescue_cat, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK});
  for (int i = 0; i < 2; ++i) d.add({sea_koala, 0, 0, LOCATION_DECK, 0, POS_FACEDOWN_DEFENSE});
  int turns = 0, recruited = 0, destroyed = 0;
  bool activated = false;
  const effect* activation = nullptr;
  const effect* destroyer = nullptr;
  d.start(options);
  d.run([&](const Event& e) {
    if (e.kind == MSG_NEW_TURN) {
      require(e.player == turns % 2 && turns < 3, "Unexpected turn");
      ++turns;
    } else if (e.kind == MSG_SELECT_IDLECMD) {
      require(e.player == (turns + 1) % 2 && d.phase() == PHASE_MAIN1, "Unexpected idle prompt");
      if (turns == 1 && !activated) {
        int pick = -1;
        for (size_t i = 0; i < e.idle[5].size(); ++i)
          if (e.idle[5][i].code == rescue_cat) pick = int(i);
        require(pick >= 0, "Rescue Cat not offered");
        activated = true;
        d.respond((pick << 16) | 5);
        return false;
      }
      if (turns == 1) {
        require(recruited == 2, "Rescue Cat did not recruit two Koala");
        activation = d.card_in(0, LOCATION_GRAVE, rescue_cat)->current.reason_effect;
        require(activation && d.effect_live(activation) && activation->owner->data.code == rescue_cat,
                "Rescue Cat does not name its own effect");
      } else {
        auto koala = d.cards_in(0, LOCATION_GRAVE, sea_koala);
        require(destroyed == 2 && koala.size() == 2, "The Koala were not destroyed");
        if (!destroyer) destroyer = koala[0]->current.reason_effect;
        require(destroyer && destroyer != activation && destroyer->owner->data.code == rescue_cat &&
                    !(destroyer->reset_flag & RESET_PHASE), "The Koala do not name the End Phase effect");
        for (auto* pcard : koala)
          require(pcard->spsummon.reason_effect == activation, "A Koala lost its Special Summon reason");
        require(d.card_in(0, LOCATION_GRAVE, rescue_cat)->current.reason_effect == activation && d.effect_live(activation),
                "The turn start cleared Rescue Cat's live reason effect");
        if (turns == 2) {
          for (auto* pcard : koala)
            require(pcard->current.reason_effect == destroyer && d.effect_live(destroyer) && !d.effect_reset(destroyer),
                    "The End Phase effect changed before it reset");
        } else {
          for (auto* pcard : koala)
            require(pcard->current.reason_effect == nullptr && !d.effect_live(destroyer),
                    "A Koala kept the deleted End Phase effect");
          std::cout << "Observed: the Koala named Rescue Cat's End Phase effect, which has no reset, until it reset itself "
                       "and the next turn start deleted it; Rescue Cat's own effect stayed as the Cat's reason and the "
                       "Koala's Special Summon reason.\n";
          return true;
        }
      }
      d.respond(7);
    } else if (e.kind == MSG_SELECT_CARD) {
      require(turns == 1 && e.player == 0 && e.minimum == 2 && e.maximum == 2 && e.cards.size() == 2, "Unexpected card prompt");
      d.respond({2, 0, 1});
    } else if (e.kind == MSG_SELECT_PLACE) {
      require(turns == 1 && e.player == 0 && e.count == 1, "Unexpected placement");
      int slot = 0;
      while (slot < 5 && (e.mask & (1u << slot))) ++slot;
      require(slot < 5, "No free monster zone");
      d.respond({0, LOCATION_MZONE, uint8_t(slot)});
    } else if (e.kind == MSG_SELECT_POSITION) {
      d.respond(POS_FACEUP_ATTACK);
    } else if (e.kind == MSG_SELECT_CHAIN) {
      if (e.forced) {
        require(turns == 1 && d.phase() == PHASE_END && !e.cards.empty() && e.cards[0].code == rescue_cat, "Unexpected forced chain");
        d.respond(0);
      } else d.respond(-1);
    } else if (e.kind == MSG_MOVE && e.from.code == sea_koala) {
      if (e.from.location == LOCATION_DECK) {
        require(turns == 1 && e.to.location == LOCATION_MZONE, "Wrong recruitment");
        ++recruited;
      } else {
        require(turns == 1 && d.phase() == PHASE_END && e.to.location == LOCATION_GRAVE && (e.reason & REASON_DESTROY),
                "Wrong destruction");
        ++destroyed;
      }
    } else if (e.kind == MSG_SELECT_EFFECTYN || e.kind == MSG_SELECT_YESNO || e.kind == MSG_SELECT_OPTION || e.kind == MSG_CHAIN_NEGATED)
      throw std::runtime_error("Unexpected Rescue Cat event");
    return false;
  });
}
