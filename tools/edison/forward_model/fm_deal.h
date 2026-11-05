// The determinizer: re-deal hidden state for one viewer from public inputs and a seed.
//
// Rule enforced here: what is dealt depends only on (a) the slot layout, which is public, (b) the
// public memory, (c) the assumed decklist, (d) the seed. True hidden identities may be read only to
// find the card object that carries a dealt code, and to refuse a deal. A refused deal is counted and
// produces no world, so reading state to refuse can leak at most that one bit. R4 measures that bit.
//
// What is assumed known, explicitly:
//   - The viewer's own lists, hand and set cards: real knowledge.
//   - The opponent's Main Deck: AssumedList::opponent_main. A deal reads no hidden code to learn it.
//     A list that disagrees with the duel is refused (kListMismatch): materialising cards outside the
//     true list is mechanically possible (test R5) but is not offered here.
//   - The opponent's Extra Deck is not re-dealt. It stays as it is, which is sound only under the same
//     known-list assumption. DealReport::opponent_extra_not_redealt records how many stayed.
//   - Face-down banished cards are not re-dealt. An opposing one the viewer has not seen would keep the
//     true identity out of the pool, so the pool would depend on it: the deal is refused
//     (kFaceDownBanished). DealReport::facedown_banished_not_redealt counts the rest.
#pragma once
#include <array>

#include "fm_env.h"

namespace fm {

#ifdef FM_MUTANTS
// Negative controls, compiled only into fm_bench_mutant. Set by the --mutant argument.
enum class Mutant { None, LeakHand, LeakSeed, LeakOrder, RollbackHistory, RollbackCursor, RollbackReveal, RollbackBind, RollbackRng };
inline Mutant g_mutant = Mutant::None;
#endif

// ---------------------------------------------------------------------------------------------
// Slots. A slot is a place a hidden card can be: a deck index, a hand index, a set zone. Re-dealing
// keeps the slot's rule state (how the slot came to be, set-turn status, field ids, position) and
// changes which card object occupies it.
struct SlotState {
  card *holder;
  uint8 controler, location, sequence, position, reason_player, summon_player, pre_controler,
      pre_location, pre_sequence, pre_position, pre_reason_player;
  bool pzone, pre_pzone;
  uint32 reason, pre_reason, status, fieldid, fieldid_r, summon_info, to_field_param;
  uint16 turnid, turn_counter, cardid;
  card *reason_card, *pre_reason_card;
  effect *reason_effect, *pre_reason_effect;
  card::sendto_param_t sendto;
};

inline SlotState capture_slot(card *c) {
  SlotState s{};
  s.holder = c;
  s.controler = c->current.controler;
  s.location = c->current.location;
  s.sequence = c->current.sequence;
  s.position = c->current.position;
  s.pzone = c->current.pzone;
  s.reason = c->current.reason;
  s.reason_card = c->current.reason_card;
  s.reason_effect = c->current.reason_effect;
  s.reason_player = c->current.reason_player;
  s.pre_controler = c->previous.controler;
  s.pre_location = c->previous.location;
  s.pre_sequence = c->previous.sequence;
  s.pre_position = c->previous.position;
  s.pre_pzone = c->previous.pzone;
  s.pre_reason = c->previous.reason;
  s.pre_reason_card = c->previous.reason_card;
  s.pre_reason_effect = c->previous.reason_effect;
  s.pre_reason_player = c->previous.reason_player;
  s.status = c->status;
  s.fieldid = c->fieldid;
  s.fieldid_r = c->fieldid_r;
  s.summon_info = c->summon_info;
  s.summon_player = c->summon_player;
  s.to_field_param = c->to_field_param;
  s.turnid = c->turnid;
  s.turn_counter = c->turn_counter;
  s.cardid = c->cardid;
  s.sendto = c->sendto_param;
  return s;
}

inline void occupy_slot(card *c, const SlotState &s, bool inherit_cardid) {
  c->current.controler = s.controler;
  c->current.location = s.location;
  c->current.sequence = s.sequence;
  c->current.position = s.position;
  c->current.pzone = s.pzone;
  c->current.reason = s.reason;
  c->current.reason_card = s.reason_card;
  c->current.reason_effect = s.reason_effect;
  c->current.reason_player = s.reason_player;
  c->previous.controler = s.pre_controler;
  c->previous.location = s.pre_location;
  c->previous.sequence = s.pre_sequence;
  c->previous.position = s.pre_position;
  c->previous.pzone = s.pre_pzone;
  c->previous.reason = s.pre_reason;
  c->previous.reason_card = s.pre_reason_card;
  c->previous.reason_effect = s.pre_reason_effect;
  c->previous.reason_player = s.pre_reason_player;
  c->status = s.status;
  c->fieldid = s.fieldid;
  c->fieldid_r = s.fieldid_r;
  c->summon_info = s.summon_info;
  c->summon_player = s.summon_player;
  c->to_field_param = s.to_field_param;
  c->turnid = s.turnid;
  c->turn_counter = s.turn_counter;
  if (inherit_cardid) c->cardid = s.cardid;
  c->sendto_param = s.sendto;
  c->q_cache.clear_cache();
}

inline card *&slot_ref(field *f, const SlotState &s) {
  auto &pl = f->player[s.controler];
  switch (s.location) {
    case LOCATION_DECK: return pl.list_main[s.sequence];
    case LOCATION_HAND: return pl.list_hand[s.sequence];
    case LOCATION_MZONE: return pl.list_mzone[s.sequence];
    case LOCATION_SZONE: return pl.list_szone[s.sequence];
  }
  throw std::runtime_error("slot location not supported");
}

// Every std::set<card*, card_sort> is ordered by cardid, and a re-deal moves cardids between objects.
// Each set still holding a moved object is rebuilt, or its later lookups would silently miss.
inline bool sorted_by_cardid(const card_set &set) {
  uint32_t last = 0;
  bool first = true;
  for (card *c : set) {
    if (!first && c->cardid <= last) return false;
    last = c->cardid;
    first = false;
  }
  return true;
}

inline int rebuild_card_sets(duel *d) {
  int rebuilt = 0;
  auto fix = [&](card_set &set) {
    if (sorted_by_cardid(set)) return;
    card_set copy(set.begin(), set.end());
    set.swap(copy);
    ++rebuilt;
  };
  auto &core = d->game_field->core;
  for (card_set *set :
       {&core.leave_confirmed, &core.special_summoning, &core.unable_tofield_set, &core.equiping_cards,
        &core.control_adjust_set[0], &core.control_adjust_set[1], &core.unique_destroy_set,
        &core.self_destroy_set, &core.self_tograve_set, &core.trap_monster_adjust_set[0],
        &core.trap_monster_adjust_set[1], &core.release_cards, &core.release_cards_ex,
        &core.release_cards_ex_oneof, &core.battle_destroy_rep, &core.fusion_materials,
        &core.synchro_materials, &core.operated_set, &core.discarded_set, &core.destroy_canceled,
        &core.indestructable_count_set, &core.delayed_enable_set, &core.set_group_pre_set,
        &core.set_group_set})
    fix(*set);
  for (card *c : d->cards)
    for (card_set *set : {&c->equiping_cards, &c->material_cards, &c->effect_target_owner, &c->effect_target_cards})
      fix(*set);
  for (auto *groups : {&d->groups, &d->sgroups})
    for (group *g : *groups) {
      if (sorted_by_cardid(g->container)) continue;
      card *at = g->it != g->container.end() && !g->is_iterator_dirty ? *g->it : nullptr;
      card_set copy(g->container.begin(), g->container.end());
      g->container.swap(copy);
      g->it = at ? g->container.find(at) : g->container.end();
      ++rebuilt;
    }
  return rebuilt;
}

// The core sorts offered effects by effect id, and a card's in-range effects take fresh ids from the
// duel's counter each time it arrives somewhere (card::enable_field_effect). So an id says when a card
// arrived, and that belongs to the slot. A dealt card must take the slot's time: left alone it would keep
// the ids of where it truly was, and option order in a sampled world's prompts would read the true
// history. R4 caught this: option order differed between worlds with identical slots. The new ids start
// from the slot's arrival (its fieldid, from the same counter) and follow the core's own order.
inline void take_slot_effect_ids(card *c, uint32_t arrival) {
  uint32_t next = arrival + 1;
  for (auto &it : c->single_effect)
    if (it.second->is_flag(EFFECT_FLAG_SINGLE_RANGE) && it.second->in_range(c)) it.second->id = next++;
  for (auto &it : c->field_effect)
    if (it.second->in_range(c)) it.second->id = next++;
  if (c->current.location == LOCATION_SZONE)
    for (auto &it : c->equip_effect) it.second->id = next++;
  for (auto &it : c->target_effect)
    if (it.second->in_range(c)) it.second->id = next++;
}

// assignment[i] is the object that takes over the slot currently held by holders[i]. Every object's
// effects are cancelled, then re-applied in slot order, even for a card that stays in its zone. The core
// keeps same-code effects in registration order and lists options in that order, so keeping a card's old
// place in those lists would carry where it truly was into a sampled world's prompts.
inline int apply_assignment(duel *d, const std::vector<card *> &holders, const std::vector<card *> &assignment,
                            bool inherit_cardid) {
  field *f = d->game_field;
  std::vector<SlotState> slots;
  slots.reserve(holders.size());
  for (card *c : holders) slots.push_back(capture_slot(c));
  Scope s(true);
  for (card *c : holders) c->cancel_field_effect();
  for (size_t i = 0; i < holders.size(); ++i) {
    occupy_slot(assignment[i], slots[i], inherit_cardid);
    slot_ref(f, slots[i]) = assignment[i];
  }
  for (size_t i = 0; i < holders.size(); ++i) {
    assignment[i]->apply_field_effect();
    if (slots[i].location != LOCATION_DECK) take_slot_effect_ids(assignment[i], slots[i].fieldid);
  }
  f->refresh_player_info(0);
  f->refresh_player_info(1);
  return inherit_cardid ? rebuild_card_sets(d) : 0;
}

// ---------------------------------------------------------------------------------------------
// Public memory: what each player can know about which object is which. It is written only while the
// true duel plays, and reads only public facts, so two duels with one public history produce one
// memory. The rule that keeps that true: a card stays tracked only while no hidden event could have
// moved it, and whether such an event happened is judged from public counts alone, never from which
// object moved. Forgetting costs a little information. Keeping one would leak the true move.
enum Tracked : uint8_t { kUntracked = 0, kPublicZone = 1, kInHand = 2, kSetOnField = 3 };

struct PublicMemory {
  std::vector<uint8_t> cls[2] = {std::vector<uint8_t>(4096, 0), std::vector<uint8_t>(4096, 0)};
  int previous_set[2] = {0, 0}, previous_deck[2] = {0, 0};
  bool started = false;
  // What each seat was shown between decisions, in order. Two duels with one public history agree here.
  uint64_t confirm_chain[2] = {0, 0};
  long tap_confirms = 0, tap_decktops = 0, tap_unmatched = 0;
  // One flag per history ring row: was the row's card public to the other seat when it was written?
  std::array<uint8_t, 64> row_public[2] = {};
};

inline card *card_at(field *f, int controller, int location, int sequence) {
  auto &pl = f->player[controller];
  const card_vector *v = nullptr;
  switch (location) {
    case LOCATION_DECK: v = &pl.list_main; break;
    case LOCATION_HAND: v = &pl.list_hand; break;
    case LOCATION_MZONE: v = &pl.list_mzone; break;
    case LOCATION_SZONE: v = &pl.list_szone; break;
    case LOCATION_GRAVE: v = &pl.list_grave; break;
    case LOCATION_REMOVED: v = &pl.list_remove; break;
    case LOCATION_EXTRA: v = &pl.list_extra; break;
    default: return nullptr;
  }
  return sequence >= 0 && sequence < static_cast<int>(v->size()) ? (*v)[sequence] : nullptr;
}

struct TapContext {
  FmEnv *env;
  PublicMemory *memory;
};

// MSG_CONFIRM_CARDS and MSG_CONFIRM_DECKTOP each yield to the environment as soon as they are written,
// so each opens its batch. Only a batch's first message is read.
inline void on_core_messages(void *context, const byte *data, int length) {
  auto *tap = static_cast<TapContext *>(context);
  static const bool off = getenv("FM_NO_MESSAGE_TAP") != nullptr;  // To measure what this adds.
  if (off) return;
  if (length < 3 || (data[0] != MSG_CONFIRM_CARDS && data[0] != MSG_CONFIRM_DECKTOP)) return;
  bool decktop = data[0] == MSG_CONFIRM_DECKTOP;
  int player = data[1], count = data[2];
  if (player > 1 || 3 + 7 * count > length) {
    ++tap->memory->tap_unmatched;
    return;
  }
  (decktop ? tap->memory->tap_decktops : tap->memory->tap_confirms)++;
  field *f = tap->env->core()->game_field;
  for (int i = 0; i < count; ++i) {
    const byte *entry = data + 3 + 7 * i;
    uint32_t code;
    std::memcpy(&code, entry, 4);
    card *c = card_at(f, entry[4], entry[5], entry[6]);
    if (!c) {
      ++tap->memory->tap_unmatched;
      continue;
    }
    for (int viewer = 0; viewer < 2; ++viewer) {
      if (!decktop && viewer != player) continue;
      tap->memory->cls[viewer][c->cardid] = kPublicZone;
      tap->memory->confirm_chain[viewer] = mix(mix(tap->memory->confirm_chain[viewer], code), entry[5]);
    }
  }
}

inline int face_down_on_field(const player_info &pl) {
  int n = 0;
  for (auto *zone : {&pl.list_mzone, &pl.list_szone})
    for (card *c : *zone) n += c && (c->current.position & POS_FACEDOWN);
  return n;
}

// Run at every decision of the true duel, before it is acted on.
inline void note_public(FmEnv *e, PublicMemory &m) {
  field *f = e->core()->game_field;
  for (int p = 0; p < 2; ++p) {
    auto &pl = f->player[p];
    int set_now = face_down_on_field(pl), deck_now = static_cast<int>(pl.list_main.size());
    // A card may have left this hand unseen. Which one is hidden, so every card tracked in that hand
    // forgets, wherever it is now, or the two possible worlds would remember differently.
    if (m.started && (set_now > m.previous_set[p] || deck_now > m.previous_deck[p]))
      for (int viewer = 0; viewer < 2; ++viewer)
        for (card *c : e->core()->cards)
          if (c->owner == p && m.cls[viewer][c->cardid] == kInHand) m.cls[viewer][c->cardid] = kUntracked;
    m.previous_set[p] = set_now;
    m.previous_deck[p] = deck_now;
  }
  m.started = true;
  for (int viewer = 0; viewer < 2; ++viewer) {
    auto &cls = m.cls[viewer];
    auto mark_public = [&](card *c) {
      if (c) cls[c->cardid] = kPublicZone;
    };
    for (int p = 0; p < 2; ++p) {
      auto &pl = f->player[p];
      for (card *c : pl.list_grave) mark_public(c);
      for (card *c : pl.list_remove)
        if (c->current.position & POS_FACEUP) mark_public(c);
      for (auto *zone : {&pl.list_mzone, &pl.list_szone})
        for (card *c : *zone)
          if (c && (c->current.position & POS_FACEUP)) mark_public(c);
      for (card *c : pl.list_extra)
        if (c->current.position & POS_FACEUP) mark_public(c);
      // A card entering a Deck joins the unknown order at once. Only a live reveal key pins a Deck slot.
      for (card *c : pl.list_main) cls[c->cardid] = kUntracked;
      for (card *c : pl.list_hand) {
        if (c->current.position & POS_FACEUP || e->revealed_has(viewer, p, LOCATION_HAND, c->current.sequence))
          cls[c->cardid] = kInHand;
        else if (cls[c->cardid] != kUntracked)
          cls[c->cardid] = kInHand;
      }
      for (auto *zone : {&pl.list_mzone, &pl.list_szone})
        for (card *c : *zone) {
          if (!c || !(c->current.position & POS_FACEDOWN)) continue;
          if (e->revealed_has(viewer, p, c->current.location, c->current.sequence) || cls[c->cardid] != kUntracked)
            cls[c->cardid] = kSetOnField;
        }
    }
  }
}

// ---------------------------------------------------------------------------------------------
struct WorldBaseline {
  std::vector<int> effects = std::vector<int>(4096, -1);  // effects on each object at the start of the duel
  std::vector<uint8_t> main_origin = std::vector<uint8_t>(4096, 0);
};

inline void capture_baseline(FmEnv *e, WorldBaseline &b) {
  for (card *c : e->core()->cards) {
    b.effects[c->cardid] = static_cast<int>(c->indexer.size());
    b.main_origin[c->cardid] = c->data.code && !c->is_extra_deck_monster();
  }
}

struct AssumedList {
  // The opponent's full Main Deck as the deal may assume it. Nothing else about their hidden cards is
  // an input. Build it from a real belief in play. In these tests it is the true list.
  std::vector<uint32_t> opponent_main;
};

enum class DealStatus : int {
  kOk = 0,
  kNotViewersDecision,  // the viewer is not the acting seat, or the duel has ended
  kListMismatch,        // the assumed list cannot be the one in play
  kHiddenObjectState,   // a hidden card carries rule state a slot swap would not transfer
  kCoreReference,       // the pending operation holds a hidden card by pointer
  kFaceDownBanished,    // an unseen opposing face-down banished card would bias the pool
  kForeignOwner,        // a hidden slot holds a card owned by the other player
  kReversedDeck,        // a face-up card inside a Deck: its slot's position would reveal the wrong card
  kPromptBinding,       // an own Deck option that cannot be re-bound
  kNoValidAssignment,   // the pool cannot fill the public slot types
  kCount
};

inline const char *deal_status_name(DealStatus s) {
  static const char *names[] = {"ok",           "not_viewers_decision", "list_mismatch", "hidden_object_state",
                                "core_reference", "facedown_banished",  "foreign_owner", "reversed_deck",
                                "prompt_binding", "no_valid_assignment"};
  return names[static_cast<int>(s)];
}

struct DealReport {
  DealStatus status = DealStatus::kNotViewersDecision;
  bool ok() const { return status == DealStatus::kOk; }
  int moved = 0;                         // slots dealt
  int known_pinned = 0;                  // hidden-zone cards the viewer knows, left in place
  int opponent_extra_not_redealt = 0;    // opposing Extra Deck cards, untouched
  int facedown_banished_not_redealt = 0; // face-down banished cards, untouched (the viewer's own or seen)
  int sets_rebuilt = 0;
  int deck_options_rebound = 0;
  int rows_scrubbed = 0;
};

struct DealPlan {
  std::vector<card *> holders;
  std::vector<card *> assignment;
};

inline bool selection_prompt(int msg) {
  return msg == MSG_SELECT_CARD || msg == MSG_SELECT_TRIBUTE || msg == MSG_SELECT_SUM ||
         msg == MSG_SELECT_UNSELECT_CARD || msg == MSG_SORT_CARD || msg == MSG_SELECT_COUNTER;
}

// Which of the given objects does the pending operation hold by pointer? Moving one would silently
// retarget that pointer, so the deal is refused. This asks about slots the core already holds, and
// the chain and battle state it reads is public, so it depends on no hidden identity. Stale lists
// from earlier private prompts are not read: select_cards stays populated after a prompt ends and
// reading it outside a live prompt is the leak the first R4 found.
struct HiddenSet {
  std::vector<void *> sorted;
  explicit HiddenSet(const std::vector<card *> &objects) : sorted(objects.begin(), objects.end()) {
    std::sort(sorted.begin(), sorted.end());
  }
  bool has(const void *p) const {
    return std::binary_search(sorted.begin(), sorted.end(), const_cast<void *>(p));
  }
};

inline const char *g_reference_kind = "";
inline char g_reference_detail[120] = "";
inline bool refuse_as(const char *kind) {
  g_reference_kind = kind;
  return true;
}

inline bool references_hidden(FmEnv *e, const HiddenSet &hidden) {
  duel *d = e->core();
  field *f = d->game_field;
  auto &core = f->core;
  auto any = [&](const card_vector &v) {
    for (card *c : v)
      if (hidden.has(c)) return true;
    return false;
  };
  if (selection_prompt(e->msg()) && (any(core.select_cards) || any(core.unselect_cards) || any(core.must_select_cards)))
    return refuse_as("live selection");
  for (card *c : {core.attacker, core.attack_target, core.summoning_card, core.limit_tuner, core.limit_link_card,
                  core.chain_attack_target})
    if (c && hidden.has(c)) return refuse_as("battle or summon pointer");
  auto has_group = [&](group *g) {
    if (g)
      for (card *c : g->container)
        if (hidden.has(c)) return true;
    return false;
  };
  auto chain_refers = [&](const chain &ch) {
    if (ch.triggering_effect && hidden.has(ch.triggering_effect->get_handler())) return true;
    if (has_group(ch.target_cards)) return true;
    if (has_group(ch.evt.event_cards)) return true;
    for (const auto &info : ch.opinfos)
      if (has_group(info.second.op_cards)) return true;
    return false;
  };
  for (const auto &ch : core.current_chain)
    if (chain_refers(ch)) return refuse_as("current chain");
  if (selection_prompt(e->msg()) || e->msg() == MSG_SELECT_CHAIN)
    for (const auto &ch : core.select_chains)
      if (chain_refers(ch)) return refuse_as("offered chain");
  for (const auto *list : {&core.continuous_chain, &core.solving_continuous, &core.sub_solving_continuous,
                           &core.delayed_continuous_tp, &core.delayed_continuous_ntp, &core.desrep_chain,
                           &core.new_fchain, &core.new_fchain_s, &core.new_ochain, &core.new_ochain_s,
                           &core.new_fchain_b, &core.new_ochain_b, &core.new_ochain_h, &core.new_chains})
    for (const auto &ch : *list)
      if (chain_refers(ch)) return refuse_as("pending chain list");
  for (const auto &kv : core.quick_f_chain)
    if (chain_refers(kv.second)) return refuse_as("quick chain");
  for (const auto *set : {&core.delayed_quick_tmp, &core.delayed_quick})
    for (const auto &entry : *set) {
      if (hidden.has(entry.first->get_handler())) return refuse_as("delayed quick");
      if (has_group(entry.second.event_cards)) return refuse_as("delayed quick");
    }
  // A processor unit's pointers are untyped in practice: the core casts cards into ptarget and peffect
  // as well. Each is compared as a card, and read as a group or effect only if it is a live one.
  auto untyped_refers = [&](void *p) {
    if (!p) return false;
    if (hidden.has(p)) return true;
    if (d->groups.count(static_cast<group *>(p)) && has_group(static_cast<group *>(p))) return true;
    if (d->effects.count(static_cast<effect *>(p)) && hidden.has(static_cast<effect *>(p)->get_handler())) return true;
    return false;
  };
  for (const auto *units : {&core.units, &core.subunits})
    for (const auto &unit : *units)
      for (void *p : {static_cast<void *>(unit.peffect), static_cast<void *>(unit.ptarget), unit.ptr1, unit.ptr2, unit.ptr3, unit.ptr4})
        if (untyped_refers(p)) return refuse_as("processor unit");
  // Groups held by Lua: an effect's label object, or a live script group of a running operation.
  for (group *g : d->sgroups)
    if (has_group(g)) return refuse_as("script group");
  // A label object may be something other than a userdata, which the core's own getter would dereference.
  // Reading the registry can grow Lua's stack, so this runs with the arena active.
  Scope arena(true);
  lua_State *L = d->lua->current_state;
  for (effect *peffect : d->effects) {
    if (!peffect->label_object) continue;
    luaL_checkstack(L, 1, nullptr);
    lua_rawgeti(L, LUA_REGISTRYINDEX, peffect->label_object);
    void **object = static_cast<void **>(lua_touserdata(L, -1));
    void *held = object ? *object : nullptr;
    lua_pop(L, 1);
    if (!held) continue;
    // Scripts wire a card's own effects together at load, and an effect may label its own card. Those
    // references move with the card. A label on another card's effect, or a group, would go stale.
    if (d->effects.count(static_cast<effect *>(held))) {
      if (hidden.has(static_cast<effect *>(held)->get_handler()) && static_cast<effect *>(held)->get_handler() != peffect->get_handler()) return refuse_as("label effect");
      continue;
    }
    if (held == peffect->get_handler()) continue;
    // A label held as a group is left alone. Summon procedures keep their materials this way (EFFECT_SPSUMMON_PROC,
    // seen on Machina Fortress and others), and the group goes stale when the Summon ends, so refusing on it
    // refused worlds over a card that merely once was a material. A group a running effect is using is
    // caught above, through the chain or the processor unit that holds it.
    if (d->groups.count(static_cast<group *>(held))) continue;
    if (hidden.has(held)) {
      snprintf(g_reference_detail, sizeof g_reference_detail, "effect of %u code %u labels card %u", peffect->get_handler()->data.code, peffect->code,
               static_cast<card *>(held)->data.code);
      return refuse_as("label object");
    }
  }
  return false;
}

// Rule state that belongs to the object and a slot swap would not transfer. Reading it reads a hidden
// card, which is why the only thing this may do is refuse the whole deal.
inline bool carries_object_state(card *c, const WorldBaseline &baseline) {
  if (!c->relations.empty() || !c->counters.empty() || c->equiping_target || !c->effect_target_cards.empty() ||
      !c->effect_target_owner.empty() || !c->relate_effect.empty() || !c->equiping_cards.empty() ||
      !c->xyz_materials.empty() || c->overlay_target || !c->material_cards.empty() || !c->battled_cards.empty() ||
      !c->attacked_cards.empty() || !c->announced_cards.empty() || c->assume_type)
    return true;
  return baseline.effects[c->cardid] >= 0 && static_cast<int>(c->indexer.size()) != baseline.effects[c->cardid];
}

inline uint32_t type_of_code(uint32_t code) { return ygopro::cards_data_.at(code).type; }
enum SlotClass : uint8_t { kFree = 0, kMonsterSlot = 1, kBackRowSlot = 2, kFieldSlot = 3 };

inline SlotClass class_of_slot(const card *slot) {
  if (slot->current.location == LOCATION_MZONE) return kMonsterSlot;
  if (slot->current.location == LOCATION_SZONE) return slot->current.sequence == 5 ? kFieldSlot : kBackRowSlot;
  return kFree;
}
inline bool code_fits(uint32_t code, SlotClass cls) {
  uint32_t type = type_of_code(code);
  switch (cls) {
    case kMonsterSlot: return type & TYPE_MONSTER;
    case kBackRowSlot: return !(type & TYPE_MONSTER) && !(type & TYPE_FIELD);
    case kFieldSlot: return type & TYPE_FIELD;
    default: return true;
  }
}

// The slots that are dealt and what the viewer may assume about them, read in one canonical order.
struct HiddenSlots {
  std::vector<card *> own_deck;     // viewer's Deck
  std::vector<card *> opp_slots;    // opposing Deck, then hand, monster zones, back row, by sequence
  DealStatus refuse = DealStatus::kOk;
  int known_pinned = 0, opp_extra = 0, facedown_banished = 0;
};

inline HiddenSlots collect_hidden(FmEnv *e, int viewer, const PublicMemory &m, const WorldBaseline &baseline) {
  HiddenSlots h;
  field *f = e->core()->game_field;
  const auto &cls = m.cls[viewer];
  int opp = 1 - viewer;
  auto &mine = f->player[viewer];
  auto &theirs = f->player[opp];
  auto deck_pinned = [&](int controller, card *c) {
    return e->revealed_has(viewer, controller, LOCATION_DECK, c->current.sequence);
  };
  if (f->core.deck_reversed) h.refuse = DealStatus::kReversedDeck;
  for (card *c : mine.list_main) {
    if (c->current.position & POS_FACEUP) h.refuse = DealStatus::kReversedDeck;
    if (deck_pinned(viewer, c)) {
      ++h.known_pinned;
      continue;
    }
    h.own_deck.push_back(c);
  }
  auto add_opp = [&](card *c, bool known) {
    if (known) {
      ++h.known_pinned;
      return;
    }
    if (c->owner != opp) h.refuse = DealStatus::kForeignOwner;
    h.opp_slots.push_back(c);
  };
  for (card *c : theirs.list_main) {
    if (c->current.position & POS_FACEUP) h.refuse = DealStatus::kReversedDeck;
    add_opp(c, deck_pinned(opp, c));
  }
  for (card *c : theirs.list_hand) add_opp(c, cls[c->cardid] != kUntracked || (c->current.position & POS_FACEUP));
  for (auto *zone : {&theirs.list_mzone, &theirs.list_szone})
    for (card *c : *zone)
      if (c && (c->current.position & POS_FACEDOWN)) add_opp(c, cls[c->cardid] != kUntracked);
  h.opp_extra = 0;
  for (card *c : theirs.list_extra) h.opp_extra += !(c->current.position & POS_FACEUP);
  for (int p = 0; p < 2; ++p)
    for (card *c : f->player[p].list_remove)
      if (c->current.position & POS_FACEDOWN) {
        ++h.facedown_banished;
        if (c->owner == opp && cls[c->cardid] == kUntracked && baseline.main_origin[c->cardid])
          h.refuse = DealStatus::kFaceDownBanished;
      }
  return h;
}

// Rewrite the pending own Deck prompt for the new Deck order. The core keeps its candidates as objects
// and takes the option's index as its response, so only the environment's bound positions go stale. Each
// bound option re-binds by production's own rule: same code, then face state, then already shown.
struct DeckBinding {
  uint8_t old_sequence;
  uint32_t code;
  bool face_up;
};

struct DealAccess {
  static std::vector<DeckBinding> capture_bindings(FmEnv *e, int viewer) {
    std::vector<std::pair<int, uint8_t>> by_rank;
    for (const auto &kv : e->deck_bind_rank_) by_rank.push_back({kv.second, kv.first});
    std::sort(by_rank.begin(), by_rank.end());
    std::vector<DeckBinding> out;
    auto &deck = e->core()->game_field->player[viewer].list_main;
    for (const auto &entry : by_rank) {
      if (entry.second >= deck.size()) continue;
      card *c = deck[entry.second];
      out.push_back({entry.second, c->data.code, bool(c->current.position & POS_FACEUP)});
    }
    return out;
  }

  // True if any offered action names an own Deck position that binding does not cover. No own Deck
  // option outside MSG_SELECT_CARD and MSG_SELECT_UNSELECT_CARD is rebindable.
  static bool unbound_deck_options(FmEnv *e) {
    std::vector<std::string> specs;
    for (const auto &a : e->legal_actions_) specs.push_back(a.spec_);
    specs.insert(specs.end(), e->decision_.selected.begin(), e->decision_.selected.end());
    specs.insert(specs.end(), e->decision_.mandatory.begin(), e->decision_.mandatory.end());
    for (const auto &spec : specs) {
      if (spec.empty() || spec[0] == 'o') continue;
      auto [loc, seq, pos] = ygopro::spec_to_ls(spec);
      if (loc == LOCATION_DECK && !e->deck_bind_rank_.count(seq)) return true;
    }
    return false;
  }

  static int rebind(FmEnv *e, int viewer, const std::vector<DeckBinding> &bindings) {
    if (bindings.empty()) return 0;
    Scope s(true);
    struct Copy {
      uint8_t sequence;
      bool face_up, revealed;
    };
    std::map<uint32_t, std::vector<Copy>> unbound;
    auto &deck = e->core()->game_field->player[viewer].list_main;
    for (size_t i = 0; i < deck.size(); ++i)
      unbound[deck[i]->data.code].push_back(
          {uint8_t(i), bool(deck[i]->current.position & POS_FACEUP), e->revealed_has(viewer, viewer, LOCATION_DECK, int(i))});
    std::map<std::string, std::string> rename;
    std::map<uint8_t, int> ranks;
    for (const auto &b : bindings) {
      auto &copies = unbound[b.code];
      if (copies.empty()) throw std::runtime_error("re-bind: no Deck copy left");
      auto cost = [&](const Copy &c) { return std::make_pair(c.face_up != b.face_up, !c.revealed); };
      auto best = std::min_element(copies.begin(), copies.end(),
                                   [&](const Copy &x, const Copy &y) { return cost(x) < cost(y); });
      rename[ygopro::ls_to_spec(LOCATION_DECK, b.old_sequence, 0, false)] = ygopro::ls_to_spec(LOCATION_DECK, best->sequence, 0, false);
      ranks.emplace(best->sequence, int(ranks.size()));
      copies.erase(best);
    }
    auto apply = [&](std::string &spec) {
      auto it = rename.find(spec);
      if (it != rename.end()) spec = it->second;
    };
    int rewritten = 0;
    for (auto &a : e->legal_actions_) {
      auto it = rename.find(a.spec_);
      if (it != rename.end()) {
        a.spec_ = it->second;
        ++rewritten;
      }
    }
    for (auto &spec : e->decision_.candidates) apply(spec);
    for (auto &spec : e->decision_.selected) apply(spec);
    for (auto &spec : e->decision_.mandatory) apply(spec);
    for (auto &spec : e->ms_specs_) apply(spec);
    if (!e->ms_spec2idx_.empty()) {
      ankerl::unordered_dense::map<std::string, int> renamed;
      for (const auto &kv : e->ms_spec2idx_) {
        std::string spec = kv.first;
        apply(spec);
        renamed[spec] = kv.second;
      }
      e->ms_spec2idx_ = std::move(renamed);
    }
    e->deck_bind_rank_ = std::move(ranks);
    return rewritten;
  }

  // The other seat's history ring holds private identities of the true world, such as what it set or
  // searched. Inside a sampled world that seat acts on what the sampled world dealt it, and a network
  // evaluating it must not read the true cards. Rows not public when written lose their card id.
  static int scrub_other_seat_history(FmEnv *e, int viewer, const PublicMemory &m) {
    int seat = 1 - viewer, scrubbed = 0;
    auto &ring = seat == 0 ? e->history_actions_1_ : e->history_actions_2_;
    for (int row = 0; row < e->n_history_actions_; ++row) {
      if (m.row_public[seat][row]) continue;
      if (uint8_t(ring(row, 1)) || uint8_t(ring(row, 2))) ++scrubbed;
      ring(row, 1) = uint8_t(0);
      ring(row, 2) = uint8_t(0);
    }
    return scrubbed;
  }

  static void reseed(FmEnv *e, uint32_t core_seed, uint32_t env_seed) {
    e->core()->random.reset(core_seed);
    e->gen_.seed(env_seed);
  }

  // Own Deck options, shown as the card they offer: a stable description across Deck orders.
  static std::string canonical_spec(FmEnv *e, const LegalAction &a) {
    if (a.spec_.empty() || a.spec_[0] == 'o') return a.spec_;
    auto [loc, seq, pos] = ygopro::spec_to_ls(a.spec_);
    if (loc != LOCATION_DECK) return a.spec_;
    auto &deck = e->core()->game_field->player[e->to_play_].list_main;
    return "D" + std::to_string(seq < deck.size() ? deck[seq]->data.code : 0);
  }

  // Which object a chosen action names, or null if it names none.
  static card *object_of(FmEnv *e, const LegalAction &a) {
    if (a.spec_.empty()) return nullptr;
    bool opponent = a.spec_[0] == 'o';
    auto [loc, seq, pos] = ygopro::spec_to_ls(a.spec_.substr(opponent ? 1 : 0));
    return card_at(e->core()->game_field, opponent ? 1 - e->to_play_ : e->to_play_, loc, seq);
  }
  static int ring_cursor(FmEnv *e, int seat) { return seat == 0 ? e->ha_p_1_ : e->ha_p_2_; }
  static int ring_size(FmEnv *e) { return e->n_history_actions_; }
};

// Marks the history rows the coming step writes. A chosen card public to the other seat keeps its id
// in that seat's sampled view. Every other new row, such as a lone option answered automatically, is
// marked private: conservative, and safe.
struct RowMarker {
  int seat = 0, cursor[2] = {0, 0};
  bool chosen_public = false, writes_row = false;
};

inline RowMarker before_true_step(FmEnv *e, const PublicMemory &m, int option) {
  RowMarker marker;
  marker.seat = e->to_play();
  marker.cursor[0] = DealAccess::ring_cursor(e, 0);
  marker.cursor[1] = DealAccess::ring_cursor(e, 1);
  const LegalAction &a = e->actions()[option];
  marker.writes_row = a.act_ != ygopro::ActionAct::Cancel;
  card *c = DealAccess::object_of(e, a);
  // No card in the row, or a card the other seat already tracks. A row with no spec can still carry an id,
  // as a position prompt does for the card being placed, which may go face-down, so that counts as private.
  marker.chosen_public = (a.spec_.empty() && a.cid_ == 0) || (c && m.cls[1 - marker.seat][c->cardid] != kUntracked);
  return marker;
}

inline void after_true_step(FmEnv *e, PublicMemory &m, const RowMarker &marker) {
  int n = DealAccess::ring_size(e);
  for (int seat = 0; seat < 2; ++seat) {
    int at = marker.cursor[seat], now = DealAccess::ring_cursor(e, seat);
    bool first = seat == marker.seat && marker.writes_row;
    while (at != now) {
      at = at == 0 ? n - 1 : at - 1;
      m.row_public[seat][at] = first && marker.chosen_public;
      first = false;
    }
  }
}

// The determinizer. Refusals produce no world and leave the duel untouched.
inline DealReport determinize(FmEnv *e, int viewer, uint64_t seed, const PublicMemory &m, const WorldBaseline &baseline,
                              const AssumedList &assumed, bool inherit_cardid = true, DealPlan *out = nullptr) {
  Scope off(false);
  DealReport rep;
  if (e->done() || !e->started() || e->to_play() != viewer) return rep;
  HiddenSlots h = collect_hidden(e, viewer, m, baseline);
  rep.known_pinned = h.known_pinned;
  rep.opponent_extra_not_redealt = h.opp_extra;
  rep.facedown_banished_not_redealt = h.facedown_banished;
  if (h.refuse != DealStatus::kOk) {
    rep.status = h.refuse;
    return rep;
  }
  if (DealAccess::unbound_deck_options(e)) {
    rep.status = DealStatus::kPromptBinding;
    return rep;
  }
  // Only opposing slots: those cards change identity under the pointer. The viewer's own Deck is the same
  // set of objects in another order, so anything holding one of those objects still holds the right card.
  if (references_hidden(e, HiddenSet(h.opp_slots))) {
    rep.status = DealStatus::kCoreReference;
    return rep;
  }
  for (card *c : h.opp_slots)
    if (carries_object_state(c, baseline)) {
      rep.status = DealStatus::kHiddenObjectState;
      return rep;
    }

  // The pool: the assumed list, minus every opposing Main Deck object outside the dealt slots. Those
  // objects are public, or banished face-down and seen, so their codes are known.
  field *f = e->core()->game_field;
  std::unordered_set<card *> in_slots(h.opp_slots.begin(), h.opp_slots.end());
  std::map<uint32_t, int> pool;
  for (uint32_t code : assumed.opponent_main) ++pool[code];
  int opp = 1 - viewer;
  for (card *c : e->core()->cards) {
    if (c->owner != opp || !baseline.main_origin[c->cardid] || in_slots.count(c)) continue;
    if (--pool[c->data.code] < 0) {
      rep.status = DealStatus::kListMismatch;
      return rep;
    }
  }
  // Objects by code. A true list makes these the same multiset as the pool. Reading the codes here
  // chooses which object carries a code, and can only refuse.
  std::map<uint32_t, std::vector<card *>> objects;
  for (card *c : h.opp_slots) objects[c->data.code].push_back(c);
  for (auto &kv : objects) {
    if (pool[kv.first] != static_cast<int>(kv.second.size())) {
      rep.status = DealStatus::kListMismatch;
      return rep;
    }
    std::sort(kv.second.begin(), kv.second.end(), [](card *a, card *b) { return a->cardid < b->cardid; });
  }
  for (const auto &kv : pool)
    if (kv.second != 0 && !objects.count(kv.first)) {
      rep.status = DealStatus::kListMismatch;
      return rep;
    }
  std::vector<uint32_t> codes;
  for (const auto &kv : pool)
    for (int i = 0; i < kv.second; ++i) codes.push_back(kv.first);

  uint64_t x = seed;
#ifdef FM_MUTANTS
  if (g_mutant == Mutant::LeakSeed)
    for (card *c : f->player[opp].list_hand) x = mix(x, c->data.code);
  if (g_mutant == Mutant::LeakOrder) {
    codes.clear();
    for (card *c : h.opp_slots) codes.push_back(c->data.code);
  }
#endif
  // Constrained slots first, each drawing uniformly from the codes that fit, then the rest shuffled.
  // Every valid assignment of the pool is equally likely, and nothing is rejected and retried.
  std::vector<int> free_slots;
  std::vector<uint32_t> dealt(h.opp_slots.size(), 0);
  for (size_t i = 0; i < h.opp_slots.size(); ++i) {
    SlotClass slot_class = class_of_slot(h.opp_slots[i]);
    if (slot_class == kFree) {
      free_slots.push_back(static_cast<int>(i));
      continue;
    }
    std::vector<int> fits;
    for (size_t k = 0; k < codes.size(); ++k)
      if (code_fits(codes[k], slot_class)) fits.push_back(static_cast<int>(k));
    if (fits.empty()) {
      rep.status = DealStatus::kNoValidAssignment;
      return rep;
    }
    int pick = fits[splitmix(x) % fits.size()];
    dealt[i] = codes[pick];
    codes.erase(codes.begin() + pick);
  }
  for (int i = static_cast<int>(codes.size()) - 1; i > 0; --i) std::swap(codes[i], codes[splitmix(x) % (i + 1)]);
  for (size_t i = 0; i < free_slots.size(); ++i) dealt[free_slots[i]] = codes[i];
#ifdef FM_MUTANTS
  if (g_mutant == Mutant::LeakHand) {
    // The planted leak: each opposing hand slot keeps its true card.
    for (size_t i = 0; i < h.opp_slots.size(); ++i) {
      if (h.opp_slots[i]->current.location != LOCATION_HAND) continue;
      uint32_t truth = h.opp_slots[i]->data.code;
      for (size_t k = 0; k < dealt.size(); ++k)
        if (dealt[k] == truth && (k >= i || h.opp_slots[k]->current.location != LOCATION_HAND)) {
          std::swap(dealt[k], dealt[i]);
          break;
        }
    }
  }
#endif
  std::vector<card *> holders(h.opp_slots), assignment;
  assignment.reserve(holders.size() + h.own_deck.size());
  std::map<uint32_t, size_t> used;
  for (uint32_t code : dealt) assignment.push_back(objects[code][used[code]++]);

  // The viewer's Deck: its multiset is the viewer's own knowledge. Sorted first, so the deal depends on
  // that multiset and the seed and not on the order the true Deck happens to be in.
  std::vector<card *> own(h.own_deck);
  std::sort(own.begin(), own.end(), [](card *a, card *b) {
    return a->data.code != b->data.code ? a->data.code < b->data.code : a->cardid < b->cardid;
  });
  for (int i = static_cast<int>(own.size()) - 1; i > 0; --i) std::swap(own[i], own[splitmix(x) % (i + 1)]);
  holders.insert(holders.end(), h.own_deck.begin(), h.own_deck.end());
  assignment.insert(assignment.end(), own.begin(), own.end());

  std::vector<DeckBinding> bindings = DealAccess::capture_bindings(e, viewer);
  rep.sets_rebuilt = apply_assignment(e->core(), holders, assignment, inherit_cardid);
  rep.deck_options_rebound = DealAccess::rebind(e, viewer, bindings);
  rep.rows_scrubbed = DealAccess::scrub_other_seat_history(e, viewer, m);
  uint32_t core_seed = static_cast<uint32_t>(splitmix(x)), env_seed = static_cast<uint32_t>(splitmix(x));
  DealAccess::reseed(e, core_seed, env_seed);
  rep.status = DealStatus::kOk;
  rep.moved = static_cast<int>(holders.size());
  if (out) {
    out->holders = holders;
    out->assignment = assignment;
  }
  return rep;
}

// Every rollback goes through here. With FM_MUTANTS it can be made to miss one piece of state, to prove
// the snapshot check would notice. Without it this is exactly a copy of the arena.
#ifdef FM_MUTANTS
struct MutantAccess {
  static void restore(FmEnv *e, const Snapshot &snap) {
    Scope off(false);
    std::vector<uint8_t> ring[2];
    int cursor[2] = {e->ha_p_1_, e->ha_p_2_};
    std::vector<std::string> revealed;
    std::map<uint8_t, int> ranks;
    for (int seat = 0; seat < 2; ++seat) {
      auto &r = seat == 0 ? e->history_actions_1_ : e->history_actions_2_;
      auto *p = static_cast<uint8_t *>(r.Data());
      ring[seat].assign(p, p + r.size * r.element_size);
    }
    revealed.assign(e->revealed_.begin(), e->revealed_.end());
    ranks = e->deck_bind_rank_;
    snapshot_restore(snap);
    switch (g_mutant) {
      case Mutant::RollbackHistory:
        for (int seat = 0; seat < 2; ++seat) {
          auto &r = seat == 0 ? e->history_actions_1_ : e->history_actions_2_;
          std::memcpy(r.Data(), ring[seat].data(), ring[seat].size());
        }
        break;
      case Mutant::RollbackCursor:
        e->ha_p_1_ = cursor[0];
        e->ha_p_2_ = cursor[1];
        break;
      // Abandoned branches usually end with this state empty, so simply keeping it would plant nothing.
      // These leave behind what a branch could have left, every time.
      case Mutant::RollbackReveal: {
        Scope arena(true);
        for (const auto &key : revealed) e->revealed_.insert(key.c_str());
        e->revealed_.insert(e->reveal_key(e->to_play_, 1 - e->to_play_, LOCATION_HAND, 0).c_str());
        break;
      }
      case Mutant::RollbackBind: {
        Scope arena(true);
        for (const auto &kv : ranks) e->deck_bind_rank_.emplace(kv.first, kv.second);
        e->deck_bind_rank_.emplace(uint8_t(0), 99);
        break;
      }
      case Mutant::RollbackRng:
        if (e->started()) e->core()->random.rand();
        break;
      default:
        break;
    }
  }
};
#endif

inline void restore_world(FmEnv *e, const Snapshot &snap) {
#ifdef FM_MUTANTS
  if (g_mutant >= Mutant::RollbackHistory) {
    MutantAccess::restore(e, snap);
    return;
  }
#endif
  (void)e;
  snapshot_restore(snap);
}

// Mechanical consistency of the core's card lists.
inline bool invariants_ok(FmEnv *e) {
  if (!e->started()) return true;
  field *f = e->core()->game_field;
  std::unordered_set<card *> visited;
  Scope off(false);
  bool ok = true;
  auto check = [&](const card_vector &v, int p, uint8 loc, bool sparse) {
    for (size_t i = 0; i < v.size(); ++i) {
      card *c = v[i];
      if (!c) {
        if (!sparse) ok = false;
        continue;
      }
      if (c->current.location != loc || c->current.controler != p || c->current.sequence != i ||
          !visited.insert(c).second)
        ok = false;
    }
  };
  for (int p = 0; p < 2; ++p) {
    auto &pl = f->player[p];
    check(pl.list_main, p, LOCATION_DECK, false);
    check(pl.list_hand, p, LOCATION_HAND, false);
    check(pl.list_mzone, p, LOCATION_MZONE, true);
    check(pl.list_szone, p, LOCATION_SZONE, true);
    check(pl.list_grave, p, LOCATION_GRAVE, false);
    check(pl.list_remove, p, LOCATION_REMOVED, false);
    check(pl.list_extra, p, LOCATION_EXTRA, false);
  }
  return ok;
}
}  // namespace fm
