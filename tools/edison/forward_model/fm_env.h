// Shared test bed: the unmodified production environment class, driven natively.
#pragma once
#include <fcntl.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <cinttypes>
#include <filesystem>
#include <map>
#include <random>
#include <vector>

#include "arena.h"

// Test bed only: the production class is used unmodified, but its private helpers
// are reachable from the subclass below. Library headers are included first so the
// macro touches only the project's own headers.
#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include <SQLiteCpp/VariadicBind.h>
#include <ankerl/unordered_dense.h>
#include <glog/logging.h>
#include "ygopro-core/ocgapi.h"

#ifndef FM_CORE_PIN
#error "Build through tools/edison/forward_model/build.sh: it refuses any core but the certified one."
#endif

// Test bed only: every batch of core messages passes through this tap on its way to the unmodified
// production parser. It reads and never alters. The public memory uses it to catch reveals that
// happen between decisions, which a scan at decisions would miss.
namespace fm {
using MessageTap = void (*)(void *context, const byte *data, int length);
inline MessageTap g_message_tap = nullptr;
inline void *g_message_tap_context = nullptr;
}  // namespace fm
inline int32 fm_tapped_get_message(intptr_t pduel, byte *buf) {
  int32 length = get_message(pduel, buf);
  if (fm::g_message_tap) fm::g_message_tap(fm::g_message_tap_context, buf, length);
  return length;
}
#define get_message fm_tapped_get_message
#define private protected
#include "ygoenv/ygopro/ygopro.h"
#undef private
#undef get_message
#include "ygopro-core/card.h"
#include "ygopro-core/effect.h"
#include "ygopro-core/field.h"
#include "ygopro-core/group.h"
#include "ygopro-core/interpreter.h"

namespace fm {
using ygopro::LegalAction;
using ygopro::YGOProEnvImpl;
using ygopro::YGOProEnvSpec;

inline double now_us() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

inline uint64_t mix(uint64_t h, uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  h *= 0xff51afd7ed558ccdULL;
  return h ^ (h >> 33);
}

inline uint64_t splitmix(uint64_t &x) {
  uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

inline long g_diagnostics = 0;
inline char g_last_diagnostic[300] = {0};
inline uint32 on_core_message(intptr_t handle, uint32) {
  ++g_diagnostics;
  get_log_message(handle, g_last_diagnostic);
  return 0;
}

constexpr int kFullQuery =
    QUERY_CODE | QUERY_POSITION | QUERY_ALIAS | QUERY_TYPE | QUERY_LEVEL | QUERY_RANK |
    QUERY_ATTRIBUTE | QUERY_RACE | QUERY_ATTACK | QUERY_DEFENSE | QUERY_BASE_ATTACK |
    QUERY_BASE_DEFENSE | QUERY_REASON | QUERY_REASON_CARD | QUERY_EQUIP_CARD |
    QUERY_TARGET_CARD | QUERY_OVERLAY_CARD | QUERY_COUNTERS | QUERY_OWNER | QUERY_STATUS |
    QUERY_LSCALE | QUERY_RSCALE;

inline byte g_query_buf[1 << 18];

inline bool g_encode = false;

// sha256, so native observation bytes can be compared with the digests production recorded.
class Sha256 {
 public:
  Sha256() {
    static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(h_, init, sizeof h_);
  }
  void update(const void *data, size_t n) {
    auto *p = static_cast<const uint8_t *>(data);
    len_ += n;
    while (n) {
      size_t take = std::min(n, size_t(64) - fill_);
      std::memcpy(buf_ + fill_, p, take);
      fill_ += take;
      p += take;
      n -= take;
      if (fill_ == 64) {
        block(buf_);
        fill_ = 0;
      }
    }
  }
  void digest(uint8_t out[32]) {
    uint64_t bits = len_ * 8;
    uint8_t pad = 0x80, zero = 0;
    update(&pad, 1);
    while (fill_ != 56) update(&zero, 1);
    uint8_t length[8];
    for (int i = 0; i < 8; ++i) length[i] = uint8_t(bits >> (56 - 8 * i));
    update(length, 8);
    for (int i = 0; i < 8; ++i)
      for (int j = 0; j < 4; ++j) out[4 * i + j] = uint8_t(h_[i] >> (24 - 8 * j));
  }
  // The first 16 hex digits, as collect_corpus.py records them.
  static std::string short_hex(const void *data, size_t n) {
    Sha256 s;
    s.update(data, n);
    uint8_t d[32];
    s.digest(d);
    char text[17];
    for (int i = 0; i < 8; ++i) snprintf(text + 2 * i, 3, "%02x", d[i]);
    return std::string(text, 16);
  }

 private:
  static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
  void block(const uint8_t *p) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; ++i) {
      uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
      uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
  }
  uint32_t h_[8];
  uint8_t buf_[64];
  uint64_t len_ = 0;
  size_t fill_ = 0;
};

struct ConstructedCard {
  uint32_t code;
  uint8_t owner, controller, location, sequence, position;
};

class FmEnv : public YGOProEnvImpl {
 public:
  inline static State *g_obs = nullptr;
  inline static std::vector<Array> *g_obs_arrays = nullptr;

  FmEnv(const YGOProEnvSpec &spec, uint64_t env_seed) : YGOProEnvImpl(spec, env_seed) {}

  const std::vector<LegalAction> &actions() const { return legal_actions_; }
  int to_play() const { return to_play_; }
  int msg() const { return msg_; }
  duel *core() const { return reinterpret_cast<duel *>(pduel_); }
  bool started() const { return duel_started_; }
  int winner() const { return winner_; }
  int win_reason() const { return win_reason_; }
  bool truncated() const { return ret_truncated_; }
  // What production reports as info:winner and info:win_reason.
  int reported_winner() const { return done_ && !ret_truncated_ && winner_ <= 2 ? int(winner_) : -1; }
  int reported_win_reason() const { return ret_win_reason_; }
  int turn() const { return turn_count_; }
  int phase() const { return current_phase_; }
  int ms_index() const { return ms_idx_; }

  // Production stamps msg_/cid_/spec_index_ onto legal_actions_ inside WriteState,
  // and step() reads them for the history features.
  void encode() {
    if (!g_obs) {
      Scope off(false);
      g_obs_arrays = new std::vector<Array>();
      auto specs = spec_.state_spec.template AllValues<ShapeSpec>();
      for (auto spec : specs) {
        for (auto &d : spec.shape)
          if (d == -1) d = 1;
        g_obs_arrays->emplace_back(spec);
      }
      g_obs = new State(*g_obs_arrays);
    }
    for (auto &a : *g_obs_arrays) std::memset(a.Data(), 0, a.size * a.element_size);
    WriteState(*g_obs);
  }

  // The part of WriteState that step() depends on, without the observation. Since the Deck label
  // fix, a history row takes its card id from the id WriteState stamped on the action, so stepping
  // with only msg_ stamped would write different history. spec_to_card_id is the rule production
  // itself uses for lone options. `verify --check-stamp 1` compares it with WriteState's result.
  void stamp() {
    for (auto &a : legal_actions_) {
      a.msg_ = msg_;
      if (!a.spec_.empty()) a.cid_ = spec_to_card_id(a.spec_, to_play_);
    }
  }

  void act(int idx) {
    if (g_encode) {
      encode();
    } else {
      stamp();
    }
    step(idx);
  }

  // The bytes an observation consumer gets for this decision, in a fixed order: cards_, global_,
  // decision_, selection_, offered actions_, h_actions_. Call encode() first.
  struct ObsFields {
    const void *data[6];
    size_t size[6];
  };
  ObsFields obs_fields() const {
    auto &st = *g_obs;
    auto &cards = st["obs:cards_"_];
    auto &global = st["obs:global_"_];
    auto &decision = st["obs:decision_"_];
    auto &selection = st["obs:selection_"_];
    auto &acts = st["obs:actions_"_];
    auto &hist = st["obs:h_actions_"_];
    ObsFields f;
    f.data[0] = cards.Data();
    f.size[0] = cards.size * cards.element_size;
    f.data[1] = global.Data();
    f.size[1] = global.size * global.element_size;
    f.data[2] = decision.Data();
    f.size[2] = decision.size * decision.element_size;
    f.data[3] = selection.Data();
    f.size[3] = selection.size * selection.element_size;
    f.data[4] = acts.Data();
    f.size[4] = legal_actions_.size() * acts.Shape(1) * acts.element_size;
    f.data[5] = hist.Data();
    f.size[5] = hist.size * hist.element_size;
    return f;
  }

  // Encodes this decision and folds every observation byte, history included, into a hash.
  uint64_t obs_hash() {
    if (done_) return mix(0x0b5, reported_winner());
    encode();
    ObsFields f = obs_fields();
    uint64_t h = 0x0b5;
    for (int i = 0; i < 6; ++i) {
      auto *p = static_cast<const uint8_t *>(f.data[i]);
      size_t n = f.size[i], j = 0;
      for (; j + 8 <= n; j += 8) {
        uint64_t w;
        std::memcpy(&w, p + j, 8);
        h = mix(h, w);
      }
      for (; j < n; ++j) h = mix(h, p[j]);
    }
    return h;
  }

  void destroy() {
    if (duel_started_) {
      YGO_EndDuel(pduel_);
      duel_started_ = false;
    }
  }

  // Everything the production action interface exposes for this decision.
  uint64_t decision_hash() const {
    uint64_t h = 0x1234;
    h = mix(h, to_play_);
    h = mix(h, msg_);
    h = mix(h, turn_count_);
    h = mix(h, current_phase_);
    h = mix(h, lp_[0]);
    h = mix(h, lp_[1]);
    h = mix(h, ms_idx_);
    h = mix(h, legal_actions_.size());
    for (const auto &a : legal_actions_) {
      for (char c : a.spec_) h = mix(h, static_cast<unsigned char>(c));
      h = mix(h, static_cast<int>(a.act_));
      h = mix(h, static_cast<int>(a.phase_));
      h = mix(h, a.finish_);
      h = mix(h, a.position_);
      h = mix(h, a.effect_);
      h = mix(h, a.number_);
      h = mix(h, static_cast<int>(a.place_));
      h = mix(h, a.attribute_);
    }
    return h;
  }

  // Every environment member that outlives one decision and lives outside the core: history rings
  // and their cursors, reveal memory, the Deck binder's ranks, decision context, multi-select and
  // prompt state, the environment's own random streams, the message buffer position and the offered
  // actions. A missed rollback of any of these shows up here even if no observation reads it yet.
  uint64_t env_hash() const {
    uint64_t h = 0xe7;
    auto text = [&](const std::string &s) {
      for (char c : s) h = mix(h, static_cast<unsigned char>(c));
      h = mix(h, s.size());
    };
    auto texts = [&](const std::vector<std::string> &v) {
      h = mix(h, v.size());
      for (const auto &s : v) text(s);
    };
    for (const auto *ring : {&history_actions_1_, &history_actions_2_}) {
      auto *p = static_cast<const uint8_t *>(ring->Data());
      for (size_t i = 0; i < ring->size * ring->element_size; ++i) h = mix(h, p[i]);
    }
    h = mix(h, ha_p_1_);
    h = mix(h, ha_p_2_);
    std::vector<std::string> revealed(revealed_.begin(), revealed_.end());
    std::sort(revealed.begin(), revealed.end());
    texts(revealed);
    h = mix(h, decision_.pending_hints[0]);
    h = mix(h, decision_.pending_hints[1]);
    h = mix(h, decision_.hint);
    h = mix(h, decision_.minimum);
    h = mix(h, decision_.maximum);
    h = mix(h, decision_.sum_target);
    h = mix(h, decision_.active);
    texts(decision_.selected);
    texts(decision_.mandatory);
    texts(decision_.candidates);
    h = mix(h, deck_bind_rank_.size());
    for (const auto &kv : deck_bind_rank_) h = mix(mix(h, kv.first), kv.second);
    h = mix(h, ms_idx_);
    h = mix(h, ms_mode_);
    h = mix(h, ms_min_);
    h = mix(h, ms_max_);
    h = mix(h, ms_must_);
    h = mix(h, n_places_);
    h = mix(h, n_counters_);
    h = mix(h, discard_hand_);
    h = mix(h, bool(local_prompt_));
    texts(ms_specs_);
    h = mix(h, ms_combs_.size());
    for (const auto &comb : ms_combs_)
      for (int v : comb) h = mix(h, v);
    for (int v : ms_r_idxs_) h = mix(h, v);
    std::vector<std::pair<std::string, int>> spec2idx(ms_spec2idx_.begin(), ms_spec2idx_.end());
    std::sort(spec2idx.begin(), spec2idx.end());
    for (const auto &kv : spec2idx) {
      text(kv.first);
      h = mix(h, kv.second);
    }
    std::mt19937 gen = gen_, duel_gen = duel_gen_;
    h = mix(mix(h, gen()), duel_gen());
    h = mix(h, dp_);
    h = mix(h, dl_);
    for (int i = 0; i < std::min(dl_, int(sizeof data_)); ++i) h = mix(h, data_[i]);
    h = mix(h, tp_);
    h = mix(h, eng_flag_);
    h = mix(h, step_count_);
    h = mix(h, done_);
    h = mix(h, to_play_);
    h = mix(h, msg_);
    h = mix(h, lp_[0]);
    h = mix(h, lp_[1]);
    for (const auto &a : legal_actions_) {
      text(a.spec_);
      h = mix(h, a.cid_);
    }
    return h;
  }

  // Privileged: full board, hidden cards, deck order, future randomness, processor counters and
  // the environment's own state. Used only to compare simulator runs, never by a policy.
  uint64_t deep_hash() {
    uint64_t h = decision_hash();
    h = mix(h, env_hash());
    if (!duel_started_) return h;
    duel *d = core();
    static const int locations[] = {LOCATION_DECK,  LOCATION_HAND,    LOCATION_MZONE,
                                    LOCATION_SZONE, LOCATION_GRAVE,   LOCATION_REMOVED,
                                    LOCATION_EXTRA};
    for (int p = 0; p < 2; ++p) {
      for (int loc : locations) {
        int n = query_field_card(pduel_, p, loc, kFullQuery, g_query_buf, 0);
        for (int i = 0; i < n; ++i) h = mix(h, g_query_buf[i]);
      }
    }
    field *f = d->game_field;
    h = mix(h, f->infos.field_id);
    h = mix(h, f->infos.turn_id);
    h = mix(h, f->infos.card_id);
    h = mix(h, f->infos.phase);
    h = mix(h, f->core.units.size());
    h = mix(h, f->core.current_chain.size());
    h = mix(h, f->core.summon_count[0]);
    h = mix(h, f->core.summon_count[1]);
    uint64_t counts = 0;
    for (const auto &kv : f->core.effect_count_code) counts += mix(kv.first, kv.second);
    for (const auto &kv : f->core.effect_count_code_duel) counts += mix(kv.first, kv.second);
    for (const auto &kv : f->core.effect_count_code_chain) counts += mix(kv.first, kv.second);
    h = mix(h, counts);
    uint64_t rng_probe = mix(0, 0);
    {
      ::mt19937 copy = d->random;
      for (int i = 0; i < 4; ++i) rng_probe = mix(rng_probe, copy.rand());
    }
    return mix(h, rng_probe);
  }

  // A reset() whose deck order comes from the caller instead of the production
  // shuffle. All other start-of-duel state follows reset(); used only to build
  // "natural deal" reference duels for hidden-state experiments.
  void reset_rigged(uint32_t duel_seed, const std::vector<uint32_t> &main0,
                    const std::vector<uint32_t> &main1, const std::string &name0,
                    const std::string &name1) {
    play_mode_ = ygopro::kSelfPlay;
    turn_count_ = 0;
    ms_idx_ = -1;
    local_prompt_ = nullptr;
    decision_.reset();
    history_actions_1_.Zero();
    history_actions_2_.Zero();
    ha_p_1_ = 0;
    ha_p_2_ = 0;
    auto pduel = YGO_CreateDuel(duel_seed);
    const std::vector<uint32_t> *mains[2] = {&main0, &main1};
    const std::string *names[2] = {&name0, &name1};
    for (ygopro::PlayerId p = 0; p < 2; p++) {
      YGO_SetPlayerInfo(pduel, p, ygopro::duel_config::starting_lp,
                        ygopro::duel_config::opening_hand, ygopro::duel_config::draw_count);
      for (auto code : *mains[p])
        YGO_NewCard(pduel, code, p, p, LOCATION_DECK, 0, POS_FACEDOWN_DEFENSE);
      const auto &extra = ygopro::extra_decks_.at(*names[p]);
      for (int i = int(extra.size()) - 1; i >= 0; --i)
        YGO_NewCard(pduel, extra[i], p, p, LOCATION_EXTRA, 0, POS_FACEDOWN_DEFENSE);
      (p == 0 ? main_deck0_ : main_deck1_) = *mains[p];
      (p == 0 ? extra_deck0_ : extra_deck1_) = extra;
      deck_name_[p] = *names[p];
    }
    YGO_StartDuel(pduel, ygopro::duel_options_);
    pduel_ = pduel;
    for (ygopro::PlayerId p = 0; p < 2; p++) {
      players_[p] = std::make_unique<ygopro::GreedyAI>(nickname_[p], init_lp_, p, verbose_);
      lp_[p] = players_[p]->init_lp();
    }
    duel_started_ = true;
    eng_flag_ = 0;
    winner_ = 255;
    ret_truncated_ = false;
    revealed_.clear();
    win_reason_ = 255;
    discard_hand_ = false;
    done_ = false;
    step_count_ = 0;
    next();
    ret_reward_ = 0;
    ret_win_reason_ = 0;
  }

  bool revealed_has(int viewer, int controller, int location, int sequence) {
    return revealed_.count(reveal_key(viewer, controller, location, sequence)) > 0;
  }

  // Candidate D: a duel built only through new_card/set_player_info, as the
  // fixtures do. start_count and draw_count are 0 so the description is not altered.
  void reset_constructed(const std::vector<ConstructedCard> &cards, const int lps[2],
                         const std::string &name0, const std::string &name1) {
    play_mode_ = ygopro::kSelfPlay;
    turn_count_ = 0;
    ms_idx_ = -1;
    local_prompt_ = nullptr;
    decision_.reset();
    history_actions_1_.Zero();
    history_actions_2_.Zero();
    ha_p_1_ = 0;
    ha_p_2_ = 0;
    auto pduel = YGO_CreateDuel(12345);
    for (int p = 0; p < 2; p++) YGO_SetPlayerInfo(pduel, p, lps[p], 0, 0);
    for (const auto &c : cards)
      YGO_NewCard(pduel, c.code, c.owner, c.controller, c.location, c.sequence, c.position);
    YGO_StartDuel(pduel, ygopro::duel_options_);
    pduel_ = pduel;
    deck_name_[0] = name0;
    deck_name_[1] = name1;
    for (ygopro::PlayerId p = 0; p < 2; p++) {
      players_[p] = std::make_unique<ygopro::GreedyAI>(nickname_[p], init_lp_, p, verbose_);
      lp_[p] = lps[p];
    }
    duel_started_ = true;
    eng_flag_ = 0;
    winner_ = 255;
    ret_truncated_ = false;
    revealed_.clear();
    win_reason_ = 255;
    discard_hand_ = false;
    done_ = false;
    step_count_ = 0;
    next();
    ret_reward_ = 0;
    ret_win_reason_ = 0;
  }

  // The seeds reset() would use, for the natural-deal references.
  static void production_seeds(uint64_t env_seed, uint32_t &deck_seed, uint32_t &duel_seed) {
    std::mt19937 gen(env_seed);
    std::uniform_int_distribution<uint64_t> dist(0, 0xffffffff);
    deck_seed = dist(gen);
    duel_seed = dist(gen);
  }

  friend struct DealAccess;
  friend struct MutantAccess;
};

struct Game {
  std::string deck0, deck1;
  uint32_t seed;
  int env;
  int winner, reason;
  std::vector<int> actions, to_play, options;
};

inline std::vector<Game> load_games(const std::string &path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open " + path);
  std::vector<Game> games;
  std::string tag;
  while (in >> tag) {
    Game g;
    int n;
    in >> g.deck0 >> g.deck1 >> g.seed >> g.env >> g.winner >> g.reason >> n;
    for (auto *v : {&g.actions, &g.to_play, &g.options}) {
      v->resize(n);
      for (int &x : *v) in >> x;
    }
    games.push_back(std::move(g));
  }
  return games;
}

inline std::map<std::string, YGOProEnvSpec *> g_specs;

// The same configuration as tools/edison/learner/actor.py's vector environment.
inline const YGOProEnvSpec &spec_for_decks(const std::string &deck0, const std::string &deck1, int seed) {
  std::string key = deck0 + "/" + deck1;
  auto it = g_specs.find(key);
  if (it != g_specs.end()) return *it->second;
  Scope off(false);
  auto config = YGOProEnvSpec::kDefaultConfig;
  config["deck1"_] = deck0;
  config["deck2"_] = deck1;
  config["play_mode"_] = std::string("self");
  config["player"_] = -1;
  config["max_options"_] = 256;
  config["max_steps"_] = 10000;
  config["max_episode_steps"_] = 10000;
  config["greedy_reward"_] = false;
  config["oppo_info"_] = false;
  config["timeout"_] = 10;
  config["seed"_] = seed;
  auto *spec = new YGOProEnvSpec(config);
  g_specs[key] = spec;
  return *spec;
}

inline const YGOProEnvSpec &spec_for(const Game &g) {
  return spec_for_decks(g.deck0, g.deck1, static_cast<int>(g.seed));
}

// Same derivation as ygopro::YGOProEnv's constructor.
inline uint64_t env_seed_of(uint64_t seed_plus_env) {
  std::mt19937 gen(seed_plus_env);
  std::uniform_int_distribution<uint64_t> dist(0, 0xffffffff);
  return dist(gen);
}
inline uint64_t env_seed_of(const Game &g) { return env_seed_of(uint64_t(g.seed) + g.env); }

inline bool g_use_arena = false;

inline FmEnv *new_env(const Game &g, uint64_t env_seed) {
  (void)spec_for(g);
  Scope s(g_use_arena);
  void *mem = malloc(sizeof(FmEnv));
  return new (mem) FmEnv(spec_for(g), env_seed);
}

inline void free_env(FmEnv *e) {
  Scope s(g_use_arena);
  e->destroy();
  e->~FmEnv();
  free(e);
}

inline FmEnv *build_position(const Game &g, int prefix) {
  (void)spec_for(g);
  FmEnv *e = new_env(g, env_seed_of(g));
  Scope s(g_use_arena);
  e->reset();
  for (int i = 0; i < prefix; ++i) e->act(g.actions[i]);
  return e;
}

struct RunResult {
  uint64_t decision_digest = 0;
  uint64_t deep_digest = 0;
  uint64_t obs_digest = 0;
  int decisions = 0;
  int winner = -1;
  int reason = -1;
  int done = 0;
  long diagnostics = 0;
};

inline void absorb(FmEnv *e, RunResult &r, bool deep) {
  r.decision_digest = mix(r.decision_digest, e->decision_hash());
  if (deep) {
    r.deep_digest = mix(r.deep_digest, e->deep_hash());
    r.obs_digest = mix(r.obs_digest, e->obs_hash());
  }
}

inline void finish(FmEnv *e, RunResult &r) {
  r.done = e->done();
  r.winner = e->done() ? e->winner() : -1;
  r.reason = e->done() ? e->win_reason() : -1;
  r.decision_digest = mix(r.decision_digest, r.winner + 1000 * r.reason);
}

// Uniformly random continuation, identical for every method under comparison.
inline RunResult run_random(FmEnv *e, uint64_t seed, int cap, bool hash, bool deep) {
  Scope s(g_use_arena);
  RunResult r;
  if (hash) absorb(e, r, deep);
  while (!e->done() && r.decisions < cap) {
    int n = static_cast<int>(e->actions().size());
    int idx = static_cast<int>(splitmix(seed) % n);
    e->act(idx);
    ++r.decisions;
    if (hash) absorb(e, r, deep);
  }
  finish(e, r);
  return r;
}

// The remaining recorded actions of a real game.
inline RunResult run_recorded(FmEnv *e, const Game &g, int from, bool deep) {
  Scope s(g_use_arena);
  RunResult r;
  absorb(e, r, deep);
  for (size_t i = from; i < g.actions.size(); ++i) {
    e->act(g.actions[i]);
    ++r.decisions;
    absorb(e, r, deep);
  }
  finish(e, r);
  return r;
}

struct Pos {
  int game, bucket, index;
};

inline const char *bucket_name(int b) {
  static const char *names[] = {"early", "mid", "late"};
  return names[b];
}

// One seeded decision per game per stage (10/50/90% of game length, +-5%).
inline std::vector<Pos> sample_positions(const std::vector<Game> &games, int per_bucket,
                                         uint64_t seed) {
  std::vector<Pos> all[3];
  uint64_t x = seed;
  const double centers[3] = {0.10, 0.50, 0.90};
  for (int gi = 0; gi < static_cast<int>(games.size()); ++gi) {
    int n = static_cast<int>(games[gi].actions.size());
    for (int b = 0; b < 3; ++b) {
      double jitter = (splitmix(x) % 1000) / 1000.0 * 0.10 - 0.05;
      int idx = std::clamp(static_cast<int>((centers[b] + jitter) * n), 0, n - 1);
      all[b].push_back({gi, b, idx});
    }
  }
  std::vector<Pos> out;
  for (auto &v : all) {
    for (int i = static_cast<int>(v.size()) - 1; i > 0; --i)
      std::swap(v[i], v[splitmix(x) % (i + 1)]);
    for (int i = 0; i < std::min<int>(per_bucket, v.size()); ++i) out.push_back(v[i]);
  }
  return out;
}

// Sampled evenly across cells: `per_cell` games of each deck pairing, three stages per game. A
// plain shuffle could miss a list, and these checks must cover all 21.
inline std::vector<Pos> sample_positions_per_cell(const std::vector<Game> &games, int per_cell, uint64_t seed) {
  std::map<std::string, std::vector<int>> cells;
  for (int gi = 0; gi < static_cast<int>(games.size()); ++gi)
    cells[games[gi].deck0 + "/" + games[gi].deck1].push_back(gi);
  uint64_t x = seed;
  const double centers[3] = {0.10, 0.50, 0.90};
  std::vector<Pos> out;
  for (auto &kv : cells) {
    auto &ids = kv.second;
    for (int i = static_cast<int>(ids.size()) - 1; i > 0; --i) std::swap(ids[i], ids[splitmix(x) % (i + 1)]);
    for (int i = 0; i < std::min<int>(per_cell, ids.size()); ++i) {
      int n = static_cast<int>(games[ids[i]].actions.size());
      for (int b = 0; b < 3; ++b) {
        double jitter = (splitmix(x) % 1000) / 1000.0 * 0.10 - 0.05;
        out.push_back({ids[i], b, std::clamp(static_cast<int>((centers[b] + jitter) * n), 0, n - 1)});
      }
    }
  }
  for (int i = static_cast<int>(out.size()) - 1; i > 0; --i) std::swap(out[i], out[splitmix(x) % (i + 1)]);
  return out;
}

// Fork a child, run fn, ship the POD result back over a pipe.
template <typename T, typename F>
T in_child(F fn) {
  int fds[2];
  if (pipe(fds)) throw std::runtime_error("pipe");
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    T result = fn();
    size_t off = 0;
    auto *bytes = reinterpret_cast<const char *>(&result);
    while (off < sizeof(T)) {
      ssize_t n = write(fds[1], bytes + off, sizeof(T) - off);
      if (n <= 0) _exit(3);
      off += n;
    }
    _exit(0);
  }
  close(fds[1]);
  T result{};
  size_t off = 0;
  auto *bytes = reinterpret_cast<char *>(&result);
  while (off < sizeof(T)) {
    ssize_t n = read(fds[0], bytes + off, sizeof(T) - off);
    if (n <= 0) break;
    off += n;
  }
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  if (off != sizeof(T) || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    throw std::runtime_error("child failed status=" + std::to_string(status));
  return result;
}

// Stdout goes through a stream of ours, for two reasons. Stdio allocates its buffer on first use, and with
// the arena active that buffer would live in rolled-back memory, so it gets static storage. And production
// prints fail-closed markers and carries on: while a guard is capturing, every byte that leaves the buffer
// lands here to be counted and dropped, so a marker cannot hide behind a flush or survive a failed world.
inline bool g_stdout_capturing = false;
inline long g_stdout_captured = 0;
inline char g_stdout_text[320] = {0};

inline ssize_t stdout_write(void *, const char *data, size_t size) {
  if (g_stdout_capturing) {
    size_t have = g_stdout_captured < long(sizeof g_stdout_text) - 1 ? size_t(g_stdout_captured) : sizeof g_stdout_text - 1;
    size_t take = std::min(size, sizeof g_stdout_text - 1 - have);
    std::memcpy(g_stdout_text + have, data, take);
    g_stdout_text[have + take] = 0;
    g_stdout_captured += size;
    return size;
  }
  size_t done = 0;
  while (done < size) {
    ssize_t n = write(1, data + done, size - done);
    if (n <= 0) return done ? ssize_t(done) : -1;
    done += n;
  }
  return size;
}

inline void pin_stdio() {
  static char out[1 << 16];
  cookie_io_functions_t functions{};
  functions.write = stdout_write;
  FILE *stream = fopencookie(nullptr, "w", functions);
  if (!stream) throw std::runtime_error("fopencookie");
  setvbuf(stream, out, _IOFBF, sizeof out);
  stdout = stream;
}

// The 21 registry lists and the learner's code list, exactly as tools/edison/learner/actor.py
// initialises production. collect_corpus.py writes registry.txt from decks.registry().
inline void init_assets(const std::string &root, const std::string &registry_path = "") {
  pin_stdio();
  std::string registry = registry_path.empty() ? root + "/build/forward_model/corpus/registry.txt" : registry_path;
  std::ifstream in(registry);
  if (!in) throw std::runtime_error("cannot open " + registry);
  std::map<std::string, std::string> decks;
  std::string name, path;
  while (in >> name >> path) decks[name] = path;
  if (decks.size() != 21) throw std::runtime_error("registry does not hold 21 lists");
  ygopro::init_module(root + "/assets/locale/en/cards.cdb", root + "/tools/edison/learner/codes_v1.txt", decks);
  set_message_handler(on_core_message);
}

struct Stats {
  std::vector<double> v;
  void add(double x) { v.push_back(x); }
};

inline FILE *g_out = nullptr;

inline void emit(const std::string &name, std::vector<double> v, const char *unit = "us") {
  if (v.empty()) return;
  std::sort(v.begin(), v.end());
  double sum = 0;
  for (double x : v) sum += x;
  auto q = [&](double f) { return v[std::min(v.size() - 1, static_cast<size_t>(f * v.size()))]; };
  char line[512];
  snprintf(line, sizeof line,
           "{\"name\":\"%s\",\"unit\":\"%s\",\"n\":%zu,\"mean\":%.2f,\"p50\":%.2f,"
           "\"p90\":%.2f,\"p99\":%.2f,\"max\":%.2f}\n",
           name.c_str(), unit, v.size(), sum / v.size(), q(.5), q(.9), q(.99), v.back());
  fputs(line, stdout);
  fflush(stdout);
  if (g_out) {
    fputs(line, g_out);
    fflush(g_out);
  }
}

inline void emit_kv(const std::string &text) {
  puts(text.c_str());
  fflush(stdout);
  if (g_out) {
    fprintf(g_out, "%s\n", text.c_str());
    fflush(g_out);
  }
}
}  // namespace fm
