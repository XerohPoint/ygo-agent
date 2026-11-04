// Shared test bed: the unmodified production environment class, driven natively.
#pragma once
#include <fcntl.h>
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
#define private protected
#include "ygoenv/ygopro/ygopro.h"
#undef private
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
  int turn() const { return turn_count_; }
  int phase() const { return current_phase_; }
  int ms_index() const { return ms_idx_; }

  // Production stamps msg_/cid_/spec_index_ onto legal_actions_ inside WriteState,
  // and step() reads them for the history features. act() therefore does either the
  // full observation encoding or the minimal stamp, then the unmodified step().
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

  void act(int idx) {
    if (g_encode) {
      encode();
    } else {
      for (auto &a : legal_actions_) a.msg_ = msg_;
    }
    step(idx);
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

  // Privileged: full board, hidden cards, deck order, future randomness and
  // processor counters. Used only to compare simulator runs, never by a policy.
  uint64_t deep_hash() {
    uint64_t h = decision_hash();
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

inline const YGOProEnvSpec &spec_for(const Game &g) {
  std::string key = g.deck0 + "/" + g.deck1;
  auto it = g_specs.find(key);
  if (it != g_specs.end()) return *it->second;
  auto config = YGOProEnvSpec::kDefaultConfig;
  config["deck1"_] = g.deck0;
  config["deck2"_] = g.deck1;
  config["play_mode"_] = std::string("self");
  config["player"_] = -1;
  config["max_options"_] = 256;
  config["max_steps"_] = 10000;
  config["max_episode_steps"_] = 10000;
  config["greedy_reward"_] = false;
  config["oppo_info"_] = false;
  config["timeout"_] = 10;
  config["seed"_] = static_cast<int>(g.seed);
  auto *spec = new YGOProEnvSpec(config);
  g_specs[key] = spec;
  return *spec;
}

// Same derivation as ygopro::YGOProEnv's constructor.
inline uint64_t env_seed_of(const Game &g) {
  std::mt19937 gen(g.seed + g.env);
  std::uniform_int_distribution<uint64_t> dist(0, 0xffffffff);
  return dist(gen);
}

inline bool g_use_arena = false;

inline FmEnv *new_env(const Game &g, uint64_t env_seed) {
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
  int decisions = 0;
  int winner = -1;
  int reason = -1;
  int done = 0;
  long diagnostics = 0;
};

inline void absorb(FmEnv *e, RunResult &r, bool deep) {
  r.decision_digest = mix(r.decision_digest, e->decision_hash());
  if (deep) r.deep_digest = mix(r.deep_digest, e->deep_hash());
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

inline void init_assets(const std::string &root) {
  std::map<std::string, std::string> decks;
  for (const char *dir : {"assets/edison/decks", "assets/edison/agent_decks"})
    for (const auto &entry : std::filesystem::directory_iterator(root + "/" + dir))
      decks[entry.path().stem().string()] = entry.path().string();
  ygopro::init_module(root + "/assets/locale/en/cards.cdb",
                      root + "/assets/edison/agent_codes.txt", decks);
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
