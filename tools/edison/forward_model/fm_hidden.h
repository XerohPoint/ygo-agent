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
            fprintf(log, " [%u id=%u fieldid=%u", c->data.code, c->cardid, c->fieldid_r);
            std::vector<uint32_t> ids;
            for (const auto &kv : c->indexer) ids.push_back(kv.first->id);
            std::sort(ids.begin(), ids.end());
            for (uint32_t v : ids) fprintf(log, " %u", v);
            fprintf(log, "]");
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
      all.insert(all.end(), hs.own_deck.begin(), hs.own_deck.end());
      if (DealAccess::unbound_deck_options(e) || references_hidden(e, HiddenSet(all))) eligible = false;
      for (card *c : hs.opp_slots)
        if (carries_object_state(c, t->baseline)) eligible = false;
    }
    for (int p = 0; p < 2 && eligible; ++p)
      for (card *c : movable[p])
        if (std::find(t->main_objects[p].begin(), t->main_objects[p].end(), c) == t->main_objects[p].end()) eligible = false;
    if (!eligible) continue;
    std::vector<card *> assign[2];
    bool possible = true;
    for (int p = 0; p < 2 && possible; ++p) {
      bool placed = false;
      for (int attempt = 0; attempt < 500 && !placed; ++attempt) {
        assign[p] = movable[p];
        for (int k = static_cast<int>(assign[p].size()) - 1; k > 0; --k) std::swap(assign[p][k], assign[p][splitmix(y) % (k + 1)]);
        placed = true;
        for (size_t k = 0; k < movable[p].size(); ++k)
          if (!code_fits(assign[p][k]->data.code, class_of_slot(movable[p][k]))) placed = false;
      }
      possible = placed;
    }
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
      apply_assignment(e->core(), holders, assigned, inherit);
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
#define J(name) "\"" #name "\":%ld,"
  char buf[6000];
  snprintf(buf, sizeof buf,
           "{\"redeal\":true,\"lists_covered\":%zu,\"positions\":%ld,\"crashed_positions\":%ld,\"deals\":%d,\"cap\":%d,"
           "\"r1\":{" J(playouts) J(done) J(invariant_bad) J(diag) J(exceptions) J(changed) J(repeat_bad) "\"_\":0},"
           "\"r2\":{" J(attempts) J(ok) J(playouts) J(done) J(invariant_bad) J(diag) J(exceptions) J(repeat_bad)
           J(changed) J(moved) J(known_pinned) J(opponent_extra_not_redealt) J(facedown_banished_not_redealt) J(rows_scrubbed) J(sets_rebuilt)
           J(deck_options_rebound) J(root_obs_changed) J(positions_refused_every_deal) "\"status\":%s,\"us_per_deal\":%.1f},"
           "\"r3\":{" J(samples) J(accept) J(reject_prompt) J(reject_offered) J(reject_public) J(reject_obs) J(reject_action) J(reject_end) J(exceptions)
           J(index_samples) J(index_survive) J(layout_same) J(rng_same) J(board_match) J(canon_match) J(canon_match_cardid)
           J(raw_match_cardid) J(winner_match) J(viewer_obs_match) J(compare_exceptions) J(div_lt10) J(div_lt100)
           "\"accept_early\":\"%ld/%ld\",\"accept_mid\":\"%ld/%ld\",\"accept_late\":\"%ld/%ld\",\"ms_per_accepted\":%.2f},"
           "\"r4\":{" J(pairs) J(refused_both) J(refused_one_world_only) J(failed) J(slots_equal) J(canon_equal) J(raw_equal) J(winner_equal) J(root_obs_equal)
           J(next8_viewer_obs_equal) J(viewer_obs_equal) J(all_obs_equal) J(control_pairs) J(control_chain_equal) J(control_viewer_obs_equal)
           J(layout_same_pairs) J(layout_same_slots) J(layout_same_canon) J(layout_same_raw) J(layout_same_obs) "\"_\":0},"
           "\"r5\":{" J(attempts) J(playouts) J(done) J(invariant_bad) J(diag) J(exceptions) J(cards) J(card_moved)
           "\"us_per_materialise\":%.1f}}",
           decks_covered.size(), t.positions, crashed, deals, cap, t.r1_playouts, t.r1_done, t.r1_invariant_bad, t.r1_diag, t.r1_exceptions,
           t.r1_changed, t.r1_repeat_bad, t.r2_attempts, t.r2_ok, t.r2_playouts, t.r2_done, t.r2_invariant_bad, t.r2_diag, t.r2_exceptions,
           t.r2_repeat_bad, t.r2_changed, t.r2_moved, t.r2_known_pinned, t.r2_extra_kept, t.r2_banished_kept, t.r2_rows_scrubbed,
           t.r2_sets_rebuilt, t.r2_rebound, t.r2_root_obs_changed, t.r2_positions_refused_all, refused.c_str(),
           r2_us / std::max(1L, t.r2_attempts), t.r3_samples, t.r3_accept, t.r3_reject_prompt, t.r3_reject_offered, t.r3_reject_public, t.r3_reject_obs,
           t.r3_reject_action, t.r3_reject_end, t.r3_exceptions, t.r3_index_samples, t.r3_index_survive, t.r3_layout_same, t.r3_rng_same, t.r3_board_match,
           t.r3_canon_match, t.r3_canon_match_cardid, t.r3_raw_match_cardid, t.r3_winner_match, t.r3_viewer_obs_match, t.r3_compare_exceptions,
           t.r3_div_lt10, t.r3_div_lt100, t.bucket_accept[0], t.bucket_samples[0], t.bucket_accept[1], t.bucket_samples[1], t.bucket_accept[2],
           t.bucket_samples[2], r3_us / 1000 / std::max(1L, t.r3_accept), t.r4_pairs, t.r4_refused_both, t.r4_refused_one, t.r4_failed,
           t.r4_slots_equal, t.r4_canon_equal, t.r4_raw_equal, t.r4_winner_equal, t.r4_root_obs_equal, t.r4_next8_viewer_obs_equal,
           t.r4_viewer_obs_equal, t.r4_all_obs_equal, t.r4_control_pairs, t.r4_control_chain_equal, t.r4_control_viewer_obs_equal, t.r4_ls_pairs,
           t.r4_ls_slots, t.r4_ls_canon, t.r4_ls_raw, t.r4_ls_obs, t.r5_attempts, t.r5_playouts, t.r5_done, t.r5_invariant_bad, t.r5_diag,
           t.r5_exceptions, t.r5_cards, t.r5_card_moved, r5_us / std::max(1L, t.r5_attempts));
  emit_kv(buf);
  return 0;
}

// Is the deal uniform over the slots' valid assignments? Hand slots should see each code in proportion to
// its copies in the pool, whatever the seed. Chi-square per position, over deals at real roots.
inline int cmd_uniform(const std::vector<Game> &games, std::map<std::string, std::string> &args) {
  auto get = [&](const std::string &k, int d) {
    auto it = args.find(k);
    return it == args.end() ? d : std::stoi(it->second);
  };
  int per_cell = get("per-cell", 1), deals = get("deals", 4000);
  auto positions = sample_positions_per_cell(games, per_cell, 85100097);
  double total_chi = 0;
  long total_dof = 0, tested = 0, over_three_sigma = 0;
  for (const Pos &pos : positions) {
    struct Out {
      double chi;
      int dof, ok;
    };
    Out result = in_child<Out>([&] {
      Out out{0, 0, 0};
      const Game &g = games[pos.game];
      int viewer = g.to_play[pos.index];
      AssumedList assumed = true_list_of_opponent(g, viewer);
      g_use_arena = true;
      arena_clear();
      auto *t = new TrueDuel;
      build_true_duel(g, pos.index, viewer, *t, false);
      Snapshot snap;
      snapshot_take(snap);
      FmEnv *e = t->e;
      HiddenSlots hs = collect_hidden(e, viewer, t->memory, t->baseline);
      int opp = 1 - viewer;
      // A free hand slot: the first opposing hand card the viewer does not know.
      int slot = -1;
      for (card *c : hs.opp_slots)
        if (c->current.location == LOCATION_HAND) {
          slot = c->current.sequence;
          break;
        }
      // The pool, read by the test: codes dealt to a slot with no public type constraint come from the
      // free part of the pool, which is everything left after the constrained slots draw. With no set
      // cards the pool is exact, so only those positions are tested.
      int set_slots = 0;
      std::map<uint32_t, long> pool, seen;
      for (card *c : hs.opp_slots) {
        ++pool[c->data.code];
        set_slots += class_of_slot(c) != kFree;
      }
      if (slot < 0 || set_slots) return out;
      long ok = 0;
      for (int i = 0; i < deals; ++i) {
        snapshot_restore(snap);
        DealReport r = determinize(e, viewer, mix(85100099, i), t->memory, t->baseline, assumed);
        if (!r.ok()) continue;
        ++ok;
        ++seen[e->core()->game_field->player[opp].list_hand[slot]->data.code];
      }
      if (!ok) return out;
      double n = hs.opp_slots.size();
      for (const auto &kv : pool) {
        double expected = ok * kv.second / n;
        double got = seen.count(kv.first) ? seen[kv.first] : 0;
        out.chi += (got - expected) * (got - expected) / expected;
      }
      out.dof = static_cast<int>(pool.size()) - 1;
      out.ok = 1;
      return out;
    });
    if (!result.ok) continue;
    ++tested;
    total_chi += result.chi;
    total_dof += result.dof;
    if (result.chi > result.dof + 3 * std::sqrt(2.0 * result.dof)) ++over_three_sigma;
  }
  char buf[300];
  snprintf(buf, sizeof buf, "{\"uniform\":true,\"positions_tested\":%ld,\"deals_per_position\":%d,\"chi_square\":%.1f,\"dof\":%ld,\"ratio\":%.3f,\"over_three_sigma\":%ld}", tested, deals,
           total_chi, total_dof, total_chi / std::max(1L, total_dof), over_three_sigma);
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
