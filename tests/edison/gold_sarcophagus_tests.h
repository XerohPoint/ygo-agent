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
