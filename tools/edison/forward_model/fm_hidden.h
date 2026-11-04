// Hidden-state re-dealing and position construction experiments.
//
// Everything here runs inside the simulator on purpose. The rule enforced for a
// real engine is one-directional: a sampler may be given only the public view
// (slot layout, public history, an assumed decklist) and may overwrite hidden
// state; it must not read hidden identities. `public_view` and `deal_codes` take
// only public inputs. The ground-truth comparisons (natural deals) read true state
// because they are the test oracle, not because a sampler may.
#pragma once
#include <array>

#include "fm_env.h"

namespace fm {

struct Desc {
  std::string key;
  int act = 0;
};

inline std::string descriptor(const LegalAction &a) {
  return a.spec_ + "|" + std::to_string(static_cast<int>(a.act_)) + "|" +
         std::to_string(static_cast<int>(a.phase_)) + "|" + std::to_string(a.finish_) + "|" +
         std::to_string(a.position_) + "|" + std::to_string(a.effect_) + "|" +
         std::to_string(a.number_) + "|" + std::to_string(static_cast<int>(a.place_)) + "|" +
         std::to_string(a.attribute_);
}

// -- Slot permutation ---------------------------------------------------------
// A slot is a place a hidden card can be: a deck index, a hand index, a set zone.
// Re-dealing keeps the slot's rule state (how the slot came to be, set-turn status,
// field ids, position) and changes which card object occupies it.
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

// assignment[i] is the object that takes over the slot currently held by holders[i].
inline void apply_assignment(duel *d, const std::vector<card *> &holders,
                             const std::vector<card *> &assignment, bool inherit_cardid) {
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
  for (card *c : holders) c->apply_field_effect();
  f->refresh_player_info(0);
  f->refresh_player_info(1);
}

// -- Public view --------------------------------------------------------------
// Identity memory: which card objects have ever been public. Object identity is
// public here because an observer can follow a public card through public moves.
struct PublicMemory {
  std::vector<uint8_t> seen = std::vector<uint8_t>(4096, 0);
};

inline void note_public(FmEnv *e, PublicMemory &m, int viewer) {
  field *f = e->core()->game_field;
  auto mark = [&](card *c) {
    if (c) m.seen[c->cardid] = 1;
  };
  for (int p = 0; p < 2; ++p) {
    auto &pl = f->player[p];
    for (card *c : pl.list_grave) mark(c);
    for (card *c : pl.list_remove) mark(c);
    for (card *c : pl.list_mzone)
      if (c && (c->current.position & POS_FACEUP)) mark(c);
    for (card *c : pl.list_szone)
      if (c && (c->current.position & POS_FACEUP)) mark(c);
    for (card *c : pl.list_hand)
      if (c->current.position & POS_FACEUP) mark(c);
    for (card *c : pl.list_extra)
      if (c->current.position & POS_FACEUP) mark(c);
  }
  // Production's own explicit-reveal memory for what this viewer was shown.
  for (int v = 0; v < 2; ++v) {
    if (v != viewer) continue;
    auto &opp = f->player[1 - v];
    for (card *c : opp.list_hand)
      if (e->revealed_has(v, 1 - v, LOCATION_HAND, c->current.sequence)) mark(c);
  }
}

// Objects the core may be holding by pointer in the pending operation. Moving one
// would silently retarget it, so such objects stay where they are.
//
// select_cards and friends stay populated after a prompt finishes. Reading them
// outside a live selection prompt made the pin set depend on the opponent's earlier
// private candidates: test R4 caught that as a true-state leak. Only the viewer's own
// pending selection, which the viewer can see, may be consulted.
inline bool selection_prompt(int msg) {
  return msg == MSG_SELECT_CARD || msg == MSG_SELECT_TRIBUTE || msg == MSG_SELECT_SUM ||
         msg == MSG_SELECT_UNSELECT_CARD || msg == MSG_SORT_CARD || msg == MSG_SELECT_COUNTER;
}

inline bool referenced_by_core(field *f, card *c, int msg) {
  auto has = [&](const card_vector &v) { return std::find(v.begin(), v.end(), c) != v.end(); };
  auto &core = f->core;
  if (selection_prompt(msg) &&
      (has(core.select_cards) || has(core.unselect_cards) || has(core.must_select_cards)))
    return true;
  if (c == core.attacker || c == core.attack_target || c == core.summoning_card) return true;
  for (const auto &ch : core.current_chain) {
    if (ch.triggering_effect && ch.triggering_effect->get_handler() == c) return true;
    if (ch.target_cards && ch.target_cards->container.count(c)) return true;
  }
  return false;
}

// True when an object carries state a slot swap would not transfer.
inline bool carries_object_state(card *c, const std::vector<int> &baseline_effects) {
  if (!c->relations.empty() || !c->counters.empty() || c->equiping_target ||
      !c->effect_target_cards.empty() || !c->effect_target_owner.empty() ||
      !c->relate_effect.empty() || !c->equiping_cards.empty() || !c->xyz_materials.empty() ||
      c->overlay_target)
    return true;
  return baseline_effects[c->cardid] >= 0 && static_cast<int>(c->indexer.size()) != baseline_effects[c->cardid];
}

struct DealReport {
  bool ok = false;
  int moved = 0, pinned_seen = 0, pinned_referenced = 0, pinned_state = 0, type_retries = 0;
};

inline bool is_monster(card *c) { return c->data.type & TYPE_MONSTER; }

// Slots in canonical order. What is in them is read only to find the objects to
// move; the codes dealt come from `deck_codes`, public seen state, and the seed.
struct DealPlan {
  std::vector<card *> holders;
  std::vector<card *> assignment;
};

// The determinizer. `known_list[p]` is the assumed full main deck of p: for the
// viewer it is real knowledge, for the opponent it is a belief (here the true
// fixture decklist; a real engine would sample it).
inline DealReport determinize(FmEnv *e, int viewer, uint64_t seed, const PublicMemory &m,
                              const std::vector<int> &baseline_effects, bool inherit_cardid,
                              DealPlan *out = nullptr) {
  DealReport rep;
  field *f = e->core()->game_field;
  uint64_t x = seed;
  std::vector<card *> holders, assignment;
  for (int p : {viewer, 1 - viewer}) {
    auto &pl = f->player[p];
    std::vector<card *> slots;   // slots whose occupant is unknown to the viewer
    std::vector<card *> deck_seen;
    std::vector<int> deck_slot_index;
    for (card *c : pl.list_main) {
      if (referenced_by_core(f, c, e->msg())) { ++rep.pinned_referenced; continue; }
      if (carries_object_state(c, baseline_effects)) { ++rep.pinned_state; continue; }
      slots.push_back(c);
    }
    size_t n_deck = slots.size();
    if (p != viewer) {
      for (card *c : pl.list_hand) {
        if (m.seen[c->cardid] || (c->current.position & POS_FACEUP)) { ++rep.pinned_seen; continue; }
        if (referenced_by_core(f, c, e->msg())) { ++rep.pinned_referenced; continue; }
        if (carries_object_state(c, baseline_effects)) { ++rep.pinned_state; continue; }
        slots.push_back(c);
      }
      for (auto *zone : {&pl.list_mzone, &pl.list_szone}) {
        for (card *c : *zone) {
          if (!c || (c->current.position & POS_FACEUP)) continue;
          if (m.seen[c->cardid]) { ++rep.pinned_seen; continue; }
          if (referenced_by_core(f, c, e->msg())) { ++rep.pinned_referenced; continue; }
          if (carries_object_state(c, baseline_effects)) { ++rep.pinned_state; continue; }
          slots.push_back(c);
        }
      }
    }
    // Objects, sorted by code so the deal depends on the multiset, not the layout.
    // A publicly seen object now in the deck may be anywhere in the deck but not
    // in hand; it is dealt first, into deck slots only.
    std::vector<card *> objects(slots);
    std::stable_sort(objects.begin(), objects.end(), [](card *a, card *b) {
      return a->data.code < b->data.code;
    });
    std::vector<card *> seen_objs, free_objs;
    for (card *c : objects) (m.seen[c->cardid] ? seen_objs : free_objs).push_back(c);
    bool placed = false;
    std::vector<card *> dealt(slots.size());
    for (int attempt = 0; attempt < 500 && !placed; ++attempt) {
      std::vector<int> order(slots.size());
      std::iota(order.begin(), order.end(), 0);
      for (int i = static_cast<int>(order.size()) - 1; i > 0; --i)
        std::swap(order[i], order[splitmix(x) % (i + 1)]);
      std::fill(dealt.begin(), dealt.end(), nullptr);
      size_t oi = 0;
      // Seen objects take the first free deck slots in the shuffled order.
      for (card *c : seen_objs) {
        while (oi < order.size() && static_cast<size_t>(order[oi]) >= n_deck) ++oi;
        if (oi == order.size()) break;
        dealt[order[oi]] = c;
        order.erase(order.begin() + oi);
      }
      size_t fi = 0;
      for (int idx : order) {
        if (fi < free_objs.size()) dealt[idx] = free_objs[fi++];
      }
      placed = true;
      for (size_t i = n_deck; i < slots.size(); ++i) {
        card *slot = slots[i];
        if (!dealt[i]) { placed = false; break; }
        // A set monster zone is public as "a monster"; same for spell/trap zones.
        if (slot->current.location == LOCATION_MZONE && !is_monster(dealt[i])) placed = false;
        if (slot->current.location == LOCATION_SZONE && is_monster(dealt[i])) placed = false;
      }
      if (!placed) ++rep.type_retries;
    }
    if (!placed) return rep;
    holders.insert(holders.end(), slots.begin(), slots.end());
    assignment.insert(assignment.end(), dealt.begin(), dealt.end());
  }
  apply_assignment(e->core(), holders, assignment, inherit_cardid);
  e->core()->random.reset(static_cast<uint32_t>(splitmix(x)));
  rep.ok = true;
  rep.moved = static_cast<int>(holders.size());
  if (out) {
    out->holders = holders;
    out->assignment = assignment;
  }
  return rep;
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

// -- Runs ---------------------------------------------------------------------
constexpr int kChainCap = 4000;
struct CanonRun {
  std::array<uint64_t, kChainCap> chain;
  int length = 0, winner = -1, reason = -1, done = 0;
  uint64_t digest = 0;
};

// Order-independent policy: the card-list order inside a prompt follows cardid,
// which encodes the original shuffle, so comparing worlds needs a canonical order.
inline void run_canonical(FmEnv *e, uint64_t seed, int cap, CanonRun &out, bool raw) {
  out.length = 0;
  out.digest = 0;
  std::vector<std::pair<std::string, int>> keys;
  for (;;) {
    uint64_t h = 0x77;
    int n = static_cast<int>(e->actions().size());
    {
      Scope off(false);
      keys.clear();
      for (int i = 0; i < n; ++i) keys.emplace_back(descriptor(e->actions()[i]), i);
      if (!raw) std::stable_sort(keys.begin(), keys.end());
      h = mix(h, e->done());
      h = mix(h, e->to_play());
      h = mix(h, e->msg());
      h = mix(h, e->turn());
      h = mix(h, e->phase());
      for (const auto &k : keys)
        for (char ch : k.first) h = mix(h, static_cast<unsigned char>(ch));
    }
    if (out.length < kChainCap) out.chain[out.length] = h;
    out.digest = mix(out.digest, h);
    ++out.length;
    if (e->done() || out.length > cap) break;
    int pick = keys[splitmix(seed) % n].second;
    Scope s(true);
    e->act(pick);
  }
  out.done = e->done();
  out.winner = e->done() ? e->winner() : -1;
  out.reason = e->done() ? e->win_reason() : -1;
  out.digest = mix(out.digest, out.winner + 1000 * out.reason);
}

inline int first_divergence(const CanonRun &a, const CanonRun &b) {
  int n = std::min({a.length, b.length, kChainCap});
  for (int i = 0; i < n; ++i)
    if (a.chain[i] != b.chain[i]) return i;
  return a.length == b.length ? -1 : n;
}

struct RedealOut {
  long positions;
  // R1 deck reorder
  long r1_playouts, r1_done, r1_invariant_bad, r1_diag, r1_exceptions, r1_changed, r1_repeat_bad;
  // R2 full determinization
  long r2_attempts, r2_ok, r2_playouts, r2_done, r2_invariant_bad, r2_diag, r2_exceptions,
      r2_repeat_bad, r2_changed, r2_moved, r2_pinned_seen, r2_pinned_ref, r2_pinned_state,
      r2_type_retries;
  double r2_us;
  // R3 natural alternate history
  long r3_samples, r3_accept, r3_reject_prompt, r3_reject_offered, r3_reject_public, r3_reject_action, r3_reject_end,
      r3_exceptions;
  long r3_index_samples, r3_index_survive;
  double r3_accept_us, r3_accept_decisions;
  long r3_layout_same, r3_board_match, r3_canon_match, r3_canon_match_cardid, r3_raw_match_cardid, r3_winner_match,
      r3_compare_exceptions;
  long r3_div_lt10, r3_div_lt100;
  long bucket_samples[3], bucket_accept[3];
  // R4 non-interference
  long r4_pairs, r4_slots_equal, r4_canon_equal, r4_raw_equal, r4_control_pairs, r4_control_equal,
      r4_failed, r4_ls_pairs, r4_ls_slots, r4_ls_canon, r4_ls_raw;
  // R5 materialisation
  long r5_attempts, r5_playouts, r5_done, r5_invariant_bad, r5_diag, r5_exceptions, r5_card_moved,
      r5_cards;
  double r5_us;
};

inline const std::vector<int> baseline_effects_of(FmEnv *e) {
  std::vector<int> v(4096, -1);
  for (card *c : e->core()->cards) v[c->cardid] = static_cast<int>(c->indexer.size());
  return v;
}

struct Tracked {
  FmEnv *e;
  PublicMemory memory;
  std::vector<int> baseline;
  std::vector<std::string> chosen;  // descriptor of each recorded action
  std::vector<int> to_play, msg;
  std::vector<uint64_t> public_chain, layout_chain, rng_chain, offered_chain;
  std::vector<uint32_t> main_codes[2];  // codes by original deck index
  std::vector<card *> main_objects[2];  // objects by original deck index
};

// What the viewer can see: every public zone with identities, zone sizes, life
// points, and the viewer's own hand. Used to reject alternate histories in which a
// different card became public, which matching actions by location cannot detect
// (the core sorts deck-search candidates by identity).
inline uint64_t public_hash(FmEnv *e, int viewer) {
  field *f = e->core()->game_field;
  uint64_t h = 0x51;
  for (int p = 0; p < 2; ++p) {
    auto &pl = f->player[p];
    h = mix(h, pl.lp);
    h = mix(h, pl.list_main.size());
    h = mix(h, pl.list_hand.size());
    h = mix(h, pl.list_extra.size());
    for (card *c : pl.list_grave) h = mix(h, c->data.code);
    for (card *c : pl.list_remove) h = mix(h, (c->current.position & POS_FACEUP) ? c->data.code : 1);
    for (auto *zone : {&pl.list_mzone, &pl.list_szone})
      for (card *c : *zone) {
        h = mix(h, c ? c->current.position : 0);
        h = mix(h, c && (c->current.position & POS_FACEUP) ? c->data.code : 0);
      }
    for (card *c : pl.list_hand)
      h = mix(h, (p == viewer || (c->current.position & POS_FACEUP)) ? c->data.code : 2);
  }
  return h;
}

// Layout by original deck index (cardid), and the future of the random stream.
// Identical in two worlds exactly when every physical object made the same moves.
inline uint64_t layout_hash(FmEnv *e) {
  field *f = e->core()->game_field;
  uint64_t h = 0x61;
  for (int p = 0; p < 2; ++p) {
    for (card *c : f->player[p].list_main) h = mix(h, c->cardid);
    h = mix(h, 0xfffff);
    for (card *c : f->player[p].list_hand) h = mix(h, c->cardid);
    h = mix(h, 0xffffe);
  }
  return h;
}

// The offered actions, order-independent. The viewer sees its own prompts, so a
// world consistent with the viewer's knowledge must reproduce them exactly.
inline uint64_t offered_hash(FmEnv *e) {
  Scope off(false);
  std::vector<std::string> keys;
  for (const auto &a : e->actions()) keys.push_back(descriptor(a));
  std::sort(keys.begin(), keys.end());
  uint64_t h = 0x71;
  for (const auto &k : keys)
    for (char c : k) h = mix(h, static_cast<unsigned char>(c));
  return h;
}

inline uint64_t rng_probe(FmEnv *e) {
  ::mt19937 copy = e->core()->random;
  uint64_t h = 0;
  for (int i = 0; i < 4; ++i) h = mix(h, copy.rand());
  return h;
}

inline void build_tracked(const Game &g, int prefix, int viewer, Tracked &t) {
  (void)spec_for(g);
  t.e = new_env(g, env_seed_of(g));
  {
    Scope s(true);
    t.e->reset();
  }
  t.baseline = baseline_effects_of(t.e);
  for (int p = 0; p < 2; ++p) {
    std::vector<card *> mine;
    for (card *c : t.e->core()->cards)
      if (c->owner == p && c->data.code && !c->is_extra_deck_monster()) mine.push_back(c);
    std::sort(mine.begin(), mine.end(), [](card *a, card *b) { return a->cardid < b->cardid; });
    t.main_objects[p] = mine;
    for (card *c : mine) t.main_codes[p].push_back(c->data.code);
  }
  for (int i = 0; i < prefix; ++i) {
    note_public(t.e, t.memory, viewer);
    t.chosen.push_back(descriptor(t.e->actions()[g.actions[i]]));
    t.public_chain.push_back(public_hash(t.e, viewer));
    t.layout_chain.push_back(layout_hash(t.e));
    t.rng_chain.push_back(rng_probe(t.e));
    t.offered_chain.push_back(offered_hash(t.e));
    t.to_play.push_back(t.e->to_play());
    t.msg.push_back(t.e->msg());
    Scope s(true);
    t.e->act(g.actions[i]);
  }
  note_public(t.e, t.memory, viewer);
  t.public_chain.push_back(public_hash(t.e, viewer));
  t.layout_chain.push_back(layout_hash(t.e));
  t.rng_chain.push_back(rng_probe(t.e));
  t.offered_chain.push_back(offered_hash(t.e));
  t.to_play.push_back(t.e->to_play());
  t.msg.push_back(t.e->msg());
}

inline std::vector<std::pair<uint64_t, uint32_t>> board_codes(FmEnv *e) {
  std::vector<std::pair<uint64_t, uint32_t>> out;
  field *f = e->core()->game_field;
  for (int p = 0; p < 2; ++p) {
    auto &pl = f->player[p];
    uint64_t base = uint64_t(p) << 32;
    for (size_t i = 0; i < pl.list_main.size(); ++i)
      out.push_back({base | (1 << 16) | i, pl.list_main[i]->data.code});
    for (size_t i = 0; i < pl.list_hand.size(); ++i)
      out.push_back({base | (2 << 16) | i, pl.list_hand[i]->data.code});
    for (size_t i = 0; i < pl.list_mzone.size(); ++i)
      if (pl.list_mzone[i]) out.push_back({base | (3 << 16) | i, pl.list_mzone[i]->data.code});
    for (size_t i = 0; i < pl.list_szone.size(); ++i)
      if (pl.list_szone[i]) out.push_back({base | (4 << 16) | i, pl.list_szone[i]->data.code});
  }
  return out;
}

inline void redeal_position(const std::vector<Game> &games, const Pos &pos, int deals, int cap,
                            RedealOut &o) {
  const Game &g = games[pos.game];
  int viewer = g.to_play[pos.index];
  g_use_arena = true;
  arena_clear();
  Tracked t;
  build_tracked(g, pos.index, viewer, t);
  FmEnv *e = t.e;
  Snapshot snap;
  snapshot_take(snap);
  uint64_t x = mix(mix(70000061, pos.game), pos.index);
  ++o.positions;

  auto guarded_random = [&](uint64_t s, long &done, long &invariant_bad, long &diag, long &exc,
                            uint64_t *digest, const std::function<void()> *tick = nullptr) {
    long diag0 = g_diagnostics;
    try {
      Scope sc(true);
      RunResult r;
      absorb(e, r, false);
      bool inv = true;
      while (!e->done() && r.decisions < cap) {
        int n = static_cast<int>(e->actions().size());
        e->act(static_cast<int>(splitmix(s) % n));
        ++r.decisions;
        absorb(e, r, false);
        if (r.decisions % 8 == 0) inv = inv && invariants_ok(e);
        if (tick && e->started()) (*tick)();
      }
      inv = inv && invariants_ok(e);
      finish(e, r);
      done += r.done;
      invariant_bad += !inv;
      if (digest) *digest = r.decision_digest;
    } catch (const std::exception &ex) {
      ++exc;
      fprintf(stderr, "exception game=%d index=%d: %s\n", pos.game, pos.index, ex.what());
    }
    diag += g_diagnostics != diag0;
  };

  // Reference: same policy seeds in the undisturbed world.
  std::vector<uint64_t> policy_seeds(deals), baseline(deals);
  long sink = 0;
  for (int i = 0; i < deals; ++i) {
    policy_seeds[i] = splitmix(x);
    snapshot_restore(snap);
    long sink_exc = 0;
    guarded_random(policy_seeds[i], sink, sink, sink, sink_exc, &baseline[i]);
  }

  // R1: only the two deck orders, plus fresh future randomness.
  uint64_t r1_first = 0;
  for (int i = 0; i < deals; ++i) {
    for (int rep = 0; rep < 2; ++rep) {
      snapshot_restore(snap);
      uint64_t deal_seed = mix(policy_seeds[i], 1);
      {
        Scope sc(true);
        field *f = e->core()->game_field;
        for (int p = 0; p < 2; ++p) {
          auto &v = f->player[p].list_main;
          uint64_t y = mix(deal_seed, p);
          for (int k = static_cast<int>(v.size()) - 1; k > 0; --k)
            std::swap(v[k], v[splitmix(y) % (k + 1)]);
          f->reset_sequence(p, LOCATION_DECK);
        }
        e->core()->random.reset(static_cast<uint32_t>(deal_seed));
      }
      long d = 0, bad = 0, dg = 0, ex = 0;
      uint64_t digest = 0;
      guarded_random(policy_seeds[i], d, bad, dg, ex, &digest);
      if (rep == 0) {
        r1_first = digest;
        ++o.r1_playouts;
        o.r1_done += d;
        o.r1_invariant_bad += bad;
        o.r1_diag += dg;
        o.r1_exceptions += ex;
        o.r1_changed += digest != baseline[i];
      } else {
        o.r1_repeat_bad += digest != r1_first;
      }
    }
  }

  // R2: hand, set cards and decks, from public information plus a seed.
  for (int i = 0; i < deals; ++i) {
    uint64_t first = 0;
    for (int rep = 0; rep < 2; ++rep) {
      snapshot_restore(snap);
      uint64_t deal_seed = mix(policy_seeds[i], 2);
      double t0 = now_us();
      DealReport dr = determinize(e, viewer, deal_seed, t.memory, t.baseline, true);
      double t1 = now_us();
      if (rep == 0) {
        ++o.r2_attempts;
        o.r2_ok += dr.ok;
        o.r2_us += t1 - t0;
        o.r2_moved += dr.moved;
        o.r2_pinned_seen += dr.pinned_seen;
        o.r2_pinned_ref += dr.pinned_referenced;
        o.r2_pinned_state += dr.pinned_state;
        o.r2_type_retries += dr.type_retries;
      }
      if (!dr.ok) continue;
      long d = 0, bad = 0, dg = 0, ex = 0;
      uint64_t digest = 0;
      if (!invariants_ok(e)) ++bad;
      guarded_random(policy_seeds[i], d, bad, dg, ex, &digest);
      if (rep == 0) {
        first = digest;
        ++o.r2_playouts;
        o.r2_done += d;
        o.r2_invariant_bad += bad;
        o.r2_diag += dg;
        o.r2_exceptions += ex;
        o.r2_changed += digest != baseline[i];
      } else {
        o.r2_repeat_bad += digest != first;
      }
    }
  }

  // R3: construct the same hidden assignment as a natural alternate history, by
  // rigging the initial deck and replaying the recorded actions matched by meaning.
  // This is the oracle for R2. It uses the opponent's true private responses, which
  // a real engine does not have, so its acceptance rate is an upper bound.
  uint32_t deck_seed, duel_seed;
  FmEnv::production_seeds(env_seed_of(g), deck_seed, duel_seed);
  auto *canon_a = new CanonRun, *canon_b = new CanonRun;
  Snapshot alt;
  for (int i = 0; i < std::max(1, deals / 2); ++i) {
    uint64_t y = mix(policy_seeds[i], 3);
    int opp = 1 - viewer;
    // Permute codes among never-public opponent objects still hidden at the
    // position, respecting the public monster/spell-trap slot type.
    snapshot_restore(snap);
    field *f = e->core()->game_field;
    std::vector<card *> movable[2];
    auto eligible = [&](card *c) {
      return !t.memory.seen[c->cardid] && !referenced_by_core(f, c, e->msg()) &&
             !carries_object_state(c, t.baseline);
    };
    for (card *c : f->player[viewer].list_main)
      if (eligible(c)) movable[viewer].push_back(c);
    for (card *c : f->player[opp].list_main)
      if (eligible(c)) movable[opp].push_back(c);
    for (card *c : f->player[opp].list_hand)
      if (eligible(c)) movable[opp].push_back(c);
    for (auto *zone : {&f->player[opp].list_mzone, &f->player[opp].list_szone})
      for (card *c : *zone)
        if (c && !(c->current.position & POS_FACEUP) && eligible(c)) movable[opp].push_back(c);
    std::vector<card *> assign[2];
    bool possible = true;
    for (int p = 0; p < 2 && possible; ++p) {
      bool placed = false;
      for (int attempt = 0; attempt < 500 && !placed; ++attempt) {
        assign[p] = movable[p];
        for (int k = static_cast<int>(assign[p].size()) - 1; k > 0; --k)
          std::swap(assign[p][k], assign[p][splitmix(y) % (k + 1)]);
        placed = true;
        for (size_t k = 0; k < movable[p].size(); ++k) {
          auto loc = movable[p][k]->current.location;
          if (loc == LOCATION_MZONE && !is_monster(assign[p][k])) placed = false;
          if (loc == LOCATION_SZONE && is_monster(assign[p][k])) placed = false;
        }
      }
      possible = placed;
    }
    if (!possible) continue;
    ++o.r3_samples;
    ++o.bucket_samples[pos.bucket];
    // Initial deck: each movable object's original index now holds the code dealt
    // to its slot.
    std::vector<uint32_t> rig[2] = {t.main_codes[0], t.main_codes[1]};
    for (int p = 0; p < 2; ++p) {
      for (size_t k = 0; k < movable[p].size(); ++k) {
        auto it = std::find(t.main_objects[p].begin(), t.main_objects[p].end(), movable[p][k]);
        rig[p][it - t.main_objects[p].begin()] = assign[p][k]->data.code;
      }
    }
    auto holders = movable[0];
    holders.insert(holders.end(), movable[1].begin(), movable[1].end());
    auto assigned = assign[0];
    assigned.insert(assigned.end(), assign[1].begin(), assign[1].end());
    std::vector<std::pair<uint64_t, uint32_t>> true_board_after;

    // Naive variant: replay raw indices against the rigged deal.
    {
      ++o.r3_index_samples;
      struct Survive { int ok; };
      Survive sv = in_child<Survive>([&] {
        Survive r{0};
        try {
          arena_clear();
          FmEnv *w = new_env(g, env_seed_of(g));
          Scope sc(true);
          w->reset_rigged(duel_seed, rig[0], rig[1], g.deck0, g.deck1);
          for (int a = 0; a < pos.index; ++a) {
            if (w->done() || w->to_play() != g.to_play[a] ||
                static_cast<int>(w->actions().size()) != g.options[a])
              return r;
            w->act(g.actions[a]);
          }
          r.ok = !w->done();
        } catch (...) {
        }
        return r;
      });
      o.r3_index_survive += sv.ok;
    }

    // Semantic variant.
    arena_clear();
    double t0 = now_us();
    FmEnv *w = new_env(g, env_seed_of(g));
    bool accepted = true;
    std::vector<int> alt_baseline;
    int layout_div = -1, rng_div = -1;
    PublicMemory alt_memory;
    bool rng_at_layout_div = true;
    char div_action[160] = "";
    try {
      {
        Scope sc(true);
        w->reset_rigged(duel_seed, rig[0], rig[1], g.deck0, g.deck1);
      }
      alt_baseline = baseline_effects_of(w);
      for (int a = 0; a < pos.index && accepted; ++a) {
        if (w->done()) {
          ++o.r3_reject_end;
          accepted = false;
          break;
        }
        if (w->to_play() != t.to_play[a] || w->msg() != t.msg[a]) {
          ++o.r3_reject_prompt;
          accepted = false;
          break;
        }
        note_public(w, alt_memory, viewer);
        if (layout_div < 0 && layout_hash(w) != t.layout_chain[a]) {
          layout_div = a;
          rng_at_layout_div = rng_probe(w) == t.rng_chain[a];
          snprintf(div_action, sizeof div_action, "%s msg=%d prev=%s prev_msg=%d", t.chosen[a].c_str(),
                   t.msg[a], a ? t.chosen[a - 1].c_str() : "-", a ? t.msg[a - 1] : 0);
        }
        if (rng_div < 0 && rng_probe(w) != t.rng_chain[a]) rng_div = a;
        if (t.to_play[a] == viewer && offered_hash(w) != t.offered_chain[a]) {
          ++o.r3_reject_offered;
          accepted = false;
          break;
        }
        if (public_hash(w, viewer) != t.public_chain[a]) {
          ++o.r3_reject_public;
          accepted = false;
          break;
        }
        int found = -1;
        {
          Scope off(false);
          for (size_t k = 0; k < w->actions().size(); ++k)
            if (descriptor(w->actions()[k]) == t.chosen[a]) {
              found = static_cast<int>(k);
              break;
            }
        }
        if (found < 0) {
          ++o.r3_reject_action;
          accepted = false;
          break;
        }
        Scope sc(true);
        w->act(found);
      }
    } catch (const std::exception &) {
      ++o.r3_exceptions;
      accepted = false;
    }
    if (accepted && !w->done() && public_hash(w, viewer) != t.public_chain[pos.index]) {
      ++o.r3_reject_public;
      accepted = false;
    }
    if (accepted && !w->done() &&
        (w->to_play() != viewer || w->msg() != t.msg[pos.index] ||
         offered_hash(w) != t.offered_chain[pos.index])) {
      ++o.r3_reject_offered;
      accepted = false;
    }
    if (!accepted || w->done()) {
      if (accepted) ++o.r3_reject_end;
      continue;
    }
    o.r3_accept_us += now_us() - t0;
    o.r3_accept_decisions += pos.index;
    ++o.r3_accept;
    ++o.bucket_accept[pos.bucket];
    note_public(w, alt_memory, viewer);
    if (layout_div < 0 && layout_hash(w) != t.layout_chain[pos.index]) layout_div = pos.index;
    bool layout_same = layout_div < 0;
    o.r3_layout_same += layout_same;
    snapshot_take(alt);
    auto natural_board = board_codes(w);
    uint64_t run_seed = mix(y, 7);
    try {
      run_canonical(w, run_seed, cap, *canon_a, false);
    } catch (const std::exception &) {
      ++o.r3_compare_exceptions;
      continue;
    }
    uint64_t nat_digest = canon_a->digest;
    int nat_winner = canon_a->winner;

    // Mutated world without cardid transfer.
    snapshot_restore(snap);
    apply_assignment(e->core(), holders, assigned, false);
    auto mutated_board = board_codes(e);
    o.r3_board_match += layout_same && mutated_board == natural_board;
    if (layout_same && mutated_board != natural_board) {
      int diffs = 0;
      char line[220] = "";
      for (size_t q = 0; q < std::min(mutated_board.size(), natural_board.size()); ++q)
        if (mutated_board[q] != natural_board[q]) {
          if (++diffs == 1)
            snprintf(line, sizeof line, "player=%d zone=%d seq=%d mutated=%u natural=%u",
                     int(mutated_board[q].first >> 32), int((mutated_board[q].first >> 16) & 0xffff),
                     int(mutated_board[q].first & 0xffff), mutated_board[q].second, natural_board[q].second);
        }
      fprintf(stderr,
              "BOARD game=%d index=%d viewer=%d diffs=%d first: %s | layout_div=%d rng_div=%d "
              "rng_same_at_layout_div=%d at: %s\n",
              pos.game, pos.index, viewer, diffs, line, layout_div, rng_div, rng_at_layout_div,
              div_action);
    }
    try {
      run_canonical(e, run_seed, cap, *canon_b, false);
      o.r3_canon_match += layout_same && canon_b->digest == nat_digest;
    } catch (const std::exception &) {
      ++o.r3_compare_exceptions;
    }
    // Mutated world with cardid transfer; also compare prompt order exactly.
    snapshot_restore(snap);
    apply_assignment(e->core(), holders, assigned, true);
    try {
      run_canonical(e, run_seed, cap, *canon_b, false);
      bool match = canon_b->digest == nat_digest;
      o.r3_canon_match_cardid += layout_same && match;
      o.r3_winner_match += layout_same && canon_b->winner == nat_winner;
      if (layout_same && !match) {
        int div = first_divergence(*canon_a, *canon_b);
        if (div >= 0 && div < 10) ++o.r3_div_lt10;
        if (div >= 0 && div < 100) ++o.r3_div_lt100;
        fprintf(stderr, "DIVERGE game=%d index=%d viewer=%d turn=%d first_divergence=%d lens=%d/%d\n",
                pos.game, pos.index, viewer, 0, div, canon_a->length, canon_b->length);
      }
    } catch (const std::exception &) {
      ++o.r3_compare_exceptions;
    }
    try {
      // Raw prompt order: natural world again, then mutated, no canonical sort.
      snapshot_restore(alt);
      CanonRun *raw_a = canon_a;
      run_canonical(w, run_seed, cap, *raw_a, true);
      snapshot_restore(snap);
      apply_assignment(e->core(), holders, assigned, true);
      run_canonical(e, run_seed, cap, *canon_b, true);
      o.r3_raw_match_cardid += layout_same && canon_b->digest == raw_a->digest;
    } catch (const std::exception &) {
      ++o.r3_compare_exceptions;
    }

    // R4 non-interference. World A is the real duel, world B the natural duel with
    // other hidden cards and the same public history. Same deal seed in both: if a
    // sampled world depends only on public inputs, the worlds must coincide.
    try {
      uint64_t deal_seed = mix(y, 11);
      uint64_t policy = mix(y, 13);
      ++o.r4_pairs;
      snapshot_restore(snap);
      DealReport a = determinize(e, viewer, deal_seed, t.memory, t.baseline, true);
      auto board_a = board_codes(e);
      run_canonical(e, policy, cap, *canon_a, false);
      uint64_t digest_a = canon_a->digest;
      snapshot_restore(snap);
      determinize(e, viewer, deal_seed, t.memory, t.baseline, true);
      run_canonical(e, policy, cap, *canon_a, true);
      uint64_t raw_a = canon_a->digest;

      snapshot_restore(alt);
      DealReport b = determinize(w, viewer, deal_seed, alt_memory, alt_baseline, true);
      auto board_b = board_codes(w);
      run_canonical(w, policy, cap, *canon_b, false);
      uint64_t digest_b = canon_b->digest;
      snapshot_restore(alt);
      determinize(w, viewer, deal_seed, alt_memory, alt_baseline, true);
      run_canonical(w, policy, cap, *canon_b, true);
      uint64_t raw_b = canon_b->digest;
      if (!a.ok || !b.ok) {
        ++o.r4_failed;
      } else {
        o.r4_slots_equal += board_a == board_b;
        o.r4_canon_equal += digest_a == digest_b;
        o.r4_raw_equal += raw_a == raw_b;
        if (layout_same) {
          ++o.r4_ls_pairs;
          o.r4_ls_slots += board_a == board_b;
          o.r4_ls_canon += digest_a == digest_b;
          o.r4_ls_raw += raw_a == raw_b;
        }
        if (board_a != board_b || digest_a != digest_b)
          fprintf(stderr, "R4DIFF game=%d index=%d viewer=%d layout_same=%d slots_equal=%d canon_equal=%d moved=%d/%d pins=%d,%d,%d/%d,%d,%d\n",
                  pos.game, pos.index, viewer, layout_same, board_a == board_b, digest_a == digest_b, a.moved, b.moved,
                  a.pinned_seen, a.pinned_referenced, a.pinned_state, b.pinned_seen, b.pinned_referenced, b.pinned_state);
      }
      // Control: no re-deal, same policy. Must usually differ or the test has no power.
      snapshot_restore(snap);
      {
        Scope sc(true);
        e->core()->random.reset(static_cast<uint32_t>(deal_seed));
      }
      run_canonical(e, policy, cap, *canon_a, false);
      uint64_t ca = canon_a->digest;
      snapshot_restore(alt);
      {
        Scope sc(true);
        w->core()->random.reset(static_cast<uint32_t>(deal_seed));
      }
      run_canonical(w, policy, cap, *canon_b, false);
      ++o.r4_control_pairs;
      o.r4_control_equal += ca == canon_b->digest;
    } catch (const std::exception &) {
      ++o.r4_failed;
    }
  }
  delete canon_a;
  delete canon_b;
  __libc_free(alt.data);

  // R5: cards outside the true decklist. Needed if the opponent's list is unknown.
  static const std::vector<uint32_t> foreign = [] {
    std::vector<uint32_t> v;
    for (const auto &kv : ygopro::main_decks_)
      for (uint32_t c : kv.second) v.push_back(c);
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
  }();
  arena_clear();
  Tracked tt;
  build_tracked(g, pos.index, viewer, tt);
  FmEnv *m = tt.e;
  snapshot_take(snap);
  e = m;
  for (int i = 0; i < deals; ++i) {
    snapshot_restore(snap);
    field *f = m->core()->game_field;
    int opp = 1 - viewer;
    uint64_t y = mix(policy_seeds[i], 5);
    std::vector<card *> replaced;
    std::vector<card *> created;
    std::vector<card *> candidates;
    for (card *c : f->player[opp].list_hand)
      if (!tt.memory.seen[c->cardid] && !referenced_by_core(f, c, m->msg())) candidates.push_back(c);
    for (card *c : f->player[opp].list_main)
      if (!tt.memory.seen[c->cardid] && !referenced_by_core(f, c, m->msg())) candidates.push_back(c);
    if (candidates.empty()) continue;
    ++o.r5_attempts;
    double t0 = now_us();
    {
      Scope sc(true);
      int replacements = std::min<int>(3, candidates.size());
      for (int k = 0; k < replacements; ++k) {
        size_t pick = (k < static_cast<int>(f->player[opp].list_hand.size()) ? k
                                                                            : splitmix(y) % candidates.size());
        card *old = candidates[pick];
        uint32_t code = foreign[splitmix(y) % foreign.size()];
        SlotState slot = capture_slot(old);
        old->cancel_field_effect();
        card *fresh = m->core()->new_card(code);
        fresh->owner = old->owner;
        occupy_slot(fresh, slot, false);
        slot_ref(f, slot) = fresh;
        fresh->apply_field_effect();
        fresh->enable_field_effect(true);
        old->current.controler = PLAYER_NONE;
        old->current.location = 0;
        candidates.erase(candidates.begin() + pick);
        created.push_back(fresh);
        replaced.push_back(old);
        if (candidates.empty()) break;
      }
      m->core()->random.reset(static_cast<uint32_t>(y));
    }
    o.r5_us += now_us() - t0;
    std::vector<std::pair<card *, uint8>> initial;
    std::vector<uint8_t> moved;
    for (card *c : created) {
      initial.push_back({c, c->current.location});
      moved.push_back(0);
    }
    std::function<void()> tick = [&] {
      for (size_t k = 0; k < initial.size(); ++k)
        if (initial[k].first->current.location != initial[k].second) moved[k] = 1;
    };
    long dn = 0, bad = 0, dg = 0, ex = 0;
    ++o.r5_playouts;
    guarded_random(policy_seeds[i], dn, bad, dg, ex, nullptr, &tick);
    o.r5_done += dn;
    o.r5_invariant_bad += bad;
    o.r5_diag += dg;
    o.r5_exceptions += ex;
    o.r5_cards += initial.size();
    for (uint8_t mv : moved) o.r5_card_moved += mv;
  }
  snapshot_restore(snap);
  __libc_free(snap.data);
  g_use_arena = false;
}

inline void add_out(RedealOut &t, const RedealOut &o) {
  auto *a = reinterpret_cast<char *>(&t);
  const auto *b = reinterpret_cast<const char *>(&o);
  // All fields are 8 bytes (long or double); r2_us, r3_accept_us, r3_accept_decisions
  // and r5_us are doubles and are fixed below.
  for (size_t i = 0; i + 8 <= sizeof(RedealOut); i += 8) {
    long x, y;
    std::memcpy(&x, a + i, 8);
    std::memcpy(&y, b + i, 8);
    x += y;
    std::memcpy(a + i, &x, 8);
  }
}

inline int cmd_redeal(const std::vector<Game> &games, std::map<std::string, std::string> &args) {
  auto get = [&](const std::string &k, int d) {
    auto it = args.find(k);
    return it == args.end() ? d : std::stoi(it->second);
  };
  int per_bucket = get("positions", 60), deals = get("deals", 6), cap = get("cap", 3000),
      workers = get("workers", 4);
  auto positions = sample_positions(games, per_bucket, 70000059);
  for (const auto &g : games) (void)spec_for(g);
  std::vector<int> pipes;
  std::vector<pid_t> pids;
  for (int w = 0; w < workers; ++w) {
    int fds[2];
    if (pipe(fds)) return 1;
    pid_t pid = fork();
    if (pid == 0) {
      close(fds[0]);
      struct Totals {
        double r2_us = 0, r3_us = 0, r3_dec = 0, r5_us = 0;
        long crashed = 0;
        RedealOut sum{};
      };
      auto *tot = new Totals();
      track_install();
      for (size_t i = w; i < positions.size(); i += workers) {
        // Each position in its own process: a crash is counted, not fatal.
        struct Boxed {
          RedealOut o;
        };
        try {
          Boxed b = in_child<Boxed>([&] {
            Boxed r{};
            redeal_position(games, positions[i], deals, cap, r.o);
            return r;
          });
          tot->r2_us += b.o.r2_us;
          tot->r3_us += b.o.r3_accept_us;
          tot->r3_dec += b.o.r3_accept_decisions;
          tot->r5_us += b.o.r5_us;
          b.o.r2_us = b.o.r3_accept_us = b.o.r3_accept_decisions = b.o.r5_us = 0;
          add_out(tot->sum, b.o);
        } catch (const std::exception &ex) {
          ++tot->crashed;
          fprintf(stderr, "CRASH game=%d index=%d: %s\n", positions[i].game, positions[i].index,
                  ex.what());
        }
      }
      (void)!write(fds[1], tot, sizeof *tot);
      _exit(0);
    }
    close(fds[1]);
    pipes.push_back(fds[0]);
    pids.push_back(pid);
  }
  RedealOut t{};
  double r2_us = 0, r3_us = 0, r3_dec = 0, r5_us = 0;
  long crashed = 0;
  for (int w = 0; w < workers; ++w) {
    struct {
      double r2_us, r3_us, r3_dec, r5_us;
      long crashed;
      RedealOut sum;
    } in{};
    size_t off = 0;
    auto *bytes = reinterpret_cast<char *>(&in);
    while (off < sizeof in) {
      ssize_t n = read(pipes[w], bytes + off, sizeof in - off);
      if (n <= 0) break;
      off += n;
    }
    waitpid(pids[w], nullptr, 0);
    if (off != sizeof in) {
      fprintf(stderr, "redeal worker %d died\n", w);
      return 1;
    }
    r2_us += in.r2_us;
    r3_us += in.r3_us;
    r3_dec += in.r3_dec;
    r5_us += in.r5_us;
    crashed += in.crashed;
    add_out(t, in.sum);
  }
#define J(name) "\"" #name "\":%ld,"
  char buf[4000];
  snprintf(buf, sizeof buf,
           "{\"redeal\":true,\"positions\":%ld,\"crashed_positions\":%ld,\"deals\":%d,\"cap\":%d,"
           "\"r1\":{" J(playouts) J(done) J(invariant_bad) J(diag) J(exceptions) J(changed) J(repeat_bad) "\"_\":0},"
           "\"r2\":{" J(attempts) J(ok) J(playouts) J(done) J(invariant_bad) J(diag) J(exceptions) J(repeat_bad)
           J(changed) J(moved) J(pinned_seen) J(pinned_ref) J(pinned_state) J(type_retries) "\"us_per_deal\":%.1f},"
           "\"r3\":{" J(samples) J(accept) J(reject_prompt) J(reject_offered) J(reject_public) J(reject_action) J(reject_end) J(exceptions)
           J(index_samples) J(index_survive) J(layout_same) J(board_match) J(canon_match) J(canon_match_cardid)
           J(raw_match_cardid) J(winner_match) J(compare_exceptions) J(div_lt10) J(div_lt100)
           "\"accept_early\":\"%ld/%ld\",\"accept_mid\":\"%ld/%ld\",\"accept_late\":\"%ld/%ld\",\"ms_per_accepted\":%.2f},"
           "\"r4\":{" J(pairs) J(failed) J(slots_equal) J(canon_equal) J(raw_equal) J(control_pairs) J(control_equal) J(layout_same_pairs) J(layout_same_slots) J(layout_same_canon) J(layout_same_raw) "\"_\":0},"
           "\"r5\":{" J(attempts) J(playouts) J(done) J(invariant_bad) J(diag) J(exceptions) J(cards) J(card_moved)
           "\"us_per_materialise\":%.1f}}",
           t.positions, crashed, deals, cap, t.r1_playouts, t.r1_done, t.r1_invariant_bad, t.r1_diag,
           t.r1_exceptions, t.r1_changed, t.r1_repeat_bad, t.r2_attempts, t.r2_ok, t.r2_playouts,
           t.r2_done, t.r2_invariant_bad, t.r2_diag, t.r2_exceptions, t.r2_repeat_bad, t.r2_changed,
           t.r2_moved, t.r2_pinned_seen, t.r2_pinned_ref, t.r2_pinned_state, t.r2_type_retries,
           r2_us / std::max(1L, t.r2_attempts), t.r3_samples, t.r3_accept, t.r3_reject_prompt, t.r3_reject_offered, t.r3_reject_public,
           t.r3_reject_action, t.r3_reject_end, t.r3_exceptions, t.r3_index_samples,
           t.r3_index_survive, t.r3_layout_same, t.r3_board_match, t.r3_canon_match, t.r3_canon_match_cardid,
           t.r3_raw_match_cardid, t.r3_winner_match, t.r3_compare_exceptions, t.r3_div_lt10,
           t.r3_div_lt100, t.bucket_accept[0], t.bucket_samples[0], t.bucket_accept[1],
           t.bucket_samples[1], t.bucket_accept[2], t.bucket_samples[2],
           r3_us / 1000 / std::max(1L, t.r3_accept), t.r4_pairs, t.r4_failed, t.r4_slots_equal,
           t.r4_canon_equal, t.r4_raw_equal, t.r4_control_pairs, t.r4_control_equal, t.r4_ls_pairs, t.r4_ls_slots, t.r4_ls_canon, t.r4_ls_raw, t.r5_attempts,
           t.r5_playouts, t.r5_done, t.r5_invariant_bad, t.r5_diag, t.r5_exceptions, t.r5_cards,
           t.r5_card_moved, r5_us / std::max(1L, t.r5_attempts));
  emit_kv(buf);
  return 0;
}

// -- Position construction ----------------------------------------------------
// Candidate D: rebuild a position from a description through the only public
// injection API, new_card/set_player_info. Compare the first idle prompt offered
// against the true idle prompt from the real game.
inline std::string act_category(const std::string &key) {
  auto first = key.find('|');
  auto second = key.find('|', first + 1);
  return key.substr(first + 1, second - first - 1);
}

inline int cmd_construct(const std::vector<Game> &games, std::map<std::string, std::string> &args) {
  auto get = [&](const std::string &k, int d) {
    auto it = args.find(k);
    return it == args.end() ? d : std::stoi(it->second);
  };
  int limit = get("positions", 400);
  uint64_t x = 70000067;
  // Every main-phase idle prompt in the recorded games, sampled.
  std::vector<Pos> idle;
  {
    for (size_t gi = 0; gi < games.size(); ++gi) {
      FmEnv *e = build_position(games[gi], 0);
      Scope s(g_use_arena);
      for (size_t i = 0; i < games[gi].actions.size(); ++i) {
        if (e->msg() == MSG_SELECT_IDLECMD) {
          Scope off(false);
          idle.push_back({static_cast<int>(gi), 0, static_cast<int>(i)});
        }
        e->act(games[gi].actions[i]);
      }
      free_env(e);
    }
  }
  for (int i = static_cast<int>(idle.size()) - 1; i > 0; --i)
    std::swap(idle[i], idle[splitmix(x) % (i + 1)]);
  if (static_cast<int>(idle.size()) > limit) idle.resize(limit);

  long tested = 0, not_idle = 0, exact = 0, exact_nophase = 0, board_equal = 0, atk_equal = 0;
  long missing_total = 0, extra_total = 0, exceptions = 0, first_idle_tested = 0, first_idle_exact = 0;
  long truth_has_summon_used = 0, truth_has_chain = 0, truth_turn_effects = 0, truth_nonbaseline = 0;
  std::map<std::string, long> missing_by_act, extra_by_act;
  for (const Pos &p : idle) {
    const Game &g = games[p.game];
    struct Result {
      int tested, not_idle, exact, exact_nophase, board_equal, atk_equal, missing, extra, exception;
      int first_idle, summon_used, nonbaseline, declined;
      int miss_cat[32], extra_cat[32];
    };
    Result r = in_child<Result>([&] {
      Result out{};
      try {
        FmEnv *truth = build_position(g, p.index);
        field *tf = truth->core()->game_field;
        int tp = truth->to_play();
        out.summon_used = tf->core.summon_count[tp] != 0;
        int nonbaseline = 0;
        for (card *c : truth->core()->cards)
          if (c->current.location & (LOCATION_MZONE | LOCATION_SZONE))
            nonbaseline += !c->relations.empty() || c->equiping_target || !c->counters.empty() ||
                           !c->effect_target_cards.empty();
        out.nonbaseline = nonbaseline > 0;
        std::vector<std::string> truth_actions;
        for (const auto &a : truth->actions()) truth_actions.push_back(descriptor(a));
        // Seat-swap so the player to act is player 0, who has the first turn.
        std::vector<ConstructedCard> cards;
        int lps[2];
        for (int q = 0; q < 2; ++q) {
          auto &pl = tf->player[q];
          int seat = q == tp ? 0 : 1;
          lps[seat] = pl.lp;
          auto add = [&](card *c) {
            if (c) cards.push_back({c->data.code, static_cast<uint8_t>(c->owner == q ? seat : 1 - seat), static_cast<uint8_t>(seat),
                                    c->current.location, c->current.sequence, c->current.position});
          };
          for (card *c : pl.list_main) add(c);
          for (card *c : pl.list_extra) add(c);
          for (card *c : pl.list_hand) add(c);
          for (card *c : pl.list_mzone) add(c);
          for (card *c : pl.list_szone) add(c);
          for (card *c : pl.list_grave) add(c);
          for (card *c : pl.list_remove) add(c);
        }
        uint32_t attack_truth = 0;
        for (int q = 0; q < 2; ++q)
          for (card *c : tf->player[q].list_mzone)
            if (c) attack_truth = attack_truth * 31 + c->data.code + 7 * c->get_attack();
        FmEnv *built = new_env(g, 12345);
        built->reset_constructed(cards, lps, g.deck0, g.deck1);
        // Decline optional chain windows on the way to the first idle prompt.
        for (int window = 0; window < 32 && !built->done() && built->msg() == MSG_SELECT_CHAIN; ++window) {
          int cancel = -1;
          for (size_t k = 0; k < built->actions().size(); ++k)
            if (built->actions()[k].act_ == ygopro::ActionAct::Cancel) cancel = static_cast<int>(k);
          if (cancel < 0) break;
          built->act(cancel);
          ++out.declined;
        }
        if (built->done() || built->msg() != MSG_SELECT_IDLECMD) {
          out.not_idle = 1;
          return out;
        }
        out.tested = 1;
        std::vector<std::string> constructed;
        for (const auto &a : built->actions()) constructed.push_back(descriptor(a));
        auto a = truth_actions, b = constructed;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        out.exact = a == b;
        std::vector<std::string> af, bf;
        for (auto &k : a)
          if (k[0] != '|') af.push_back(k);
        for (auto &k : b)
          if (k[0] != '|') bf.push_back(k);
        out.exact_nophase = af == bf;
        for (auto &k : af)
          if (!std::binary_search(bf.begin(), bf.end(), k)) {
            ++out.missing;
            int cat = std::atoi(act_category(k).c_str());
            if (cat >= 0 && cat < 32) ++out.miss_cat[cat];
          }
        for (auto &k : bf)
          if (!std::binary_search(af.begin(), af.end(), k)) {
            ++out.extra;
            int cat = std::atoi(act_category(k).c_str());
            if (cat >= 0 && cat < 32) ++out.extra_cat[cat];
          }
        field *bfd = built->core()->game_field;
        bool same_board = true;
        for (int q = 0; q < 2; ++q) {
          int seat = q == tp ? 0 : 1;
          auto &pt = tf->player[q];
          auto &pb = bfd->player[seat];
          for (size_t i = 0; i < pt.list_mzone.size(); ++i) {
            card *ca = pt.list_mzone[i], *cb = pb.list_mzone[i];
            if ((ca == nullptr) != (cb == nullptr) ||
                (ca && (ca->data.code != cb->data.code || ca->current.position != cb->current.position)))
              same_board = false;
            if (ca && cb && ca->data.code == cb->data.code && ca->get_attack() != cb->get_attack())
              out.atk_equal = -1;
          }
          for (size_t i = 0; i < pt.list_szone.size(); ++i) {
            card *ca = pt.list_szone[i], *cb = pb.list_szone[i];
            if ((ca == nullptr) != (cb == nullptr) || (ca && ca->data.code != cb->data.code))
              same_board = false;
          }
          if (pt.list_hand.size() != pb.list_hand.size() || pt.list_grave.size() != pb.list_grave.size())
            same_board = false;
        }
        out.board_equal = same_board;
        out.atk_equal = out.atk_equal == -1 ? 0 : 1;
      } catch (const std::exception &) {
        out.exception = 1;
      }
      return out;
    });
    tested += r.tested;
    not_idle += r.not_idle;
    exact += r.exact;
    exact_nophase += r.exact_nophase;
    board_equal += r.board_equal;
    atk_equal += r.atk_equal;
    missing_total += r.missing;
    extra_total += r.extra;
    exceptions += r.exception;
    truth_has_summon_used += r.tested && r.summon_used;
    truth_nonbaseline += r.tested && r.nonbaseline;
    if (r.tested && !r.summon_used && !r.nonbaseline) {
      ++first_idle_tested;
      first_idle_exact += r.exact_nophase;
    }
    for (int c = 0; c < 32; ++c) {
      if (r.miss_cat[c]) missing_by_act[std::to_string(c)] += r.miss_cat[c];
      if (r.extra_cat[c]) extra_by_act[std::to_string(c)] += r.extra_cat[c];
    }
  }
  std::string cats = "\"missing_by_act\":{";
  for (auto &kv : missing_by_act) cats += "\"" + kv.first + "\":" + std::to_string(kv.second) + ",";
  cats += "\"_\":0},\"extra_by_act\":{";
  for (auto &kv : extra_by_act) cats += "\"" + kv.first + "\":" + std::to_string(kv.second) + ",";
  cats += "\"_\":0}";
  char buf[1500];
  snprintf(buf, sizeof buf,
           "{\"construct\":true,\"sampled\":%zu,\"tested\":%ld,\"first_prompt_not_idle\":%ld,"
           "\"exceptions\":%ld,\"exact_action_set\":%ld,\"exact_excluding_phase_actions\":%ld,"
           "\"board_equal\":%ld,\"atk_all_equal\":%ld,\"missing_actions\":%ld,\"extra_actions\":%ld,"
           "\"truth_normal_summon_used\":%ld,\"truth_has_relations\":%ld,"
           "\"subset_no_summon_no_relations\":%ld,\"subset_exact_excl_phase\":%ld,%s}",
           idle.size(), tested, not_idle, exceptions, exact, exact_nophase, board_equal, atk_equal,
           missing_total, extra_total, truth_has_summon_used, truth_nonbaseline, first_idle_tested,
           first_idle_exact, cats.c_str());
  emit_kv(buf);
  return 0;
}
}  // namespace fm
