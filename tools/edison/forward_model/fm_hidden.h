// Hidden-state tests R1-R5, and position construction (candidate D).
//
// The determinizer itself is in fm_deal.h. Everything here runs inside the simulator on purpose: the
// natural-deal comparisons read true state because they are the test oracle, not because a sampler may.
#pragma once
#include <array>

#include "fm_deal.h"

namespace fm {

// An action as a viewer can name it: own Deck options by the card offered, not the Deck position the
// binder happened to take, since the two differ between Deck orders.
inline std::string descriptor(FmEnv *e, const LegalAction &a) {
  return DealAccess::canonical_spec(e, a) + "|" + std::to_string(static_cast<int>(a.act_)) + "|" +
         std::to_string(static_cast<int>(a.phase_)) + "|" + std::to_string(a.finish_) + "|" +
         std::to_string(a.position_) + "|" + std::to_string(a.effect_) + "|" +
         std::to_string(a.number_) + "|" + std::to_string(static_cast<int>(a.place_)) + "|" +
         std::to_string(a.attribute_);
}

inline bool is_monster(card *c) { return c->data.type & TYPE_MONSTER; }

// -- Runs ---------------------------------------------------------------------
constexpr int kChainCap = 4000;
struct CanonRun {
  std::array<uint64_t, kChainCap> chain, obs;
  std::array<uint8_t, kChainCap> seat;
  int length = 0, winner = -1, reason = -1, done = 0;
  uint64_t digest = 0;
};

// With raw off, the policy chooses from options in canonical order, so it is blind to prompt order;
// with raw on it chooses by exact index. `with_obs` records every observation byte at every decision.
inline void run_canonical(FmEnv *e, uint64_t seed, int cap, CanonRun &out, bool raw, bool with_obs) {
  out.length = 0;
  out.digest = 0;
  std::vector<std::pair<std::string, int>> keys;
  for (;;) {
    uint64_t h = 0x77;
    int n = static_cast<int>(e->actions().size());
    {
      Scope off(false);
      keys.clear();
      for (int i = 0; i < n; ++i) keys.emplace_back(descriptor(e, e->actions()[i]), i);
      if (!raw) std::stable_sort(keys.begin(), keys.end());
      h = mix(h, e->done());
      h = mix(h, e->to_play());
      h = mix(h, e->msg());
      h = mix(h, e->turn());
      h = mix(h, e->phase());
      for (const auto &k : keys)
        for (char ch : k.first) h = mix(h, static_cast<unsigned char>(ch));
    }
    if (out.length < kChainCap) {
      out.chain[out.length] = h;
      out.seat[out.length] = static_cast<uint8_t>(e->to_play());
      if (with_obs) {
        Scope s(true);
        out.obs[out.length] = e->obs_hash();
      }
    }
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

// The first decision whose observation bytes differ, among one seat's decisions or all of them.
inline int first_obs_divergence(const CanonRun &a, const CanonRun &b, int only_seat) {
  int n = std::min({a.length, b.length, kChainCap});
  for (int i = 0; i < n; ++i) {
    if (a.seat[i] != b.seat[i] || a.chain[i] != b.chain[i]) return i;
    if ((only_seat < 0 || a.seat[i] == only_seat) && a.obs[i] != b.obs[i]) return i;
  }
  return a.length == b.length ? -1 : n;
}

// Of the first `count` decisions by `seat`, are all observation bytes equal?
inline bool first_seat_obs_equal(const CanonRun &a, const CanonRun &b, int seat, int count) {
  int n = std::min({a.length, b.length, kChainCap}), seen = 0;
  for (int i = 0; i < n && seen < count; ++i) {
    if (a.seat[i] != b.seat[i] || a.chain[i] != b.chain[i]) return false;
    if (a.seat[i] == seat) {
      ++seen;
      if (a.obs[i] != b.obs[i]) return false;
    }
  }
  return seen == count || a.length == b.length;
}

struct RedealOut {
  long positions;
  // R1 deck reorder
  long r1_playouts, r1_done, r1_invariant_bad, r1_diag, r1_exceptions, r1_changed, r1_repeat_bad;
  // R2 full determinization
  long r2_attempts, r2_ok, r2_playouts, r2_done, r2_invariant_bad, r2_diag, r2_exceptions, r2_repeat_bad, r2_changed,
      r2_moved, r2_known_pinned, r2_extra_kept, r2_banished_kept, r2_rows_scrubbed, r2_sets_rebuilt, r2_rebound,
      r2_root_obs_changed;
  long r2_refused[int(DealStatus::kCount)];
  long r2_positions_refused_all;
  double r2_us;
  long r2_hard_rule_bad, r2_true_hard_rule_bad, r2_set_slots, r2_state_kind[8], r2_slot_state_changed;
  // R3 natural alternate history
  long r3_samples, r3_accept, r3_reject_prompt, r3_reject_offered, r3_reject_public, r3_reject_obs, r3_reject_action,
      r3_reject_end, r3_exceptions;
  long r3_index_samples, r3_index_survive;
  double r3_accept_us, r3_accept_decisions;
  long r3_layout_same, r3_rng_same, r3_board_match, r3_canon_match, r3_canon_match_cardid, r3_raw_match_cardid, r3_winner_match,
      r3_viewer_obs_match, r3_compare_exceptions;
  long r3_div_lt10, r3_div_lt100;
  long bucket_samples[3], bucket_accept[3];
  // R4 non-interference
  long r4_pairs, r4_refused_both, r4_refused_one, r4_slots_equal, r4_canon_equal, r4_raw_equal, r4_winner_equal,
      r4_root_obs_equal, r4_next8_viewer_obs_equal, r4_viewer_obs_equal, r4_all_obs_equal, r4_control_pairs,
      r4_control_chain_equal, r4_control_viewer_obs_equal, r4_failed, r4_ls_pairs, r4_ls_slots, r4_ls_canon, r4_ls_raw,
      r4_ls_obs;
  // R4W: worlds that need only one public history and one viewer view, and may differ in private prompts
  long w_samples, w_accept, w_with_extra, w_extra_prompts, w_skipped_prompts, w_reject_private, w_reject_viewer, w_reject_public, w_reject_end;
  long w_pairs, w_refused_a, w_refused_b, w_refused_both, w_refused_one, w_slots_equal, w_canon_equal, w_raw_equal, w_winner_equal, w_root_obs_equal,
      w_next8_viewer_obs_equal, w_viewer_obs_equal, w_all_obs_equal, w_x_pairs, w_x_refused_one, w_x_slots, w_x_canon, w_x_raw, w_hard_rule_bad, w_failed;
  // R5 materialisation
  long r5_attempts, r5_playouts, r5_done, r5_invariant_bad, r5_diag, r5_exceptions, r5_card_moved, r5_cards;
  double r5_us;
};

struct TrueDuel {
  FmEnv *e = nullptr;
  PublicMemory memory;
  WorldBaseline baseline;
  std::vector<std::string> chosen;  // descriptor of each recorded action
  std::vector<int> to_play, msg;
  std::vector<uint8_t> declined;  // a decision answered with Cancel: no row, no public effect
  std::vector<uint64_t> public_chain, layout_chain, rng_chain, offered_chain, viewer_obs;
  std::vector<uint32_t> main_codes[2];  // codes by original deck index
  std::vector<card *> main_objects[2];  // objects by original deck index
};

// What the viewer can see: every public zone with identities, zone sizes, life points, the viewer's own
// hand, and every card shown to the viewer between decisions.
inline uint64_t public_hash(FmEnv *e, int viewer, const PublicMemory &m) {
  field *f = e->core()->game_field;
  uint64_t h = 0x51;
  h = mix(h, m.confirm_chain[viewer]);
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

// The offered actions, order-independent.
inline uint64_t offered_hash(FmEnv *e) {
  Scope off(false);
  std::vector<std::string> keys;
  for (const auto &a : e->actions()) keys.push_back(descriptor(e, a));
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

inline TapContext g_tap;

inline void start_true_duel(TrueDuel &t) {
  g_tap = {t.e, &t.memory};
  g_message_tap = on_core_messages;
  g_message_tap_context = &g_tap;
}
inline void stop_true_duel() { g_message_tap = nullptr; }

// Set by the search's privacy test: called on every accepted pair of natural duels with one public history.
inline std::function<void(const Game &, const Pos &, int, FmEnv *, TrueDuel &, const Snapshot &, FmEnv *, TrueDuel &, const Snapshot &, const AssumedList &)> g_search_pair;

inline void build_true_duel(const Game &g, int prefix, int viewer, TrueDuel &t, bool record_obs) {
  t.e = new_env(g, env_seed_of(g));
  start_true_duel(t);
  {
    Scope s(true);
    t.e->reset();
  }
  capture_baseline(t.e, t.baseline);
  for (int p = 0; p < 2; ++p) {
    std::vector<card *> mine;
    for (card *c : t.e->core()->cards)
      if (c->owner == p && c->data.code && !c->is_extra_deck_monster()) mine.push_back(c);
    std::sort(mine.begin(), mine.end(), [](card *a, card *b) { return a->cardid < b->cardid; });
    t.main_objects[p] = mine;
    for (card *c : mine) t.main_codes[p].push_back(c->data.code);
  }
  auto record = [&](int index) {
    note_public(t.e, t.memory);
    t.public_chain.push_back(public_hash(t.e, viewer, t.memory));
    t.layout_chain.push_back(layout_hash(t.e));
    t.rng_chain.push_back(rng_probe(t.e));
    t.offered_chain.push_back(offered_hash(t.e));
    t.to_play.push_back(t.e->to_play());
    t.msg.push_back(t.e->msg());
    uint64_t obs = 0;
    if (record_obs && t.e->to_play() == viewer) {
      Scope s(true);
      obs = t.e->obs_hash();
    }
    t.viewer_obs.push_back(obs);
  };
  for (int i = 0; i < prefix; ++i) {
    record(i);
    t.chosen.push_back(descriptor(t.e, t.e->actions()[g.actions[i]]));
    t.declined.push_back(t.e->actions()[g.actions[i]].act_ == ygopro::ActionAct::Cancel);
    RowMarker marker = before_true_step(t.e, t.memory, g.actions[i]);
    {
      Scope s(true);
      t.e->act(g.actions[i]);
    }
    after_true_step(t.e, t.memory, marker);
  }
  record(prefix);
  stop_true_duel();
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

inline AssumedList true_list_of_opponent(const Game &g, int viewer) {
  AssumedList assumed;
  assumed.opponent_main = ygopro::main_decks_.at(viewer == 0 ? g.deck1 : g.deck0);
  return assumed;
}

inline int first_chain_divergence(const CanonRun &a, const CanonRun &b) {
  int n = std::min({a.length, b.length, kChainCap});
  for (int i = 0; i < n; ++i)
    if (a.chain[i] != b.chain[i]) return i;
  return n;
}

// Reports where and how two worlds first differ, for a human. Replays both from their snapshots.
inline void describe_pair(FILE *log, const char *tag, const Game &g, const Pos &pos, int viewer, FmEnv *a, FmEnv *b,
                          const std::function<void()> &prepare_a, const std::function<void()> &prepare_b, uint64_t policy,
                          int at, bool canonical = false) {
  if (!log) return;
  static const char *fields[] = {"cards_", "global_", "decision_", "selection_", "actions_", "h_actions_"};
  auto run_to = [&](FmEnv *e, const std::function<void()> &prepare, std::vector<std::vector<uint8_t>> &bytes,
                    std::vector<std::string> &options, int &seat, int &msg, int &turn) {
    prepare();
    uint64_t seed = policy;
    for (int i = 0;; ++i) {
      if (e->done()) break;
      int n = static_cast<int>(e->actions().size());
      if (i == at) {
        Scope s(true);
        e->encode();
        Scope off(false);
        auto f = e->obs_fields();
        bytes.resize(6);
        for (int k = 0; k < 6; ++k) {
          auto *p = static_cast<const uint8_t *>(f.data[k]);
          bytes[k].assign(p, p + f.size[k]);
        }
        for (const auto &act : e->actions()) options.push_back(descriptor(e, act));
        seat = e->to_play();
        msg = e->msg();
        turn = e->turn();
        if (getenv("FM_DESCRIBE_HAND")) {
          fprintf(log, "HAND seat=%d:", seat);
          for (card *c : e->core()->game_field->player[seat].list_hand) {
            fprintf(log, " [%u cardid=%u fieldid=%u/%u enabled=%d", c->data.code, c->cardid, c->fieldid, c->fieldid_r, c->is_status(STATUS_EFFECT_ENABLED) ? 1 : 0);
            for (const auto &kv : c->field_effect) fprintf(log, " f%u:%u%s", kv.first, kv.second->id, kv.second->in_range(c) ? "*" : "");
            for (const auto &kv : c->single_effect) fprintf(log, " s%u:%u%s", kv.first, kv.second->id, kv.second->is_flag(EFFECT_FLAG_SINGLE_RANGE) ? "*" : "");
            fprintf(log, "]");
          }
          fprintf(log, "\nFIELD:");
          for (card *c : e->core()->game_field->player[seat].list_mzone)
            if (c) fprintf(log, " [%u seq=%d pos=%d status=%x info=%x turnid=%u counter=%u announce=%d attacked=%d cardid=%u]", c->data.code, c->current.sequence, c->current.position, c->status, c->summon_info, c->turnid,
                           c->turn_counter, c->announce_count, c->attacked_count, c->cardid);
          for (int side = 0; side < 2; ++side) {
            fprintf(log, "\nGRAVE%d:", side);
            for (card *c : e->core()->game_field->player[side].list_grave)
              fprintf(log, " %u/r%x/p%d/id%u/t%u", c->data.code, c->current.reason, c->previous.location, c->cardid, c->turnid);
            fprintf(log, "\nMZONE%d:", side);
            for (card *c : e->core()->game_field->player[side].list_mzone)
              if (c) fprintf(log, " %u/s%d/p%d/id%u/atk%d", c->data.code, c->current.sequence, c->current.position, c->cardid, c->get_attack());
            fprintf(log, "\nLP%d: %d", side, e->core()->game_field->player[side].lp);
            fprintf(log, "\nHANDS%d:", side);
            for (card *c : e->core()->game_field->player[side].list_hand) fprintf(log, " %u/id%u", c->data.code, c->cardid);
            fprintf(log, "\nDECK%d:", side);
            for (card *c : e->core()->game_field->player[side].list_main) fprintf(log, " %u/id%u", c->data.code, c->cardid);
          }
          fprintf(log, "\nOPTIONS:");
          for (const auto &o : options) fprintf(log, " %s", o.substr(0, o.find('|') + 2).c_str());
          fprintf(log, "\n");
        }
        return;
      }
      std::vector<std::pair<std::string, int>> keys;
      {
        Scope off(false);
        for (int k = 0; k < n; ++k) keys.emplace_back(descriptor(e, e->actions()[k]), k);
        if (canonical) std::stable_sort(keys.begin(), keys.end());
      }
      int pick = keys[splitmix(seed) % n].second;
      Scope s(true);
      e->act(pick);
    }
  };
  if (getenv("FM_DESCRIBE_AT")) at = std::atoi(getenv("FM_DESCRIBE_AT"));
  std::vector<std::vector<uint8_t>> ba, bb;
  std::vector<std::string> oa, ob;
  int sa = -1, sb = -1, ma = 0, mb = 0, ta = 0, tb = 0;
  run_to(a, prepare_a, ba, oa, sa, ma, ta);
  run_to(b, prepare_b, bb, ob, sb, mb, tb);
  fprintf(log, "%s game=%d(%s/%s seed=%u env=%d) index=%d viewer=%d at=%d seat=%d/%d msg=%d/%d turn=%d/%d options=%zu/%zu", tag,
          pos.game, g.deck0.c_str(), g.deck1.c_str(), g.seed, g.env, pos.index, viewer, at, sa, sb, ma, mb, ta, tb, oa.size(), ob.size());
  if (ba.size() == 6 && bb.size() == 6)
    for (int k = 0; k < 6; ++k) {
      size_t common = std::min(ba[k].size(), bb[k].size());
      for (size_t i = 0; i < common; ++i)
        if (ba[k][i] != bb[k][i]) {
          fprintf(log, " first_diff=%s[%zu]=%d/%d", fields[k], i, ba[k][i], bb[k][i]);
          break;
        }
    }
  for (size_t i = 0; i < std::min(oa.size(), ob.size()); ++i)
    if (oa[i] != ob[i]) {
      fprintf(log, " option[%zu]=%s/%s", i, oa[i].c_str(), ob[i].c_str());
      break;
    }
  fprintf(log, "\n");
  fflush(log);
}

inline FILE *g_redeal_log = nullptr;

// An assignment of these objects to these slots that the public rules allow, for rigging a natural duel. It need
// not be uniform: the replay decides whether the rigged duel is a real alternative.
inline bool rig_assignment(const std::vector<card *> &slots, uint64_t &y, const PublicMemory &m, const WorldBaseline &b, std::vector<card *> &out) {
  size_t n = slots.size();
  out.assign(n, nullptr);
  std::vector<uint8_t> used(n, 0);
  for (size_t i = 0; i < n; ++i) {
    SlotRule rule = rule_of_slot(slots[i], m);
    if (rule.cls == kFree) continue;
    std::vector<int> fits;
    for (size_t k = 0; k < n; ++k)
      if (!used[k] && rule_fits(slots[k]->data.code, rule, b)) fits.push_back(static_cast<int>(k));
    if (fits.empty()) return false;
    int pick = fits[splitmix(y) % fits.size()];
    used[pick] = 1;
    out[i] = slots[pick];
  }
  std::vector<card *> rest;
  for (size_t k = 0; k < n; ++k)
    if (!used[k]) rest.push_back(slots[k]);
  for (int i = static_cast<int>(rest.size()) - 1; i > 0; --i) std::swap(rest[i], rest[splitmix(y) % (i + 1)]);
  size_t at = 0;
  for (size_t i = 0; i < n; ++i)
    if (!out[i]) out[i] = rest[at++];
  return true;
}

inline void redeal_position(const std::vector<Game> &games, const Pos &pos, int deals, int cap, RedealOut &o) {
  const Game &g = games[pos.game];
  int viewer = g.to_play[pos.index];
  AssumedList assumed = true_list_of_opponent(g, viewer);
  g_use_arena = true;
  arena_clear();
  auto *t = new TrueDuel;
  build_true_duel(g, pos.index, viewer, *t, true);
  FmEnv *e = t->e;
  Snapshot snap;
  snapshot_take(snap);
  uint64_t x = mix(mix(85100061, pos.game), pos.index);
  ++o.positions;
  uint64_t root_obs;
  {
    Scope s(true);
    root_obs = e->obs_hash();
  }

  auto guarded_random = [&](uint64_t s, long &done, long &invariant_bad, long &diag, long &exc, uint64_t *digest,
                            const std::function<void()> *tick = nullptr) {
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
      fprintf(stderr, "exception game=%d(%s/%s) index=%d: %s\n", pos.game, g.deck0.c_str(), g.deck1.c_str(), pos.index, ex.what());
    }
    if (g_diagnostics != diag0) {
      ++diag;
      fprintf(stderr, "diagnostic game=%d(%s/%s) index=%d: %s\n", pos.game, g.deck0.c_str(), g.deck1.c_str(), pos.index,
              g_last_diagnostic);
    }
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
        auto bindings = DealAccess::capture_bindings(e, viewer);
        for (int p = 0; p < 2; ++p) {
          auto &v = f->player[p].list_main;
          uint64_t y = mix(deal_seed, p);
          for (int k = static_cast<int>(v.size()) - 1; k > 0; --k) std::swap(v[k], v[splitmix(y) % (k + 1)]);
          f->reset_sequence(p, LOCATION_DECK);
        }
        DealAccess::rebind(e, viewer, bindings);
        DealAccess::reseed(e, static_cast<uint32_t>(deal_seed), static_cast<uint32_t>(deal_seed >> 32));
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

  // The true world must satisfy the rules the deal enforces. If not, the deal could never produce it.
  snapshot_restore(snap);
  const std::vector<uint32_t> attack_state_before = slot_attack_state(e);
  {
    int violations = hard_rule_violations(e, viewer, t->memory, t->baseline);
    o.r2_true_hard_rule_bad += violations;
    HiddenSlots hs = collect_hidden(e, viewer, t->memory, t->baseline);
    for (card *c : hs.opp_slots) {
      SlotRule rule = rule_of_slot(c, t->memory);
      if (rule.cls == kFree) continue;
      if (rule.cls >= kNormalSetNoTribute) ++o.r2_set_slots;
      if (!rule_fits(c->data.code, rule, t->baseline)) {
        ++o.r2_true_hard_rule_bad;
        if (g_redeal_log)
          fprintf(g_redeal_log, "TRUE_WORLD_REJECTED game=%d(%s/%s) index=%d viewer=%d code=%u class=%d\n", pos.game, g.deck0.c_str(), g.deck1.c_str(), pos.index, viewer, c->data.code, int(rule.cls));
      }
    }
  }
  // R2: hand, set cards and decks, from public information plus a seed.
  int position_ok = 0;
  for (int i = 0; i < deals; ++i) {
    uint64_t first = 0;
    for (int rep = 0; rep < 2; ++rep) {
      snapshot_restore(snap);
      uint64_t deal_seed = mix(policy_seeds[i], 2);
      double t0 = now_us();
      DealReport dr = determinize(e, viewer, deal_seed, t->memory, t->baseline, assumed);
      double t1 = now_us();
      if (rep == 0) {
        ++o.r2_attempts;
        o.r2_ok += dr.ok();
        ++o.r2_refused[int(dr.status)];
        o.r2_us += t1 - t0;
        o.r2_moved += dr.moved;
        o.r2_known_pinned += dr.known_pinned;
        o.r2_extra_kept += dr.opponent_extra_not_redealt;
        o.r2_banished_kept += dr.facedown_banished_not_redealt;
        o.r2_rows_scrubbed += dr.rows_scrubbed;
        o.r2_sets_rebuilt += dr.sets_rebuilt;
        o.r2_rebound += dr.deck_options_rebound;
        position_ok += dr.ok();
        if (dr.status == DealStatus::kHiddenObjectState) {
          static const char *kinds[] = {"relations", "counters", "equip", "effect target", "overlay", "battled", "attacked", "added effect"};
          for (int k = 0; k < 8; ++k)
            if (!std::strcmp(g_state_kind, kinds[k])) ++o.r2_state_kind[k];
        }
      }
      if (!dr.ok()) {
        if (rep == 0 && g_redeal_log)
          fprintf(g_redeal_log, "REFUSED %s why=%s (%s) game=%d(%s/%s) index=%d viewer=%d msg=%d turn=%d\n", deal_status_name(dr.status), dr.status == DealStatus::kCoreReference ? g_reference_kind : "-", dr.status == DealStatus::kCoreReference ? g_reference_detail : "", pos.game,
                  g.deck0.c_str(), g.deck1.c_str(), pos.index, viewer, e->msg(), e->turn());
        continue;
      }
      if (rep == 0) {
        // The viewer's own view of the root must not change at all when hidden cards are re-dealt.
        uint64_t after;
        {
          Scope s(true);
          after = e->obs_hash();
        }
        if (after != root_obs) {
          ++o.r2_root_obs_changed;
          if (g_redeal_log)
            fprintf(g_redeal_log, "ROOT_OBS_CHANGED game=%d(%s/%s) index=%d viewer=%d msg=%d\n", pos.game, g.deck0.c_str(), g.deck1.c_str(),
                    pos.index, viewer, e->msg());
        }
      }
      if (rep == 0 && slot_attack_state(e) != attack_state_before) {
        ++o.r2_slot_state_changed;
        if (g_redeal_log) fprintf(g_redeal_log, "SLOT_STATE game=%d(%s/%s) index=%d viewer=%d\n", pos.game, g.deck0.c_str(), g.deck1.c_str(), pos.index, viewer);
      }
      if (rep == 0) {
        int violations = hard_rule_violations(e, viewer, t->memory, t->baseline);
        o.r2_hard_rule_bad += violations;
        if (violations && g_redeal_log)
          fprintf(g_redeal_log, "HARD_RULE game=%d(%s/%s) index=%d viewer=%d violations=%d\n", pos.game, g.deck0.c_str(), g.deck1.c_str(), pos.index, viewer, violations);
      }
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
  o.r2_positions_refused_all += position_ok == 0;

  // R3: construct the same hidden assignment as a natural alternate history, by rigging the initial
  // deck and replaying the recorded actions matched by meaning. This is the oracle for R2. It uses the
  // opponent's true private responses, which a real engine does not have, so its acceptance rate is an
  // upper bound.
  uint32_t deck_seed, duel_seed;
  FmEnv::production_seeds(env_seed_of(g), deck_seed, duel_seed);
  auto *canon_a = new CanonRun, *canon_b = new CanonRun, *raw_a_run = new CanonRun, *raw_b_run = new CanonRun;
  Snapshot alt;
  for (int i = 0; i < std::max(1, deals / 2); ++i) {
    uint64_t y = mix(policy_seeds[i], 3);
    int opp = 1 - viewer;
    snapshot_restore(snap);
    HiddenSlots hs = collect_hidden(e, viewer, t->memory, t->baseline);
    std::vector<card *> movable[2];
    movable[viewer] = hs.own_deck;
    movable[opp] = hs.opp_slots;
    bool eligible = hs.refuse == DealStatus::kOk;
    if (eligible) {
      // Same conditions as a deal: this oracle compares against worlds a deal could produce.
      std::vector<card *> all(hs.opp_slots);
      if (DealAccess::unbound_deck_options(e) || references_hidden(e, HiddenSet(all)) || own_deck_referenced(e, hs)) eligible = false;
      for (card *c : hs.opp_slots)
        if (object_state_kind(c, t->baseline)) eligible = false;
    }
    for (int p = 0; p < 2 && eligible; ++p)
      for (card *c : movable[p])
        if (std::find(t->main_objects[p].begin(), t->main_objects[p].end(), c) == t->main_objects[p].end()) eligible = false;
    if (!eligible) continue;
    std::vector<card *> assign[2];
    bool possible = true;
    for (int p = 0; p < 2 && possible; ++p) possible = rig_assignment(movable[p], y, t->memory, t->baseline, assign[p]);
    if (!possible) continue;
    ++o.r3_samples;
    ++o.bucket_samples[pos.bucket];
    // Initial deck: each movable object's original index now holds the code dealt to its slot.
    std::vector<uint32_t> rig[2] = {t->main_codes[0], t->main_codes[1]};
    for (int p = 0; p < 2; ++p)
      for (size_t k = 0; k < movable[p].size(); ++k) {
        auto it = std::find(t->main_objects[p].begin(), t->main_objects[p].end(), movable[p][k]);
        rig[p][it - t->main_objects[p].begin()] = assign[p][k]->data.code;
      }
    auto holders = movable[0];
    holders.insert(holders.end(), movable[1].begin(), movable[1].end());
    auto assigned = assign[0];
    assigned.insert(assigned.end(), assign[1].begin(), assign[1].end());

    // Naive variant: replay raw indices against the rigged deal.
    {
      ++o.r3_index_samples;
      struct Survive {
        int ok;
      };
      try {
        Survive sv = in_child<Survive>([&] {
          Survive r{0};
          try {
            arena_clear();
            FmEnv *w = new_env(g, env_seed_of(g));
            Scope sc(true);
            w->reset_rigged(duel_seed, rig[0], rig[1], g.deck0, g.deck1);
            for (int a = 0; a < pos.index; ++a) {
              if (w->done() || w->to_play() != g.to_play[a] || static_cast<int>(w->actions().size()) != g.options[a]) return r;
              w->act(g.actions[a]);
            }
            r.ok = !w->done();
          } catch (...) {
          }
          return r;
        });
        o.r3_index_survive += sv.ok;
      } catch (const std::exception &) {
      }
    }

    // Semantic variant. World W: a natural duel in which the other cards were always there.
    arena_clear();
    double t0 = now_us();
    FmEnv *w = new_env(g, env_seed_of(g));
    auto *wt = new TrueDuel;
    wt->e = w;
    start_true_duel(*wt);
    bool accepted = true;
    int layout_div = -1;
    try {
      {
        Scope sc(true);
        w->reset_rigged(duel_seed, rig[0], rig[1], g.deck0, g.deck1);
      }
      capture_baseline(w, wt->baseline);
      for (int a = 0; a < pos.index && accepted; ++a) {
        if (w->done()) {
          ++o.r3_reject_end;
          accepted = false;
          break;
        }
        if (w->to_play() != t->to_play[a] || w->msg() != t->msg[a]) {
          ++o.r3_reject_prompt;
          accepted = false;
          break;
        }
        note_public(w, wt->memory);
        if (layout_div < 0 && layout_hash(w) != t->layout_chain[a]) layout_div = a;
        if (t->to_play[a] == viewer && offered_hash(w) != t->offered_chain[a]) {
          ++o.r3_reject_offered;
          accepted = false;
          break;
        }
        if (public_hash(w, viewer, wt->memory) != t->public_chain[a]) {
          ++o.r3_reject_public;
          accepted = false;
          break;
        }
        if (t->to_play[a] == viewer) {
          Scope sc(true);
          if (w->obs_hash() != t->viewer_obs[a]) {
            ++o.r3_reject_obs;
            accepted = false;
            break;
          }
        }
        int found = -1;
        {
          Scope off(false);
          for (size_t k = 0; k < w->actions().size(); ++k)
            if (descriptor(w, w->actions()[k]) == t->chosen[a]) {
              found = static_cast<int>(k);
              break;
            }
        }
        if (found < 0) {
          ++o.r3_reject_action;
          accepted = false;
          break;
        }
        RowMarker marker = before_true_step(w, wt->memory, found);
        {
          Scope sc(true);
          w->act(found);
        }
        after_true_step(w, wt->memory, marker);
      }
    } catch (const std::exception &) {
      ++o.r3_exceptions;
      accepted = false;
    }
    stop_true_duel();
    if (accepted && !w->done()) note_public(w, wt->memory);
    if (accepted && !w->done() && public_hash(w, viewer, wt->memory) != t->public_chain[pos.index]) {
      ++o.r3_reject_public;
      accepted = false;
    }
    if (accepted && !w->done() &&
        (w->to_play() != viewer || w->msg() != t->msg[pos.index] || offered_hash(w) != t->offered_chain[pos.index])) {
      ++o.r3_reject_offered;
      accepted = false;
    }
    if (accepted && !w->done()) {
      Scope sc(true);
      if (w->obs_hash() != t->viewer_obs[pos.index]) {
        ++o.r3_reject_obs;
        accepted = false;
      }
    }
    if (!accepted || w->done()) {
      if (accepted) ++o.r3_reject_end;
      delete wt;
      continue;
    }
    o.r3_accept_us += now_us() - t0;
    o.r3_accept_decisions += pos.index;
    ++o.r3_accept;
    ++o.bucket_accept[pos.bucket];
    if (layout_div < 0 && layout_hash(w) != t->layout_chain[pos.index]) layout_div = pos.index;
    bool layout_same = layout_div < 0;
    o.r3_layout_same += layout_same;
    // The oracle asks whether the slot swap reproduces rule state, so both worlds get one future random
    // stream. Whether their streams had already parted is counted: hidden draws can consume it.
    o.r3_rng_same += rng_probe(w) == t->rng_chain[pos.index];
    {
      Scope sc(true);
      DealAccess::reseed(w, static_cast<uint32_t>(y), static_cast<uint32_t>(y >> 32));
    }
    snapshot_take(alt);
    auto natural_board = board_codes(w);
    uint64_t run_seed = mix(y, 7);
    uint64_t nat_digest = 0;
    int nat_winner = -1;
    try {
      run_canonical(w, run_seed, cap, *canon_a, false, false);
      nat_digest = canon_a->digest;
      nat_winner = canon_a->winner;
    } catch (const std::exception &) {
      ++o.r3_compare_exceptions;
      delete wt;
      continue;
    }
    // The same assignment by slot swap, in the world the deal runs on.
    auto apply_mutated = [&](bool inherit) {
      snapshot_restore(snap);
      auto bindings = DealAccess::capture_bindings(e, viewer);
      LiveOrder live_order = capture_live_order(e);
      apply_assignment(e->core(), holders, assigned, inherit, t->baseline.load_id_limit);
      restore_live_order(e, live_order);
      DealAccess::rebind(e, viewer, bindings);
      Scope sc(true);
      DealAccess::reseed(e, static_cast<uint32_t>(y), static_cast<uint32_t>(y >> 32));
    };
    apply_mutated(false);
    auto mutated_board = board_codes(e);
    o.r3_board_match += layout_same && mutated_board == natural_board;
    try {
      run_canonical(e, run_seed, cap, *canon_b, false, false);
      o.r3_canon_match += layout_same && canon_b->digest == nat_digest;
    } catch (const std::exception &) {
      ++o.r3_compare_exceptions;
    }
    apply_mutated(true);
    try {
      run_canonical(e, run_seed, cap, *canon_b, false, false);
      bool match = canon_b->digest == nat_digest;
      o.r3_canon_match_cardid += layout_same && match;
      o.r3_winner_match += layout_same && canon_b->winner == nat_winner;
      if (layout_same && !match) {
        int div = first_divergence(*canon_a, *canon_b);
        if (div >= 0 && div < 10) ++o.r3_div_lt10;
        if (div >= 0 && div < 100) ++o.r3_div_lt100;
        if (g_redeal_log) {
          fprintf(g_redeal_log, "R3DIVERGE game=%d(%s/%s seed=%u env=%d) index=%d viewer=%d first_divergence=%d lens=%d/%d seat=%d\n",
                  pos.game, g.deck0.c_str(), g.deck1.c_str(), g.seed, g.env, pos.index, viewer, div, canon_a->length, canon_b->length,
                  div >= 0 && div < kChainCap ? canon_a->seat[div] : -1);
          describe_pair(g_redeal_log, "R3FIRST", g, pos, viewer, w, e, [&] { snapshot_restore(alt); }, [&] { apply_mutated(true); }, run_seed, div, true);
        }
      }
    } catch (const std::exception &) {
      ++o.r3_compare_exceptions;
    }
    try {
      snapshot_restore(alt);
      run_canonical(w, run_seed, cap, *raw_a_run, true, true);
      apply_mutated(true);
      run_canonical(e, run_seed, cap, *raw_b_run, true, true);
      o.r3_raw_match_cardid += layout_same && raw_b_run->digest == raw_a_run->digest;
      o.r3_viewer_obs_match += layout_same && first_obs_divergence(*raw_a_run, *raw_b_run, viewer) == -1;
    } catch (const std::exception &) {
      ++o.r3_compare_exceptions;
    }

    if (g_search_pair) {
      g_search_pair(g, pos, viewer, e, *t, snap, w, *wt, alt, assumed);
      snapshot_restore(snap);
    }
    // R4 non-interference. World A is the real duel, world B the natural duel with other hidden cards
    // and the same public history, which the viewer's every observation in it confirms. Same deal seed
    // in both: if a sampled world depends only on public inputs, the worlds must coincide, in hidden
    // slots, in prompts, and in the bytes of everything the viewer later observes.
    try {
      for (int seed_i = 0; seed_i < 2; ++seed_i) {
        uint64_t deal_seed = mix(mix(y, 11), seed_i);
        uint64_t policy = mix(mix(y, 13), seed_i);
        ++o.r4_pairs;
        snapshot_restore(snap);
        DealReport a = determinize(e, viewer, deal_seed, t->memory, t->baseline, assumed);
        snapshot_restore(alt);
        DealReport b = determinize(w, viewer, deal_seed, wt->memory, wt->baseline, assumed);
        if (a.ok() != b.ok()) {
          ++o.r4_refused_one;
          if (g_redeal_log)
            fprintf(g_redeal_log, "R4REFUSE game=%d(%s/%s) index=%d viewer=%d statuses=%s/%s why=%s (%s)\n", pos.game, g.deck0.c_str(), g.deck1.c_str(),
                    pos.index, viewer, deal_status_name(a.status), deal_status_name(b.status), g_reference_kind, g_reference_detail);
          continue;
        }
        if (!a.ok()) {
          ++o.r4_refused_both;
          continue;
        }
        auto prepare_a = [&] {
          snapshot_restore(snap);
          determinize(e, viewer, deal_seed, t->memory, t->baseline, assumed);
        };
        auto prepare_b = [&] {
          snapshot_restore(alt);
          determinize(w, viewer, deal_seed, wt->memory, wt->baseline, assumed);
        };
        prepare_a();
        auto board_a = board_codes(e);
        run_canonical(e, policy, cap, *canon_a, false, false);
        prepare_b();
        auto board_b = board_codes(w);
        run_canonical(w, policy, cap, *canon_b, false, false);
        bool slots = board_a == board_b;
        bool canon = canon_a->digest == canon_b->digest;
        prepare_a();
        run_canonical(e, policy, cap, *raw_a_run, true, true);
        prepare_b();
        run_canonical(w, policy, cap, *raw_b_run, true, true);
        bool raw = raw_a_run->digest == raw_b_run->digest;
        int obs_viewer_div = first_obs_divergence(*raw_a_run, *raw_b_run, viewer);
        int obs_all_div = first_obs_divergence(*raw_a_run, *raw_b_run, -1);
        o.r4_slots_equal += slots;
        o.r4_canon_equal += canon;
        o.r4_raw_equal += raw;
        o.r4_winner_equal += raw_a_run->winner == raw_b_run->winner;
        o.r4_root_obs_equal += raw_a_run->obs[0] == raw_b_run->obs[0];
        o.r4_next8_viewer_obs_equal += first_seat_obs_equal(*raw_a_run, *raw_b_run, viewer, 8);
        o.r4_viewer_obs_equal += obs_viewer_div == -1;
        o.r4_all_obs_equal += obs_all_div == -1;
        if (layout_same) {
          ++o.r4_ls_pairs;
          o.r4_ls_slots += slots;
          o.r4_ls_canon += canon;
          o.r4_ls_raw += raw;
          o.r4_ls_obs += obs_all_div == -1;
        }
        if ((!slots || !canon || !raw || obs_all_div != -1) && g_redeal_log) {
          fprintf(g_redeal_log,
                  "R4DIFF game=%d(%s/%s seed=%u env=%d) index=%d viewer=%d layout_same=%d slots=%d canon=%d raw=%d obs_viewer_div=%d obs_all_div=%d "
                  "lens=%d/%d moved=%d/%d msg=%d\n",
                  pos.game, g.deck0.c_str(), g.deck1.c_str(), g.seed, g.env, pos.index, viewer, layout_same, slots, canon, raw, obs_viewer_div,
                  obs_all_div, raw_a_run->length, raw_b_run->length, a.moved, b.moved, t->msg[pos.index]);
          int at = obs_all_div >= 0 ? obs_all_div : 0;
          describe_pair(g_redeal_log, "R4FIRST", g, pos, viewer, e, w, prepare_a, prepare_b, policy, at);
          if (!canon) describe_pair(g_redeal_log, "R4CANON", g, pos, viewer, e, w, prepare_a, prepare_b, policy, first_chain_divergence(*canon_a, *canon_b), true);
        }
      }
      // Control: no re-deal, same policy. Must usually differ or the test has no power.
      uint64_t deal_seed = mix(y, 17), policy = mix(y, 19);
      snapshot_restore(snap);
      {
        Scope sc(true);
        DealAccess::reseed(e, static_cast<uint32_t>(deal_seed), static_cast<uint32_t>(deal_seed >> 32));
      }
      run_canonical(e, policy, cap, *raw_a_run, true, true);
      snapshot_restore(alt);
      {
        Scope sc(true);
        DealAccess::reseed(w, static_cast<uint32_t>(deal_seed), static_cast<uint32_t>(deal_seed >> 32));
      }
      run_canonical(w, policy, cap, *raw_b_run, true, true);
      ++o.r4_control_pairs;
      o.r4_control_chain_equal += raw_a_run->digest == raw_b_run->digest;
      o.r4_control_viewer_obs_equal += first_obs_divergence(*raw_a_run, *raw_b_run, viewer) == -1;
    } catch (const std::exception &ex) {
      ++o.r4_failed;
      if (g_redeal_log)
        fprintf(g_redeal_log, "R4EXCEPTION game=%d(%s/%s) index=%d: %s\n", pos.game, g.deck0.c_str(), g.deck1.c_str(), pos.index, ex.what());
    }
    delete wt;
  }
  // R4W, the wider family. R4's worlds keep one private timeline, so a world with a hand trap the other lacks is never
  // paired. Here two worlds need only one public history and one viewer view. Nothing about the first world filters the
  // sample. At an opposing prompt the other world lacks, a declined choice is skipped. At one only it has, it declines.
  for (int i = 0; i < std::max(1, deals / 2); ++i) {
    uint64_t y = mix(policy_seeds[i], 23);
    int opp = 1 - viewer;
    snapshot_restore(snap);
    HiddenSlots hs = collect_hidden(e, viewer, t->memory, t->baseline);
    std::vector<card *> movable[2], assign[2];
    movable[viewer] = hs.own_deck;
    movable[opp] = hs.opp_slots;
    bool eligible = true;
    for (int p = 0; p < 2 && eligible; ++p)
      for (card *c : movable[p])
        if (std::find(t->main_objects[p].begin(), t->main_objects[p].end(), c) == t->main_objects[p].end()) eligible = false;
    for (int p = 0; p < 2 && eligible; ++p) eligible = rig_assignment(movable[p], y, t->memory, t->baseline, assign[p]);
    if (!eligible) continue;
    ++o.w_samples;
    std::vector<uint32_t> rig[2] = {t->main_codes[0], t->main_codes[1]};
    for (int p = 0; p < 2; ++p)
      for (size_t k = 0; k < movable[p].size(); ++k) {
        auto it = std::find(t->main_objects[p].begin(), t->main_objects[p].end(), movable[p][k]);
        rig[p][it - t->main_objects[p].begin()] = assign[p][k]->data.code;
      }
    arena_clear();
    FmEnv *w = new_env(g, env_seed_of(g));
    auto *wt = new TrueDuel;
    wt->e = w;
    start_true_duel(*wt);
    bool accepted = true;
    int extra = 0, skipped = 0;
    try {
      {
        Scope sc(true);
        w->reset_rigged(duel_seed, rig[0], rig[1], g.deck0, g.deck1);
      }
      capture_baseline(w, wt->baseline);
      int a = 0;
      auto step_world = [&](int option) {
        RowMarker marker = before_true_step(w, wt->memory, option);
        {
          Scope sc(true);
          w->act(option);
        }
        after_true_step(w, wt->memory, marker);
      };
      auto find_action = [&](const std::string &wanted) {
        Scope off(false);
        for (size_t k = 0; k < w->actions().size(); ++k)
          if (descriptor(w, w->actions()[k]) == wanted) return static_cast<int>(k);
        return -1;
      };
      for (;;) {
        if (w->done()) {
          ++o.w_reject_end;
          accepted = false;
          break;
        }
        note_public(w, wt->memory);
        if (w->to_play() == viewer) {
          // Prompts only the true world had, all declined, leave nothing public.
          while (a < pos.index && t->to_play[a] != viewer) {
            if (!t->declined[a]) {
              ++o.w_reject_private;
              accepted = false;
              break;
            }
            ++skipped;
            ++a;
          }
          if (!accepted) break;
          if (w->msg() != t->msg[a] || offered_hash(w) != t->offered_chain[a] || public_hash(w, viewer, wt->memory) != t->public_chain[a]) {
            ++o.w_reject_public;
            accepted = false;
            break;
          }
          uint64_t seen;
          {
            Scope sc(true);
            seen = w->obs_hash();
          }
          if (seen != t->viewer_obs[a]) {
            ++o.w_reject_viewer;
            accepted = false;
            break;
          }
          if (a == pos.index) break;
          int found = find_action(t->chosen[a]);
          if (found < 0) {
            ++o.w_reject_viewer;
            accepted = false;
            break;
          }
          step_world(found);
          ++a;
        } else {
          int found = a < pos.index && t->to_play[a] == opp && t->msg[a] == w->msg() ? find_action(t->chosen[a]) : -1;
          if (found >= 0) {
            step_world(found);
            ++a;
            continue;
          }
          int decline = -1;
          for (size_t k = 0; k < w->actions().size(); ++k)
            if (w->actions()[k].act_ == ygopro::ActionAct::Cancel) decline = static_cast<int>(k);
          if (decline >= 0 && extra < 500) {
            ++extra;
            step_world(decline);
            continue;
          }
          if (a < pos.index && t->to_play[a] == opp && t->declined[a]) {
            ++skipped;
            ++a;
            continue;
          }
          ++o.w_reject_private;
          accepted = false;
          break;
        }
      }
    } catch (const std::exception &ex) {
      ++o.w_failed;
      accepted = false;
      if (g_redeal_log) fprintf(g_redeal_log, "R4WEXCEPTION game=%d index=%d: %s\n", pos.game, pos.index, ex.what());
    }
    stop_true_duel();
    if (!accepted) {
      delete wt;
      continue;
    }
    ++o.w_accept;
    o.w_with_extra += extra > 0 || skipped > 0;
    o.w_extra_prompts += extra;
    o.w_skipped_prompts += skipped;
    snapshot_take(alt);
    bool differs_privately = extra > 0 || skipped > 0;
    try {
      for (int seed_i = 0; seed_i < 2; ++seed_i) {
        uint64_t deal_seed = mix(mix(y, 29), seed_i), policy = mix(mix(y, 31), seed_i);
        auto prepare_a = [&] {
          snapshot_restore(snap);
          return determinize(e, viewer, deal_seed, t->memory, t->baseline, assumed);
        };
        auto prepare_b = [&] {
          snapshot_restore(alt);
          return determinize(w, viewer, deal_seed, wt->memory, wt->baseline, assumed);
        };
        ++o.w_pairs;
        o.w_x_pairs += differs_privately;
        DealReport a = prepare_a();
        std::string why_a = a.status == DealStatus::kHiddenObjectState ? g_state_kind : g_reference_kind;
        DealReport b = prepare_b();
        std::string why_b = b.status == DealStatus::kHiddenObjectState ? g_state_kind : g_reference_kind;
        o.w_refused_a += !a.ok();
        o.w_refused_b += !b.ok();
        if (a.ok() != b.ok()) {
          ++o.w_refused_one;
          o.w_x_refused_one += differs_privately;
          if (g_redeal_log)
            fprintf(g_redeal_log, "R4WREFUSE game=%d(%s/%s) index=%d viewer=%d private=%d statuses=%s(%s)/%s(%s) detail=%s\n", pos.game, g.deck0.c_str(), g.deck1.c_str(),
                    pos.index, viewer, int(differs_privately), deal_status_name(a.status), a.ok() ? "" : why_a.c_str(), deal_status_name(b.status), b.ok() ? "" : why_b.c_str(), g_reference_detail);
          continue;
        }
        if (!a.ok()) {
          ++o.w_refused_both;
          continue;
        }
        prepare_a();
        auto board_a = board_codes(e);
        o.w_hard_rule_bad += hard_rule_violations(e, viewer, t->memory, t->baseline);
        run_canonical(e, policy, cap, *canon_a, false, false);
        prepare_b();
        auto board_b = board_codes(w);
        o.w_hard_rule_bad += hard_rule_violations(w, viewer, wt->memory, wt->baseline);
        run_canonical(w, policy, cap, *canon_b, false, false);
        bool slots = board_a == board_b, canon = canon_a->digest == canon_b->digest;
        prepare_a();
        run_canonical(e, policy, cap, *raw_a_run, true, true);
        prepare_b();
        run_canonical(w, policy, cap, *raw_b_run, true, true);
        bool raw = raw_a_run->digest == raw_b_run->digest;
        int obs_viewer_div = first_obs_divergence(*raw_a_run, *raw_b_run, viewer), obs_all_div = first_obs_divergence(*raw_a_run, *raw_b_run, -1);
        o.w_slots_equal += slots;
        o.w_canon_equal += canon;
        o.w_raw_equal += raw;
        o.w_winner_equal += raw_a_run->winner == raw_b_run->winner;
        o.w_root_obs_equal += raw_a_run->obs[0] == raw_b_run->obs[0];
        o.w_next8_viewer_obs_equal += first_seat_obs_equal(*raw_a_run, *raw_b_run, viewer, 8);
        o.w_viewer_obs_equal += obs_viewer_div == -1;
        o.w_all_obs_equal += obs_all_div == -1;
        o.w_x_slots += differs_privately && slots;
        o.w_x_canon += differs_privately && canon;
        o.w_x_raw += differs_privately && raw;
        if ((!slots || !canon || !raw || obs_all_div != -1) && g_redeal_log) {
          fprintf(g_redeal_log, "R4WDIFF game=%d(%s/%s seed=%u env=%d) index=%d viewer=%d extra=%d skipped=%d slots=%d canon=%d raw=%d obs_viewer_div=%d obs_all_div=%d lens=%d/%d\n", pos.game,
                  g.deck0.c_str(), g.deck1.c_str(), g.seed, g.env, pos.index, viewer, extra, skipped, slots, canon, raw, obs_viewer_div, obs_all_div, raw_a_run->length, raw_b_run->length);
          describe_pair(g_redeal_log, "R4WFIRST", g, pos, viewer, e, w, [&] { prepare_a(); }, [&] { prepare_b(); }, policy, obs_all_div >= 0 ? obs_all_div : 0);
          if (!canon) describe_pair(g_redeal_log, "R4WCANON", g, pos, viewer, e, w, [&] { prepare_a(); }, [&] { prepare_b(); }, policy, first_chain_divergence(*canon_a, *canon_b), true);
        }
      }
    } catch (const std::exception &ex) {
      ++o.w_failed;
      if (g_redeal_log) fprintf(g_redeal_log, "R4WEXCEPTION game=%d index=%d: %s\n", pos.game, pos.index, ex.what());
    }
    delete wt;
  }
  delete canon_a;
  delete canon_b;
  delete raw_a_run;
  delete raw_b_run;
  __libc_free(alt.data);

  // R5: cards outside the true decklist. Needed if the opponent's list is unknown. Mechanics only: this
  // path is not offered by the search API.
  static const std::vector<uint32_t> foreign = [] {
    std::vector<uint32_t> v;
    for (const auto &kv : ygopro::main_decks_)
      for (uint32_t c : kv.second) v.push_back(c);
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
  }();
  arena_clear();
  auto *tt = new TrueDuel;
  build_true_duel(g, pos.index, viewer, *tt, false);
  FmEnv *m = tt->e;
  snapshot_take(snap);
  e = m;
  for (int i = 0; i < deals; ++i) {
    snapshot_restore(snap);
    field *f = m->core()->game_field;
    int opp = 1 - viewer;
    uint64_t y = mix(policy_seeds[i], 5);
    std::vector<card *> created;
    HiddenSlots hs = collect_hidden(m, viewer, tt->memory, tt->baseline);
    std::vector<card *> candidates;
    for (card *c : hs.opp_slots)
      if (c->current.location == LOCATION_HAND || c->current.location == LOCATION_DECK) candidates.push_back(c);
    if (hs.refuse != DealStatus::kOk || candidates.empty() || references_hidden(m, HiddenSet(candidates))) continue;
    // Hand first, as the slots come out in Deck-then-hand order.
    std::stable_partition(candidates.begin(), candidates.end(), [](card *c) { return c->current.location == LOCATION_HAND; });
    ++o.r5_attempts;
    double t0 = now_us();
    {
      Scope sc(true);
      int replacements = std::min<int>(3, candidates.size());
      for (int k = 0; k < replacements; ++k) {
        size_t pick = k < static_cast<int>(f->player[opp].list_hand.size()) ? 0 : splitmix(y) % candidates.size();
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
        if (candidates.empty()) break;
      }
      DealAccess::reseed(m, static_cast<uint32_t>(y), static_cast<uint32_t>(y >> 32));
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
  delete t;
  delete tt;
  g_use_arena = false;
}

inline void add_out(RedealOut &t, const RedealOut &o) {
  auto *a = reinterpret_cast<char *>(&t);
  const auto *b = reinterpret_cast<const char *>(&o);
  // Every field is 8 bytes. The doubles are summed separately before this is called.
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
  auto text = [&](const std::string &k) {
    auto it = args.find(k);
    return it == args.end() ? std::string() : it->second;
  };
  int per_cell = get("per-cell", 4), deals = get("deals", 6), cap = get("cap", 3000), workers = get("workers", 4);
  auto positions = sample_positions_per_cell(games, per_cell, 85100059);
  if (get("only-game", -1) >= 0) positions = {Pos{get("only-game", 0), 0, get("only-index", 0)}};  // To diagnose one logged case.
  std::set<std::string> decks_covered;
  for (const auto &p : positions) {
    decks_covered.insert(games[p.game].deck0);
    decks_covered.insert(games[p.game].deck1);
  }
  for (const auto &g : games) (void)spec_for(g);
  std::string log_path = text("log");
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
      if (!log_path.empty()) g_redeal_log = fopen((log_path + "." + std::to_string(w)).c_str(), "w");
      for (size_t i = w; i < positions.size(); i += workers) {
        // Each position in its own process: a crash is counted, not fatal.
        struct Boxed {
          RedealOut o;
        };
        try {
          Boxed b = in_child<Boxed>([&] {
            Boxed r{};
            redeal_position(games, positions[i], deals, cap, r.o);
            if (g_redeal_log) fflush(g_redeal_log);
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
          fprintf(stderr, "CRASH game=%d(%s/%s) index=%d: %s\n", positions[i].game, games[positions[i].game].deck0.c_str(),
                  games[positions[i].game].deck1.c_str(), positions[i].index, ex.what());
        }
      }
      if (g_redeal_log) fclose(g_redeal_log);
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
  std::string refused = "{";
  for (int s = 0; s < int(DealStatus::kCount); ++s) {
    refused += std::string(s ? "," : "") + "\"" + deal_status_name(DealStatus(s)) + "\":" + std::to_string(t.r2_refused[s]);
  }
  refused += "}";
  std::string state_kinds = "{";
  {
    static const char *kinds[] = {"relations", "counters", "equip", "effect_target", "overlay", "battled", "attacked", "added_effect"};
    for (int k = 0; k < 8; ++k) state_kinds += std::string(k ? "," : "") + "\"" + kinds[k] + "\":" + std::to_string(t.r2_state_kind[k]);
    state_kinds += "}";
  }
#define J(name) "\"" #name "\":%ld,"
  char buf[9000];
  snprintf(buf, sizeof buf,
           "{\"redeal\":true,\"lists_covered\":%zu,\"positions\":%ld,\"crashed_positions\":%ld,\"deals\":%d,\"cap\":%d,"
           "\"r1\":{" J(playouts) J(done) J(invariant_bad) J(diag) J(exceptions) J(changed) J(repeat_bad) "\"_\":0},"
           "\"r2\":{" J(attempts) J(ok) J(playouts) J(done) J(invariant_bad) J(diag) J(exceptions) J(repeat_bad)
           J(changed) J(moved) J(known_pinned) J(opponent_extra_not_redealt) J(facedown_banished_not_redealt) J(rows_scrubbed) J(sets_rebuilt)
           J(deck_options_rebound) J(root_obs_changed) J(positions_refused_every_deal) J(hard_rule_violations) J(true_world_rule_violations) J(set_slots_seen) J(slot_state_changed) "\"state_kinds\":%s,\"status\":%s,\"us_per_deal\":%.1f},"
           "\"r3\":{" J(samples) J(accept) J(reject_prompt) J(reject_offered) J(reject_public) J(reject_obs) J(reject_action) J(reject_end) J(exceptions)
           J(index_samples) J(index_survive) J(layout_same) J(rng_same) J(board_match) J(canon_match) J(canon_match_cardid)
           J(raw_match_cardid) J(winner_match) J(viewer_obs_match) J(compare_exceptions) J(div_lt10) J(div_lt100)
           "\"accept_early\":\"%ld/%ld\",\"accept_mid\":\"%ld/%ld\",\"accept_late\":\"%ld/%ld\",\"ms_per_accepted\":%.2f},"
           "\"r4\":{" J(pairs) J(refused_both) J(refused_one_world_only) J(failed) J(slots_equal) J(canon_equal) J(raw_equal) J(winner_equal) J(root_obs_equal)
           J(next8_viewer_obs_equal) J(viewer_obs_equal) J(all_obs_equal) J(control_pairs) J(control_chain_equal) J(control_viewer_obs_equal)
           J(layout_same_pairs) J(layout_same_slots) J(layout_same_canon) J(layout_same_raw) J(layout_same_obs) "\"_\":0},"
           "\"r4w\":{" J(samples) J(accept) J(accept_with_private_difference) J(extra_prompts) J(skipped_prompts) J(reject_private) J(reject_viewer) J(reject_public) J(reject_end) J(failed) J(pairs) J(refused_a) J(refused_b) J(refused_both)
           J(refused_one_world_only) J(slots_equal) J(canon_equal) J(raw_equal) J(winner_equal) J(root_obs_equal) J(next8_viewer_obs_equal) J(viewer_obs_equal) J(all_obs_equal) J(private_pairs) J(private_refused_one) J(private_slots) J(private_canon) J(private_raw) J(hard_rule_violations) "\"_\":0},"
           "\"r5\":{" J(attempts) J(playouts) J(done) J(invariant_bad) J(diag) J(exceptions) J(cards) J(card_moved)
           "\"us_per_materialise\":%.1f}}",
           decks_covered.size(), t.positions, crashed, deals, cap, t.r1_playouts, t.r1_done, t.r1_invariant_bad, t.r1_diag, t.r1_exceptions,
           t.r1_changed, t.r1_repeat_bad, t.r2_attempts, t.r2_ok, t.r2_playouts, t.r2_done, t.r2_invariant_bad, t.r2_diag, t.r2_exceptions,
           t.r2_repeat_bad, t.r2_changed, t.r2_moved, t.r2_known_pinned, t.r2_extra_kept, t.r2_banished_kept, t.r2_rows_scrubbed,
           t.r2_sets_rebuilt, t.r2_rebound, t.r2_root_obs_changed, t.r2_positions_refused_all, t.r2_hard_rule_bad, t.r2_true_hard_rule_bad, t.r2_set_slots, t.r2_slot_state_changed, state_kinds.c_str(), refused.c_str(),
           r2_us / std::max(1L, t.r2_attempts), t.r3_samples, t.r3_accept, t.r3_reject_prompt, t.r3_reject_offered, t.r3_reject_public, t.r3_reject_obs,
           t.r3_reject_action, t.r3_reject_end, t.r3_exceptions, t.r3_index_samples, t.r3_index_survive, t.r3_layout_same, t.r3_rng_same, t.r3_board_match,
           t.r3_canon_match, t.r3_canon_match_cardid, t.r3_raw_match_cardid, t.r3_winner_match, t.r3_viewer_obs_match, t.r3_compare_exceptions,
           t.r3_div_lt10, t.r3_div_lt100, t.bucket_accept[0], t.bucket_samples[0], t.bucket_accept[1], t.bucket_samples[1], t.bucket_accept[2],
           t.bucket_samples[2], r3_us / 1000 / std::max(1L, t.r3_accept), t.r4_pairs, t.r4_refused_both, t.r4_refused_one, t.r4_failed,
           t.r4_slots_equal, t.r4_canon_equal, t.r4_raw_equal, t.r4_winner_equal, t.r4_root_obs_equal, t.r4_next8_viewer_obs_equal,
           t.r4_viewer_obs_equal, t.r4_all_obs_equal, t.r4_control_pairs, t.r4_control_chain_equal, t.r4_control_viewer_obs_equal, t.r4_ls_pairs,
           t.r4_ls_slots, t.r4_ls_canon, t.r4_ls_raw, t.r4_ls_obs, t.w_samples, t.w_accept, t.w_with_extra, t.w_extra_prompts, t.w_skipped_prompts, t.w_reject_private, t.w_reject_viewer, t.w_reject_public, t.w_reject_end, t.w_failed,
           t.w_pairs, t.w_refused_a, t.w_refused_b, t.w_refused_both, t.w_refused_one, t.w_slots_equal, t.w_canon_equal, t.w_raw_equal, t.w_winner_equal, t.w_root_obs_equal, t.w_next8_viewer_obs_equal, t.w_viewer_obs_equal, t.w_all_obs_equal,
           t.w_x_pairs, t.w_x_refused_one, t.w_x_slots, t.w_x_canon, t.w_x_raw, t.w_hard_rule_bad, t.r5_attempts, t.r5_playouts, t.r5_done, t.r5_invariant_bad, t.r5_diag,
           t.r5_exceptions, t.r5_cards, t.r5_card_moved, r5_us / std::max(1L, t.r5_attempts));
  emit_kv(buf);
  return 0;
}

// Is the deal uniform over legal assignments? The test counts them by brute force, separately from the deal's own
// counting, and compares each code's frequency in one constrained slot and in one free hand slot with exact odds.
inline int cmd_uniform(const std::vector<Game> &games, std::map<std::string, std::string> &args) {
  auto get = [&](const std::string &k, int d) {
    auto it = args.find(k);
    return it == args.end() ? d : std::stoi(it->second);
  };
  int per_cell = get("per-cell", 2), deals = get("deals", 4000);
  auto positions = sample_positions_per_cell(games, per_cell, 85100097);
  struct Totals {
    double chi = 0;
    long dof = 0, positions = 0, over = 0;
  } hand, set_slot;
  for (const Pos &pos : positions) {
    struct Out {
      double chi_hand, chi_set;
      int dof_hand, dof_set, ok;
    };
    Out result{};
    try {
      result = in_child<Out>([&] {
        Out out{};
        const Game &g = games[pos.game];
        int viewer = g.to_play[pos.index], opp = 1 - viewer;
        AssumedList assumed = true_list_of_opponent(g, viewer);
        g_use_arena = true;
        arena_clear();
        auto *t = new TrueDuel;
        build_true_duel(g, pos.index, viewer, *t, false);
        Snapshot snap;
        snapshot_take(snap);
        FmEnv *e = t->e;
        HiddenSlots hs = collect_hidden(e, viewer, t->memory, t->baseline);
        std::vector<card *> constrained;
        int hand_slot = -1;
        size_t n_free = 0;
        std::map<uint32_t, int> pool;
        for (card *c : hs.opp_slots) {
          ++pool[c->data.code];
          SlotRule rule = rule_of_slot(c, t->memory);
          if (rule.cls != kFree) constrained.push_back(c);
          else ++n_free;
          if (hand_slot < 0 && c->current.location == LOCATION_HAND) hand_slot = c->current.sequence;
        }
        if (constrained.size() > 3 || !n_free) return out;
        // Exact odds, by enumeration over codes with multiplicity.
        std::map<uint32_t, double> set_odds, hand_odds;
        double total = 0;
        std::vector<uint32_t> distinct;
        for (const auto &kv : pool) distinct.push_back(kv.first);
        std::function<void(size_t, double, uint32_t)> walk = [&](size_t at, double ways, uint32_t first) {
          if (at == constrained.size()) {
            total += ways;
            if (!constrained.empty()) set_odds[first] += ways;
            for (const auto &kv : pool) hand_odds[kv.first] += ways * kv.second / n_free;
            return;
          }
          SlotRule rule = rule_of_slot(constrained[at], t->memory);
          for (uint32_t code : distinct) {
            if (!pool[code] || !rule_fits(code, rule, t->baseline)) continue;
            int copies = pool[code]--;
            walk(at + 1, ways * copies, at == 0 ? code : first);
            ++pool[code];
          }
        };
        walk(0, 1.0, 0);
        if (total <= 0) return out;
        std::map<uint32_t, long> seen_set, seen_hand;
        long ok = 0;
        // Each position has its own seeds. With shared ones, two positions with one pool get the same counts, so their
        // chi-squares are not independent and their sum says little.
        const uint64_t position_seed = mix(pos.game, pos.index);
        for (int i = 0; i < deals; ++i) {
          snapshot_restore(snap);
          DealReport r = determinize(e, viewer, mix(mix(85100099, position_seed), i), t->memory, t->baseline, assumed);
          if (!r.ok()) continue;
          ++ok;
          field *f = e->core()->game_field;
          if (hand_slot >= 0) ++seen_hand[f->player[opp].list_hand[hand_slot]->data.code];
        }
        if (!ok) return out;
        auto score = [&](const std::map<uint32_t, double> &odds, const std::map<uint32_t, long> &seen, double &chi, int &dof) {
          double pooled_expected = 0, pooled_seen = 0;
          int bins = 0;
          for (const auto &kv : odds) {
            double expected = ok * kv.second / total;
            auto it = seen.find(kv.first);
            double got = it == seen.end() ? 0 : it->second;
            if (expected < 5) {
              pooled_expected += expected;
              pooled_seen += got;
              continue;
            }
            chi += (got - expected) * (got - expected) / expected;
            ++bins;
          }
          if (pooled_expected >= 5) {
            chi += (pooled_seen - pooled_expected) * (pooled_seen - pooled_expected) / pooled_expected;
            ++bins;
          }
          dof = std::max(0, bins - 1);
        };
        if (hand_slot >= 0) score(hand_odds, seen_hand, out.chi_hand, out.dof_hand);
        if (!constrained.empty()) {
          // Deal again, reading the first constrained slot this time. The object may have moved in the last deal.
          snapshot_restore(snap);
          int location = constrained[0]->current.location, sequence = constrained[0]->current.sequence;
          ok = 0;
          for (int i = 0; i < deals; ++i) {
            snapshot_restore(snap);
            DealReport r = determinize(e, viewer, mix(mix(85100101, position_seed), i), t->memory, t->baseline, assumed);
            if (!r.ok()) continue;
            ++ok;
            ++seen_set[card_at(e->core()->game_field, opp, location, sequence)->data.code];
          }
          if (ok) score(set_odds, seen_set, out.chi_set, out.dof_set);
        }
        out.ok = 1;
        return out;
      });
    } catch (const std::exception &) {
      continue;
    }
    if (!result.ok) continue;
    auto add = [](Totals &totals, double chi, int dof) {
      if (!dof) return;
      ++totals.positions;
      totals.chi += chi;
      totals.dof += dof;
      if (chi > dof + 3 * std::sqrt(2.0 * dof)) ++totals.over;
    };
    add(hand, result.chi_hand, result.dof_hand);
    add(set_slot, result.chi_set, result.dof_set);
  }
  char buf[400];
  snprintf(buf, sizeof buf,
           "{\"uniform\":true,\"deals_per_position\":%d,\"hand\":{\"positions\":%ld,\"chi_square\":%.1f,\"dof\":%ld,\"ratio\":%.3f,\"over_three_sigma\":%ld},"
           "\"constrained_slot\":{\"positions\":%ld,\"chi_square\":%.1f,\"dof\":%ld,\"ratio\":%.3f,\"over_three_sigma\":%ld}}",
           deals, hand.positions, hand.chi, hand.dof, hand.chi / std::max(1L, hand.dof), hand.over, set_slot.positions, set_slot.chi, set_slot.dof,
           set_slot.chi / std::max(1L, set_slot.dof), set_slot.over);
  emit_kv(buf);
  return 0;
}

// Every face-down slot at every decision of every game, for both viewers: the true card must obey the rules the deal
// enforces, or the deal could never produce the true world. Also counts how the slots were filled.
inline int cmd_rules(const std::vector<Game> &games, std::map<std::string, std::string> &args) {
  bool random_play = args.count("policy") && args["policy"] == "random";
  struct Out {
    long decisions, slots, normal_none, normal_one, normal_two, normal_more, other_face_down, back_row, tracked, procedure_open, rejected, refused_slots;
  };
  Out t{};
  for (size_t gi = 0; gi < games.size(); ++gi) {
    const Game &g = games[gi];
    Out r = in_child<Out>([&] {
      Out o{};
      g_use_arena = true;
      arena_clear();
      auto *duel = new TrueDuel;
      duel->e = new_env(g, env_seed_of(g));
      start_true_duel(*duel);
      {
        Scope s(true);
        duel->e->reset();
      }
      capture_baseline(duel->e, duel->baseline);
      FmEnv *e = duel->e;
      uint64_t x = mix(85100113, gi);
      size_t length = random_play ? size_t(3000) : g.actions.size();
      for (size_t i = 0; i < length && !e->done(); ++i) {
        int option = random_play ? static_cast<int>(splitmix(x) % e->actions().size()) : g.actions[i];
        note_public(e, duel->memory);
        ++o.decisions;
        for (int viewer = 0; viewer < 2; ++viewer) {
          o.rejected += hard_rule_violations(e, viewer, duel->memory, duel->baseline);
          auto &pl = e->core()->game_field->player[1 - viewer];
          for (auto *zone : {&pl.list_mzone, &pl.list_szone})
            for (card *c : *zone) {
              if (!c || !(c->current.position & POS_FACEDOWN)) continue;
              if (duel->memory.cls[viewer][c->cardid] != kUntracked) {
                ++o.tracked;
                continue;
              }
              if (c->owner != 1 - viewer || c->equiping_target) {
                ++o.refused_slots;  // A foreign card, or a monster equipped face-down: a deal refuses these.
                continue;
              }
              ++o.slots;
              SlotRule rule = rule_of_slot(c, duel->memory);
              if (!rule_fits(c->data.code, rule, duel->baseline)) {
                ++o.rejected;
                fprintf(stderr, "TRUE_WORLD_REJECTED game=%zu(%s/%s) decision=%zu viewer=%d code=%u class=%d owner=%d previous_location=%d reason_card=%u reason_effect=%u\n", gi, g.deck0.c_str(), g.deck1.c_str(), i,
                        viewer, c->data.code, int(rule.cls), int(c->owner), int(c->previous.location), c->current.reason_card ? c->current.reason_card->data.code : 0,
                        c->current.reason_effect ? c->current.reason_effect->get_handler()->data.code : 0);
              }
              o.normal_none += rule.cls == kNormalSetNoTribute;
              o.normal_one += rule.cls == kNormalSetOneTribute;
              o.normal_two += rule.cls == kNormalSetTwoTributes;
              o.other_face_down += rule.cls == kMonsterSlot;
              o.back_row += rule.cls == kBackRowSlot;
              o.procedure_open += rule.procedure_open;
            }
        }
        RowMarker marker = before_true_step(e, duel->memory, option);
        {
          Scope s(true);
          e->act(option);
        }
        after_true_step(e, duel->memory, marker);
      }
      stop_true_duel();
      return o;
    });
    auto *a = reinterpret_cast<long *>(&t);
    auto *b = reinterpret_cast<long *>(&r);
    for (size_t k = 0; k < sizeof(Out) / sizeof(long); ++k) a[k] += b[k];
  }
  char buf[500];
  snprintf(buf, sizeof buf,
           "{\"rules\":true,\"policy\":\"%s\",\"games\":%zu,\"decisions\":%ld,\"unknown_face_down_slot_views\":%ld,\"normal_set_no_tribute\":%ld,\"normal_set_one_tribute\":%ld,\"normal_set_two_tributes\":%ld,"
           "\"other_face_down_monster\":%ld,\"back_row\":%ld,\"known_face_down\":%ld,\"set_procedure_open\":%ld,\"refused_slot_views\":%ld,\"true_world_rejected\":%ld}",
           random_play ? "random" : "recorded", games.size(), t.decisions, t.slots, t.normal_none, t.normal_one, t.normal_two, t.other_face_down, t.back_row, t.tracked, t.procedure_open, t.refused_slots, t.rejected);
  emit_kv(buf);
  return t.rejected != 0;
}

// The Set Sol found. A Level 2 Normal Set with no Tributes, from a real line, and many deals: nothing above Level 4 fits.
inline int cmd_setfixture(std::map<std::string, std::string> &args) {
  auto get = [&](const std::string &k, int d) {
    auto it = args.find(k);
    return it == args.end() ? d : std::stoi(it->second);
  };
  int deals = get("deals", 1000);
  Game g;
  g.deck0 = "frog_monarch";
  g.deck1 = "blackwing";
  g.seed = 85450002;
  g.env = 0;
  struct Out {
    int set_code, set_level, ok, refused, above_four, violations, viewer;
  };
  Out out = in_child<Out>([&] {
    Out o{};
    g_use_arena = true;
    arena_clear();
    auto *t = new TrueDuel;
    t->e = new_env(g, env_seed_of(g));
    start_true_duel(*t);
    {
      Scope s(true);
      t->e->reset();
    }
    capture_baseline(t->e, t->baseline);
    FmEnv *e = t->e;
    bool set_done = false;
    for (int j = 0; j < 80; ++j) {
      note_public(e, t->memory);
      if (set_done && e->turn() == 2 && e->to_play() == 1 && e->msg() == MSG_SELECT_IDLECMD) break;
      int pick = -1;
      const auto &acts = e->actions();
      for (size_t i = 0; i < acts.size(); ++i)
        if (acts[i].act_ == ygopro::ActionAct::Cancel) pick = static_cast<int>(i);
      if (e->msg() == MSG_SELECT_IDLECMD) {
        for (size_t i = 0; i < acts.size(); ++i)
          if (acts[i].phase_ != ygopro::ActionPhase::None) pick = static_cast<int>(i);
        if (!set_done)
          for (size_t i = 0; i < acts.size(); ++i)
            if (acts[i].act_ == ygopro::ActionAct::MSet) {
              pick = static_cast<int>(i);
              set_done = true;
              card *c = DealAccess::object_of(e, acts[i]);
              o.set_code = c->data.code;
              o.set_level = c->data.level;
              break;
            }
      }
      if (e->msg() == MSG_SELECT_BATTLECMD)
        for (size_t i = 0; i < acts.size(); ++i)
          if (acts[i].phase_ != ygopro::ActionPhase::None) pick = static_cast<int>(i);
      if (pick < 0) pick = 0;
      RowMarker marker = before_true_step(e, t->memory, pick);
      {
        Scope s(true);
        e->act(pick);
      }
      after_true_step(e, t->memory, marker);
    }
    stop_true_duel();
    int viewer = e->to_play();
    o.viewer = viewer;
    AssumedList assumed = true_list_of_opponent(g, viewer);
    Snapshot snap;
    snapshot_take(snap);
    for (int i = 0; i < deals; ++i) {
      snapshot_restore(snap);
      DealReport r = determinize(e, viewer, mix(85100103, i), t->memory, t->baseline, assumed);
      if (!r.ok()) {
        ++o.refused;
        continue;
      }
      ++o.ok;
      o.violations += hard_rule_violations(e, viewer, t->memory, t->baseline);
      for (card *c : e->core()->game_field->player[1 - viewer].list_mzone)
        if (c && (c->current.position & POS_FACEDOWN) && (c->data.level & 0xff) > 4) ++o.above_four;
    }
    return o;
  });
  char buf[300];
  snprintf(buf, sizeof buf, "{\"set_fixture\":true,\"seed\":%u,\"set_code\":%d,\"set_level\":%d,\"deals\":%d,\"ok\":%d,\"refused\":%d,\"above_level_four\":%d,\"hard_rule_violations\":%d}",
           g.seed, out.set_code, out.set_level, deals, out.ok, out.refused, out.above_four, out.violations);
  emit_kv(buf);
  return 0;
}

// Hand traps and other private prompts. Each opposing card with an effect from the hand, in each list that holds it: one
// duel with that card in the opening hand and one with it deep in the Deck, the same scripted public line, and the opponent
// declining everything and passing. Wherever the viewer's views have matched so far, the two worlds must deal alike. This
// is the test Sol's Gorz pair came from, widened to every such card on these lists.
constexpr int kTrapDecisions = 24;
struct TrapWorld {
  int decisions, private_prompts;
  uint64_t view[kTrapDecisions], slots[kTrapDecisions], canon[kTrapDecisions], raw[kTrapDecisions];
  uint8_t refused[kTrapDecisions];
  char kind[kTrapDecisions][24];
  int private_at[kTrapDecisions];
};

// The same line in both worlds: Summon or Set what the viewer can, go to battle, attack whatever it can, end the turn.
inline int scripted_pick(FmEnv *e, int viewer, bool &summoned) {
  const auto &acts = e->actions();
  int pick = -1;
  for (size_t i = 0; i < acts.size(); ++i)
    if (acts[i].act_ == ygopro::ActionAct::Cancel) pick = static_cast<int>(i);
  if (e->msg() == MSG_SELECT_IDLECMD) {
    for (size_t i = 0; i < acts.size(); ++i)
      if (acts[i].phase_ == ygopro::ActionPhase::End || acts[i].phase_ == ygopro::ActionPhase::Battle) pick = static_cast<int>(i);
    if (e->to_play() == viewer) {
      for (size_t i = 0; i < acts.size(); ++i)
        if (acts[i].phase_ == ygopro::ActionPhase::Battle) pick = static_cast<int>(i);
      if (!summoned)
        for (size_t i = 0; i < acts.size(); ++i)
          if (acts[i].act_ == ygopro::ActionAct::Summon) {
            pick = static_cast<int>(i);
            summoned = true;
            break;
          }
    }
  }
  if (e->msg() == MSG_SELECT_BATTLECMD) {
    for (size_t i = 0; i < acts.size(); ++i)
      if (acts[i].phase_ != ygopro::ActionPhase::None) pick = static_cast<int>(i);
    if (e->to_play() == viewer)
      for (size_t i = 0; i < acts.size(); ++i)
        if (acts[i].act_ == ygopro::ActionAct::Attack || acts[i].act_ == ygopro::ActionAct::DirectAttack) {
          pick = static_cast<int>(i);
          break;
        }
  }
  return pick < 0 ? 0 : pick;
}

inline TrapWorld run_trap_world(const Game &g, const std::vector<uint32_t> rig[2], uint32_t duel_seed, const AssumedList &assumed, int cap) {
  TrapWorld out{};
  g_use_arena = true;
  arena_clear();
  auto *t = new TrueDuel;
  t->e = new_env(g, env_seed_of(g));
  start_true_duel(*t);
  {
    Scope s(true);
    t->e->reset_rigged(duel_seed, rig[0], rig[1], g.deck0, g.deck1);
  }
  capture_baseline(t->e, t->baseline);
  FmEnv *e = t->e;
  int viewer = 0;
  bool summoned = false;
  auto *run = new CanonRun;
  Snapshot snap;
  for (int step = 0; step < 300 && !e->done() && out.decisions < kTrapDecisions; ++step) {
    note_public(e, t->memory);
    if (e->to_play() == viewer) {
      int k = out.decisions++;
      {
        Scope s(true);
        out.view[k] = e->obs_hash();
      }
      out.private_at[k] = out.private_prompts;
      stop_true_duel();
      snapshot_take(snap);
      int refused = 0;
      for (int seed = 0; seed < 20; ++seed) {
        snapshot_restore(snap);
        DealReport r = determinize(e, viewer, mix(85100107, seed), t->memory, t->baseline, assumed);
        if (!r.ok()) {
          ++refused;
          snprintf(out.kind[k], sizeof out.kind[k], "%s", r.status == DealStatus::kHiddenObjectState ? g_state_kind : g_reference_kind);
          continue;
        }
        if (seed == 0) {
          uint64_t h = 0;
          for (const auto &slot : board_codes(e)) h = mix(mix(h, slot.first), slot.second);
          out.slots[k] = h;
          run_canonical(e, mix(85100109, k), cap, *run, false, false);
          out.canon[k] = run->digest;
          snapshot_restore(snap);
          determinize(e, viewer, mix(85100107, seed), t->memory, t->baseline, assumed);
          run_canonical(e, mix(85100109, k), cap, *run, true, true);
          uint64_t d = run->digest;
          for (int i = 0; i < std::min(run->length, kChainCap); ++i) d = mix(d, run->obs[i]);
          out.raw[k] = d;
        }
      }
      out.refused[k] = static_cast<uint8_t>(refused);
      snapshot_restore(snap);
      start_true_duel(*t);
    } else if (e->msg() == MSG_SELECT_EFFECTYN || e->msg() == MSG_SELECT_YESNO || e->msg() == MSG_SELECT_CHAIN) {
      ++out.private_prompts;
    }
    int pick = scripted_pick(e, viewer, summoned);
    RowMarker marker = before_true_step(e, t->memory, pick);
    {
      Scope s(true);
      e->act(pick);
    }
    after_true_step(e, t->memory, marker);
  }
  stop_true_duel();
  return out;
}

inline int cmd_handtrap(const std::vector<Game> &games, std::map<std::string, std::string> &args) {
  auto text = [&](const std::string &k) {
    auto it = args.find(k);
    return it == args.end() ? std::string() : it->second;
  };
  int cap = 800;
  FILE *log = text("log").empty() ? nullptr : fopen(text("log").c_str(), "w");
  // Every list with its next neighbour as the viewer, from the corpus's cells.
  std::vector<std::pair<std::string, std::string>> cells;
  std::set<std::pair<std::string, std::string>> seen;
  for (const auto &g : games)
    if (seen.insert({g.deck0, g.deck1}).second) cells.push_back({g.deck0, g.deck1});
  long fixtures = 0, with_prompt = 0, private_prompts = 0, compared = 0, refusal_differences = 0, slot_differences = 0, canon_differences = 0, raw_differences = 0, refused_decisions = 0,
       skipped = 0, gorz_compared = 0, gorz_prompts = 0, gorz_refusal_differences = 0, gorz_refused = 0;
  std::map<uint32_t, long> prompts_by_code;
  uint32_t seed = 85450100;
  std::vector<std::pair<std::string, std::string>> rounds;
  for (int round = 0; round < 4; ++round) rounds.insert(rounds.end(), cells.begin(), cells.end());
  for (const auto &cell : rounds) {
    struct Setup {
      int n[2], hand[8], hand_count, traps;
      uint32_t code[2][64], trap[32];
      uint32_t duel_seed;
    };
    Game g;
    g.deck0 = cell.first;
    g.deck1 = cell.second;
    g.seed = seed;
    g.env = 0;
    (void)spec_for(g);
    Setup setup = in_child<Setup>([&] {
      Setup u{};
      g_use_arena = true;
      arena_clear();
      uint32_t deck_seed;
      FmEnv::production_seeds(env_seed_of(g), deck_seed, u.duel_seed);
      FmEnv *e = new_env(g, env_seed_of(g));
      {
        Scope s(true);
        e->reset();
      }
      std::vector<card *> objects[2];
      for (card *c : e->core()->cards)
        if (c->data.code && !c->is_extra_deck_monster()) objects[c->owner].push_back(c);
      for (int p = 0; p < 2; ++p) {
        std::sort(objects[p].begin(), objects[p].end(), [](card *a, card *b) { return a->cardid < b->cardid; });
        u.n[p] = static_cast<int>(objects[p].size());
        for (int i = 0; i < u.n[p]; ++i) u.code[p][i] = objects[p][i]->data.code;
      }
      for (int i = 0; i < u.n[1]; ++i)
        if (objects[1][i]->current.location == LOCATION_HAND && u.hand_count < 8) u.hand[u.hand_count++] = i;
      std::set<uint32_t> traps;
      for (card *c : objects[1])
        for (const auto &kv : c->field_effect)
          if ((kv.second->range & LOCATION_HAND) && (kv.second->type & (EFFECT_TYPE_TRIGGER_O | EFFECT_TYPE_TRIGGER_F | EFFECT_TYPE_QUICK_O | EFFECT_TYPE_QUICK_F)))
            traps.insert(c->data.code);
      for (uint32_t code : traps)
        if (u.traps < 32) u.trap[u.traps++] = code;
      return u;
    });
    ++seed;
    std::set<uint32_t> trap_set(setup.trap, setup.trap + setup.traps);
    for (int ti = 0; ti < setup.traps; ++ti) {
      uint32_t code = setup.trap[ti];
      std::vector<uint32_t> rig[2];
      for (int p = 0; p < 2; ++p) rig[p].assign(setup.code[p], setup.code[p] + setup.n[p]);
      // Other hand-effect cards stay out of the opening hand in both, so the pair differs in this one card.
      std::vector<int> plain;
      std::set<int> hand(setup.hand, setup.hand + setup.hand_count);
      for (int i = 0; i < setup.n[1]; ++i)
        if (!hand.count(i) && !trap_set.count(rig[1][i])) plain.push_back(i);
      if (plain.size() < 8) {
        ++skipped;
        continue;
      }
      size_t next_plain = 0;
      for (int h : hand)
        if (trap_set.count(rig[1][h])) std::swap(rig[1][h], rig[1][plain[next_plain++]]);
      auto *first_copy = &rig[1];
      auto it = std::find(first_copy->begin(), first_copy->end(), code);
      if (it == first_copy->end()) {
        ++skipped;
        continue;
      }
      std::swap(*it, rig[1][setup.hand[0]]);
      std::vector<uint32_t> without[2] = {rig[0], rig[1]};
      // Index 0 is the bottom of the Deck, so this world does not simply draw the card a turn later.
      std::swap(without[1][setup.hand[0]], without[1][plain[0]]);
      AssumedList assumed;
      assumed.opponent_main = ygopro::main_decks_.at(g.deck1);
      TrapWorld a{}, b{};
      try {
        a = in_child<TrapWorld>([&] { return run_trap_world(g, rig, setup.duel_seed, assumed, cap); });
        b = in_child<TrapWorld>([&] { return run_trap_world(g, without, setup.duel_seed, assumed, cap); });
      } catch (const std::exception &ex) {
        ++skipped;
        if (log) fprintf(log, "FAILED %s/%s code=%u: %s\n", g.deck0.c_str(), g.deck1.c_str(), code, ex.what());
        continue;
      }
      ++fixtures;
      int extra = 0;
      bool is_gorz = code == 44330098;
      for (int k = 0; k < std::min(a.decisions, b.decisions); ++k) {
        if (a.view[k] != b.view[k]) break;  // From here the viewer can tell these worlds apart. They are not a pair.
        ++compared;
        extra = a.private_at[k] - b.private_at[k];
        if (log && getenv("FM_HANDTRAP_VERBOSE"))
          fprintf(log, "AT %s/%s code=%u decision=%d private=%d/%d refused=%d/%d kind=%s/%s\n", g.deck0.c_str(), g.deck1.c_str(), code, k, a.private_at[k], b.private_at[k], a.refused[k], b.refused[k], a.kind[k], b.kind[k]);
        if (a.refused[k] || b.refused[k]) ++refused_decisions;
        if (is_gorz) {
          ++gorz_compared;
          gorz_refused += a.refused[k] > 0 || b.refused[k] > 0;
        }
        if (a.refused[k] != b.refused[k]) {
          ++refusal_differences;
          gorz_refusal_differences += is_gorz;
          if (log) fprintf(log, "REFUSAL %s/%s code=%u decision=%d refused=%d/%d kind=%s/%s private=%d\n", g.deck0.c_str(), g.deck1.c_str(), code, k, a.refused[k], b.refused[k], a.kind[k], b.kind[k], extra);
          continue;
        }
        if (a.refused[k]) continue;
        bool slots = a.slots[k] == b.slots[k], canon = a.canon[k] == b.canon[k], raw = a.raw[k] == b.raw[k];
        slot_differences += !slots;
        canon_differences += !canon;
        raw_differences += !raw;
        if ((!slots || !canon || !raw) && log)
          fprintf(log, "DIFF %s/%s code=%u decision=%d slots=%d canon=%d raw=%d private=%d\n", g.deck0.c_str(), g.deck1.c_str(), code, k, slots, canon, raw, extra);
      }
      int prompts = a.private_prompts - b.private_prompts;
      with_prompt += prompts > 0;
      private_prompts += std::max(0, prompts);
      prompts_by_code[code] += std::max(0, prompts);
      if (is_gorz) gorz_prompts += std::max(0, prompts);
    }
  }
  std::string by_code = "{";
  for (const auto &kv : prompts_by_code) by_code += std::string(by_code.size() > 1 ? "," : "") + "\"" + std::to_string(kv.first) + "\":" + std::to_string(kv.second);
  by_code += "}";
  char buf[1800];
  snprintf(buf, sizeof buf,
           "{\"handtrap\":true,\"seeds\":\"85450100-%u\",\"fixtures\":%ld,\"skipped\":%ld,\"fixtures_with_extra_private_prompts\":%ld,\"extra_private_prompts\":%ld,\"viewer_decisions_compared\":%ld,"
           "\"decisions_with_a_refusal\":%ld,\"refusal_differences\":%ld,\"slot_differences\":%ld,\"canonical_differences\":%ld,\"exact_differences\":%ld,"
           "\"gorz\":{\"decisions_compared\":%ld,\"extra_private_prompts\":%ld,\"refused_decisions\":%ld,\"refusal_differences\":%ld},\"extra_prompts_by_code\":%s}",
           seed - 1, fixtures, skipped, with_prompt, private_prompts, compared, refused_decisions, refusal_differences, slot_differences, canon_differences, raw_differences, gorz_compared, gorz_prompts,
           gorz_refused, gorz_refusal_differences, by_code.c_str());
  emit_kv(buf);
  if (log) fclose(log);
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
  uint64_t x = 85100067;
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
  long truth_has_summon_used = 0, truth_nonbaseline = 0;
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
        for (const auto &a : truth->actions()) truth_actions.push_back(descriptor(truth, a));
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
        for (const auto &a : built->actions()) constructed.push_back(descriptor(built, a));
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
