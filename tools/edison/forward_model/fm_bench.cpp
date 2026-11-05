#include <alloca.h>
#include <execinfo.h>
#include <sys/resource.h>
// Forward model test bed: production parity, snapshots, determinizer tests and failure handling.
// Usage: fm_bench <command> [--key value ...]   (see docs/edison-forward-model.md)
#define FM_TEST_HOOKS
#include "fm_env.h"
#include "fm_hidden.h"
#include "fm_world.cpp"
#include "pimc.h"

using namespace fm;

namespace {
// The search's privacy test. On every accepted pair of natural duels with one public history, the whole
// search runs in each world through the search API, and its answers are compared. See search/test_search.py.
ednet::Net g_pair_net;
int g_pair_log = -1;
pimc::Config g_pair_config;

std::string search_answer(FmEnv *env, const PublicMemory &memory, const WorldBaseline &baseline, const Snapshot &root, int viewer, const AssumedList &assumed, ednet::Scratch &scratch,
                          std::string *rounded, int *chosen, long *worlds) {
  snapshot_restore(root);
  std::shared_ptr<void> holder;
  auto world = TestAccess::open_on(env, memory, baseline, viewer, holder);
  Observation obs;
  if (world->observe(&obs) != Status::kOk) return "no observation";
  float value = 0;
  const float *logits = nullptr;
  int status = ednet::evaluate_full(g_pair_net, scratch, pimc::to_raw(obs), true, &value, &logits);
  if (status != ednet::kOk) return "net status " + std::to_string(status);
  std::vector<float> root_logits(logits, logits + obs.n_options);
  std::vector<int> allowed;
  for (int i = 0; i < obs.n_options; ++i) allowed.push_back(i);
  DealAssumption assumption;
  assumption.opponent_main = assumed.opponent_main;
  pimc::Result result = pimc::search(*world, g_pair_net, scratch, root_logits.data(), allowed, assumption, g_pair_config, 1);
  std::string exact, close;
  char row[120];
  for (const auto &option : result.options) {
    snprintf(row, sizeof row, "%a %a %d;", option.mean, option.error, option.deals);
    exact += row;
    snprintf(row, sizeof row, "%.3f %d;", option.mean, option.deals);
    close += row;
  }
  snprintf(row, sizeof row, "|%d %d %ld %ld %ld %ld", result.chosen, result.greedy, result.counters.deals_ok, result.counters.deals_refused, result.counters.rollouts, result.counters.worlds_void);
  *rounded = close + row;
  *chosen = result.chosen;
  *worlds = result.counters.deals_ok;
  world.reset();
  return exact + row;
}

void install_search_pair(std::map<std::string, std::string> &args) {
  ednet::Expect expect{args.at("search-params").c_str(), args.at("search-codes").c_str(), {509, 32, 64, 2, 4, 128}};
  const char *err = nullptr;
  if (!ednet::load(g_pair_net, args.at("search-net").c_str(), expect, &err)) throw std::runtime_error(err);
  g_pair_config.budget = std::stoi(args.at("search-budget"));
  g_pair_config.depth = std::stoi(args.at("search-depth"));
  g_pair_config.max_candidates = 0;
  g_pair_config.halving = false;
  g_pair_config.seed = 86005000;
  g_pair_log = open(args.at("search-log").c_str(), O_WRONLY | O_CREAT | O_APPEND | O_TRUNC, 0644);
  g_search_pair = [](const Game &g, const Pos &pos, int viewer, FmEnv *a, TrueDuel &ta, const Snapshot &sa, FmEnv *b, TrueDuel &tb, const Snapshot &sb, const AssumedList &assumed) {
    static ednet::Scratch scratch;
    static bool ready = false;
    const char *problem = nullptr;
    if (!ready && !ednet::create_scratch(scratch, g_pair_net.config, &problem)) throw std::runtime_error(problem);
    ready = true;
    std::string close_a, close_b;
    int chosen_a = -1, chosen_b = -1;
    long worlds_a = 0, worlds_b = 0;
    std::string exact_a = search_answer(a, ta.memory, ta.baseline, sa, viewer, assumed, scratch, &close_a, &chosen_a, &worlds_a);
    std::string exact_b = search_answer(b, tb.memory, tb.baseline, sb, viewer, assumed, scratch, &close_b, &chosen_b, &worlds_b);
    dprintf(g_pair_log, "{\"game\":%d,\"index\":%d,\"viewer\":%d,\"deck0\":\"%s\",\"deck1\":\"%s\",\"exact\":%d,\"close\":%d,\"chosen\":%d,\"worlds\":[%ld,%ld]}\n", pos.game, pos.index, viewer, g.deck0.c_str(),
            g.deck1.c_str(), exact_a == exact_b, close_a == close_b, chosen_a == chosen_b, worlds_a, worlds_b);
  };
}

// sample_positions groups by stage. Walking that list in order would keep a short
// run inside one stage, so throughput runs walk a seeded shuffle of all stages.
std::vector<Pos> mixed_positions(const std::vector<Game> &games, uint64_t seed) {
  auto positions = sample_positions(games, 100000, seed);
  uint64_t x = seed ^ 0x5bd1e995;
  for (int i = static_cast<int>(positions.size()) - 1; i > 0; --i)
    std::swap(positions[i], positions[splitmix(x) % (i + 1)]);
  return positions;
}

std::map<std::string, std::string> g_args;
std::string arg(const std::string &key, const std::string &fallback) {
  auto it = g_args.find(key);
  return it == g_args.end() ? fallback : it->second;
}
int argi(const std::string &key, int fallback) {
  auto it = g_args.find(key);
  return it == g_args.end() ? fallback : std::stoi(it->second);
}

void heap_scramble(uint64_t seed) {
  // Churn the glibc heap so later allocations land at different relative
  // addresses than in a clean process. Deliberately leaves half the blocks live.
  std::vector<void *> blocks;
  for (int i = 0; i < 4000; ++i) {
    size_t size = 16 + splitmix(seed) % 4096;
    blocks.push_back(malloc(size));
  }
  for (size_t i = 0; i < blocks.size(); ++i)
    if (splitmix(seed) & 1) free(blocks[i]);
}

// verify: replay every recorded game against the production corpus. At every decision of every game:
// acting player, option count and the digest of each observation field. At the end: winner and win
// reason. --check-stamp also proves step() sees the same card ids with or without an observation.
// --chains writes per-game digests so separate processes and heaps can be compared.
struct VerifyOut {
  long games, decisions, seat_bad, options_bad, result_bad, games_bad, stamp_bad, diagnostics;
  long field_bad[6];
};

VerifyOut verify_worker(const std::vector<Game> &games, const std::vector<std::vector<std::array<std::string, 6>>> &digests,
                        int worker, int workers, const std::string &heap, bool deep, bool hash, bool check_stamp, bool check_obs,
                        FILE *chains) {
  VerifyOut o{};
  static const char *fields[6] = {"cards_", "global_", "decision_", "selection_", "actions_", "h_actions_"};
  g_use_arena = heap == "arena";
  for (size_t gi = worker; gi < games.size(); gi += workers) {
    const Game &g = games[gi];
    if (heap == "perturb") heap_scramble(gi * 7919 + 13);
    if (g_use_arena) arena_clear();
    FmEnv *e = new_env(g, env_seed_of(g));
    bool bad = false;
    RunResult r;
    {
      Scope s(g_use_arena);
      e->reset();
      if (hash) absorb(e, r, deep);
      for (size_t i = 0; i < g.actions.size(); ++i) {
        bool seat_ok = !e->done() && e->to_play() == g.to_play[i];
        bool options_ok = !e->done() && static_cast<int>(e->actions().size()) == g.options[i];
        o.seat_bad += !seat_ok;
        o.options_bad += !options_ok;
        if (!seat_ok || !options_ok) {
          bad = true;
          fprintf(stderr, "game %zu decision %zu: trace mismatch to_play %d/%d options %zu/%d\n", gi, i, e->to_play(), g.to_play[i],
                  e->actions().size(), g.options[i]);
          break;
        }
        if (check_obs) {
          e->encode();
          auto f = e->obs_fields();
          Scope off(false);
          for (int k = 0; k < 6; ++k) {
            if (Sha256::short_hex(f.data[k], f.size[k]) != digests[gi][i][k]) {
              ++o.field_bad[k];
              if (!bad) fprintf(stderr, "game %zu (%s/%s) decision %zu msg %d: %s differs from production\n", gi, g.deck0.c_str(), g.deck1.c_str(), i, e->msg(), fields[k]);
              bad = true;
            }
          }
        }
        if (check_stamp) {
          // The stamp, then WriteState on the same actions: each must give each action the same id.
          e->stamp();
          std::vector<ygopro::CardId> stamped;
          for (const auto &a : e->actions()) stamped.push_back(a.cid_);
          e->encode();
          for (size_t k = 0; k < stamped.size(); ++k) o.stamp_bad += stamped[k] != e->actions()[k].cid_;
        }
        e->act(g.actions[i]);
        ++r.decisions;
        if (hash) absorb(e, r, deep);
      }
      if (!bad && (!e->done() || e->reported_winner() != g.winner || e->reported_win_reason() != g.reason)) {
        bad = true;
        ++o.result_bad;
        fprintf(stderr, "game %zu: result mismatch winner %d/%d reason %d/%d\n", gi, e->reported_winner(), g.winner,
                e->reported_win_reason(), g.reason);
      }
      finish(e, r);
    }
    ++o.games;
    o.decisions += r.decisions;
    o.games_bad += bad;
    if (chains) fprintf(chains, "%zu %d %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %d %d\n", gi, r.decisions, r.decision_digest, r.deep_digest, r.obs_digest, r.winner, r.reason);
    free_env(e);
  }
  o.diagnostics = g_diagnostics;
  return o;
}

int cmd_verify(const std::vector<Game> &games) {
  std::string heap = arg("heap", "glibc");
  bool deep = argi("deep", 1), hash = argi("hash", 1), check_stamp = argi("check-stamp", 0);
  std::string digest_path = arg("digests", "");
  std::string chains = arg("chains", "");
  int workers = argi("workers", 4);
  std::vector<std::vector<std::array<std::string, 6>>> digests;
  if (!digest_path.empty()) {
    std::ifstream in(digest_path);
    if (!in) throw std::runtime_error("cannot open " + digest_path);
    digests.resize(games.size());
    for (size_t gi = 0; gi < games.size(); ++gi) {
      digests[gi].resize(games[gi].actions.size());
      for (auto &row : digests[gi])
        for (auto &field : row) in >> field;
    }
    if (!in) throw std::runtime_error("digests.txt is shorter than games.txt");
  }
  for (const auto &g : games) (void)spec_for(g);
  std::vector<int> pipes;
  std::vector<pid_t> pids;
  for (int w = 0; w < workers; ++w) {
    int fds[2];
    if (pipe(fds)) return 1;
    pid_t pid = fork();
    if (pid == 0) {
      close(fds[0]);
      FILE *f = chains.empty() ? nullptr : fopen((chains + "." + std::to_string(w)).c_str(), "w");
      VerifyOut o = verify_worker(games, digests, w, workers, heap, deep, hash, check_stamp, !digests.empty(), f);
      if (f) fclose(f);
      (void)!write(fds[1], &o, sizeof o);
      _exit(0);
    }
    close(fds[1]);
    pipes.push_back(fds[0]);
    pids.push_back(pid);
  }
  VerifyOut t{};
  for (int w = 0; w < workers; ++w) {
    VerifyOut o{};
    if (read(pipes[w], &o, sizeof o) != sizeof o) {
      fprintf(stderr, "verify worker %d died\n", w);
      return 1;
    }
    waitpid(pids[w], nullptr, 0);
    auto *a = reinterpret_cast<long *>(&t);
    auto *b = reinterpret_cast<long *>(&o);
    for (size_t i = 0; i < sizeof(VerifyOut) / sizeof(long); ++i) a[i] += b[i];
  }
  std::set<std::string> lists;
  for (const auto &g : games) {
    lists.insert(g.deck0);
    lists.insert(g.deck1);
  }
  char buf[700];
  snprintf(buf, sizeof buf,
           "{\"verify\":\"%s\",\"deep\":%d,\"lists\":%zu,\"games\":%ld,\"decisions\":%ld,\"games_with_a_difference\":%ld,\"seat_differences\":%ld,"
           "\"option_count_differences\":%ld,\"result_differences\":%ld,\"cards_\":%ld,\"global_\":%ld,\"decision_\":%ld,\"selection_\":%ld,"
           "\"actions_\":%ld,\"h_actions_\":%ld,\"observations_compared\":%s,\"stamp_vs_writestate_differences\":%ld,\"diagnostics\":%ld}",
           heap.c_str(), deep, lists.size(), t.games, t.decisions, t.games_bad, t.seat_bad, t.options_bad, t.result_bad, t.field_bad[0],
           t.field_bad[1], t.field_bad[2], t.field_bad[3], t.field_bad[4], t.field_bad[5], digests.empty() ? "false" : "true", t.stamp_bad,
           t.diagnostics);
  emit_kv(buf);
  return t.games_bad != 0 || t.stamp_bad != 0;
}

// replay: the cost of recreating a position from seed plus action prefix.
int cmd_replay(const std::vector<Game> &games) {
  int reps = argi("reps", 3);
  std::string heap = arg("heap", "glibc");
  g_use_arena = heap == "arena";
  auto positions = sample_positions(games, 100000, 85100017);
  std::vector<std::vector<int>> wanted(games.size());
  std::vector<double> create[3], prefix[3], one[3], total[3], index[3], step_all, destroy, game_ms;
  double step_time = 0;
  long step_count = 0;
  for (int rep = 0; rep < reps; ++rep) {
    for (size_t gi = 0; gi < games.size(); ++gi) {
      const Game &g = games[gi];
      if (g_use_arena) arena_clear();
      (void)spec_for(g);
      double t0 = now_us();
      FmEnv *e = new_env(g, env_seed_of(g));
      std::vector<double> cumulative(g.actions.size() + 1);
      {
        Scope s(g_use_arena);
        e->reset();
        double t1 = now_us();
        cumulative[0] = t1 - t0;
        for (size_t i = 0; i < g.actions.size(); ++i) {
          double a = now_us();
          e->act(g.actions[i]);
          double b = now_us();
          cumulative[i + 1] = b - t0;
          step_time += b - a;
          ++step_count;
        }
      }
      for (size_t i = 0; i < g.actions.size(); ++i)
        step_all.push_back(cumulative[i + 1] - cumulative[i]);
      double t2 = now_us();
      free_env(e);
      destroy.push_back(now_us() - t2);
      game_ms.push_back(cumulative.back() / 1000.0);
      for (const auto &p : positions) {
        if (p.game != static_cast<int>(gi)) continue;
        create[p.bucket].push_back(cumulative[0]);
        prefix[p.bucket].push_back(cumulative[p.index] - cumulative[0]);
        one[p.bucket].push_back(cumulative[p.index + 1] - cumulative[p.index]);
        total[p.bucket].push_back(cumulative[p.index + 1] + destroy.back());
        index[p.bucket].push_back(p.index);
      }
    }
  }
  for (int b = 0; b < 3; ++b) {
    std::string tag = "replay/" + heap + "/" + bucket_name(b);
    emit(tag + "/decision_index", index[b], "decisions");
    emit(tag + "/create_duel", create[b]);
    emit(tag + "/prefix", prefix[b]);
    emit(tag + "/one_step", one[b]);
    emit(tag + "/node_total", total[b]);
  }
  emit("replay/" + heap + "/step_all_decisions", step_all);
  emit("replay/" + heap + "/destroy", destroy);
  emit("replay/" + heap + "/full_game", game_ms, "ms");
  char buf[200];
  snprintf(buf, sizeof buf, "{\"replay_engine_decisions_per_second\":%.0f,\"heap\":\"%s\"}",
           step_count / (step_time / 1e6), heap.c_str());
  emit_kv(buf);
  return 0;
}

long self_private_dirty_kb() {
  std::ifstream in("/proc/self/smaps_rollup");
  std::string key;
  long value, total = 0;
  std::string unit;
  while (in >> key) {
    if (key == "Private_Dirty:") {
      in >> value >> unit;
      total += value;
    }
  }
  return total;
}

long status_kb(const std::string &wanted, const std::string &path = "/proc/self/status") {
  std::ifstream in(path);
  std::string key;
  while (in >> key) {
    if (key == wanted) {
      long v;
      in >> v;
      return v;
    }
  }
  return -1;
}

struct ForkOut {
  uint64_t hash;
  int decisions;
  int winner;
  int done;
  long private_dirty_kb;
};

// fork: process copy-on-write snapshot of the entire duel.
int cmd_fork(const std::vector<Game> &games) {
  int per_bucket = argi("positions", 40);
  int option_cap = argi("option-cap", 8);
  int playouts = argi("playouts", 5);
  int ballast_mb = argi("ballast-mb", 0);
  int cap = argi("cap", 5000);
  std::string tag = "fork/ballast" + std::to_string(ballast_mb);
  std::vector<char> ballast(size_t(ballast_mb) << 20, 1);
  for (size_t i = 0; i < ballast.size(); i += 4096) ballast[i] = static_cast<char>(i >> 12);
  auto positions = sample_positions(games, per_bucket, 85100023);
  std::vector<double> bare[3], node[3], playout[3], play_dec[3], dirty_node[3], dirty_play[3],
      rss[3], self_step[3];
  uint64_t x = 85100031;
  for (const auto &p : positions) {
    const Game &g = games[p.game];
    FmEnv *e = build_position(g, p.index);
    rss[p.bucket].push_back(status_kb("VmRSS:"));
    for (int i = 0; i < 5; ++i) {
      double t0 = now_us();
      pid_t pid = fork();
      if (pid == 0) _exit(0);
      int st;
      waitpid(pid, &st, 0);
      bare[p.bucket].push_back(now_us() - t0);
    }
    int options = std::min<int>(option_cap, e->actions().size());
    for (int o = 0; o < options; ++o) {
      double t0 = now_us();
      auto out = in_child<ForkOut>([&] {
        e->act(o);
        ForkOut f{};
        f.hash = e->done() ? 0 : e->decision_hash();
        return f;
      });
      node[p.bucket].push_back(now_us() - t0);
      (void)out;
    }
    for (int o = 0; o < 3; ++o) {
      auto out = in_child<ForkOut>([&] {
        e->act(o % e->actions().size());
        ForkOut f{};
        f.private_dirty_kb = self_private_dirty_kb();
        return f;
      });
      dirty_node[p.bucket].push_back(out.private_dirty_kb);
    }
    for (int i = 0; i < playouts; ++i) {
      uint64_t s = splitmix(x);
      double t0 = now_us();
      auto out = in_child<ForkOut>([&] {
        RunResult r = run_random(e, s, cap, false, false);
        ForkOut f{};
        f.decisions = r.decisions;
        f.winner = r.winner;
        f.done = r.done;
        return f;
      });
      playout[p.bucket].push_back(now_us() - t0);
      play_dec[p.bucket].push_back(out.decisions);
    }
    for (int i = 0; i < 2; ++i) {
      uint64_t s = splitmix(x);
      auto out = in_child<ForkOut>([&] {
        run_random(e, s, cap, false, false);
        ForkOut f{};
        f.private_dirty_kb = self_private_dirty_kb();
        return f;
      });
      dirty_play[p.bucket].push_back(out.private_dirty_kb);
    }
    free_env(e);
  }
  for (int b = 0; b < 3; ++b) {
    std::string t = tag + "/" + bucket_name(b);
    emit(t + "/parent_rss", rss[b], "kB");
    emit(t + "/bare_fork_exit_wait", bare[b]);
    emit(t + "/node_fork_step_pipe_wait", node[b]);
    emit(t + "/playout_total", playout[b]);
    emit(t + "/playout_decisions", play_dec[b], "decisions");
    emit(t + "/child_private_dirty_after_step", dirty_node[b], "kB");
    emit(t + "/child_private_dirty_after_playout", dirty_play[b], "kB");
  }
  return 0;
}

// Keep many snapshots alive as stopped children and measure what they cost.
int cmd_forkmem(const std::vector<Game> &games) {
  int count = argi("children", 200);
  auto positions = sample_positions(games, 1, 85100029);
  for (const auto &p : positions) {
    const Game &g = games[p.game];
    FmEnv *e = build_position(g, p.index);
    long avail0 = status_kb("MemAvailable:", "/proc/meminfo");
    std::vector<pid_t> kids;
    std::vector<int> gates;
    double t0 = now_us();
    for (int i = 0; i < count; ++i) {
      int gate[2];
      if (pipe(gate)) return 1;
      pid_t pid = fork();
      if (pid == 0) {
        close(gate[1]);
        e->act(i % e->actions().size());
        kill(getpid(), SIGSTOP);
        char c;
        (void)!read(gate[0], &c, 1);
        _exit(0);
      }
      close(gate[0]);
      kids.push_back(pid);
      gates.push_back(gate[1]);
      int st;
      waitpid(pid, &st, WUNTRACED);
    }
    double spawn_us = (now_us() - t0) / count;
    long avail1 = status_kb("MemAvailable:", "/proc/meminfo");
    long pte = 0, dirty = 0;
    for (size_t i = 0; i < kids.size(); ++i) {
      pte += status_kb("VmPTE:", "/proc/" + std::to_string(kids[i]) + "/status");
      std::ifstream in("/proc/" + std::to_string(kids[i]) + "/smaps_rollup");
      std::string key, unit;
      long v;
      while (in >> key)
        if (key == "Private_Dirty:") {
          in >> v >> unit;
          dirty += v;
        }
    }
    for (size_t i = 0; i < kids.size(); ++i) {
      kill(kids[i], SIGCONT);
      close(gates[i]);
    }
    for (pid_t k : kids) waitpid(k, nullptr, 0);
    char buf[400];
    snprintf(buf, sizeof buf,
             "{\"forkmem\":\"%s\",\"children\":%d,\"spawn_step_stop_us\":%.1f,"
             "\"memavailable_drop_kb_per_child\":%.1f,\"private_dirty_kb_per_child\":%.1f,"
             "\"vmpte_kb_per_child\":%.1f,\"parent_rss_kb\":%ld}",
             bucket_name(p.bucket), count, spawn_us, (avail0 - avail1) / double(count),
             dirty / double(count), pte / double(count), status_kb("VmRSS:"));
    emit_kv(buf);
    free_env(e);
  }
  return 0;
}

// arena: in-process byte snapshots, full copy and write-tracked.
int cmd_arena(const std::vector<Game> &games) {
  int per_bucket = argi("positions", 40);
  int option_cap = argi("option-cap", 8);
  int playouts = argi("playouts", 5);
  int cap = argi("cap", 5000);
  g_use_arena = true;
  track_install();
  auto positions = sample_positions(games, per_bucket, 85100023);
  std::vector<double> size_kb[3], take[3], restore[3], node_full[3], step_only[3], node_tracked[3],
      dirty_node[3], restore_tracked[3], playout[3], play_dec[3], dirty_play[3], root_build[3],
      restore_play[3], node_tracked_after_play[3];
  uint64_t x = 85100031;
  Snapshot snap;
  for (const auto &p : positions) {
    const Game &g = games[p.game];
    arena_clear();
    double t00 = now_us();
    FmEnv *e = build_position(g, p.index);
    root_build[p.bucket].push_back(now_us() - t00);
    for (int i = 0; i < 5; ++i) {
      double t0 = now_us();
      snapshot_take(snap);
      take[p.bucket].push_back(now_us() - t0);
    }
    size_kb[p.bucket].push_back(snap.size / 1024.0);
    int options = std::min<int>(option_cap, e->actions().size());
    uint64_t sink = 0;
    for (int o = 0; o < options; ++o) {
      double t0 = now_us();
      snapshot_restore(snap);
      double t1 = now_us();
      {
        Scope s(true);
        e->act(o);
        sink += e->done() ? 0 : e->decision_hash();
      }
      double t2 = now_us();
      restore[p.bucket].push_back(t1 - t0);
      step_only[p.bucket].push_back(t2 - t1);
      node_full[p.bucket].push_back(t2 - t0);
    }
    snapshot_restore(snap);
    for (int o = 0; o < options; ++o) {
      track_begin(snap);
      double t0 = now_us();
      {
        Scope s(true);
        e->act(o);
        sink += e->done() ? 0 : e->decision_hash();
      }
      double t1 = now_us();
      dirty_node[p.bucket].push_back(g_dirty_count);
      track_restore(snap);
      double t2 = now_us();
      restore_tracked[p.bucket].push_back(t2 - t1);
      node_tracked[p.bucket].push_back(t2 - t0);
      track_end();
    }
    for (int i = 0; i < playouts; ++i) {
      uint64_t s = splitmix(x);
      double t0 = now_us();
      snapshot_restore(snap);
      double t1 = now_us();
      RunResult r = run_random(e, s, cap, false, false);
      playout[p.bucket].push_back(now_us() - t0);
      restore_play[p.bucket].push_back(t1 - t0);
      play_dec[p.bucket].push_back(r.decisions);
    }
    for (int i = 0; i < 2; ++i) {
      snapshot_restore(snap);
      track_begin(snap);
      run_random(e, splitmix(x), cap, false, false);
      dirty_play[p.bucket].push_back(g_dirty_count);
      track_restore(snap);
      track_end();
    }
    snapshot_restore(snap);
    if (sink == 0x5555) puts("");
    free_env(e);
  }
  for (int b = 0; b < 3; ++b) {
    std::string t = std::string("arena/") + bucket_name(b);
    emit(t + "/root_build_by_replay", root_build[b]);
    emit(t + "/snapshot_size", size_kb[b], "kB");
    emit(t + "/take_full_copy", take[b]);
    emit(t + "/restore_full_copy", restore[b]);
    emit(t + "/step_only", step_only[b]);
    emit(t + "/node_full_restore_plus_step", node_full[b]);
    emit(t + "/node_tracked_step_plus_restore", node_tracked[b]);
    emit(t + "/tracked_restore_only", restore_tracked[b]);
    emit(t + "/dirty_pages_per_node", dirty_node[b], "pages");
    emit(t + "/playout_total_full_restore", playout[b]);
    emit(t + "/playout_decisions", play_dec[b], "decisions");
    emit(t + "/dirty_pages_per_playout", dirty_play[b], "pages");
  }
  return 0;
}

struct ThroughputOut {
  long playouts, decisions, truncated, roots;
  double branch_seconds, root_seconds;
  long wins0;
};

// throughput: playouts per second under N independent worker processes, each
// snapshotting and running uniformly random playouts from sampled real positions.
ThroughputOut throughput_worker(const std::vector<Game> &games, const std::string &method,
                                int worker, double seconds, int cap, int per_root) {
  auto positions = mixed_positions(games, 85100037);
  ThroughputOut out{};
  uint64_t x = 85100041 + worker * 1000003ULL;
  double start = now_us();
  size_t cursor = splitmix(x) % positions.size();
  Snapshot snap;
  if (method == "arena") {
    g_use_arena = true;
  }
  while ((now_us() - start) / 1e6 < seconds) {
    const Pos &p = positions[cursor++ % positions.size()];
    const Game &g = games[p.game];
    if (method == "arena") arena_clear();
    double t0 = now_us();
    FmEnv *e = build_position(g, p.index);
    if (method == "arena") snapshot_take(snap);
    double t1 = now_us();
    out.root_seconds += (t1 - t0) / 1e6;
    ++out.roots;
    for (int i = 0; i < per_root; ++i) {
      uint64_t s = splitmix(x);
      if (method == "arena") {
        snapshot_restore(snap);
        RunResult r = run_random(e, s, cap, false, false);
        out.playouts++;
        out.decisions += r.decisions;
        out.truncated += !r.done;
        out.wins0 += r.winner == 0;
      } else if (method == "fork") {
        auto f = in_child<ForkOut>([&] {
          RunResult r = run_random(e, s, cap, false, false);
          ForkOut o{};
          o.decisions = r.decisions;
          o.winner = r.winner;
          o.done = r.done;
          return o;
        });
        out.playouts++;
        out.decisions += f.decisions;
        out.truncated += !f.done;
        out.wins0 += f.winner == 0;
      } else {  // full replay per playout
        if (i) {
          free_env(e);
          e = build_position(g, p.index);
        }
        RunResult r = run_random(e, s, cap, false, false);
        out.playouts++;
        out.decisions += r.decisions;
        out.truncated += !r.done;
        out.wins0 += r.winner == 0;
      }
    }
    out.branch_seconds += (now_us() - t1) / 1e6;
    if (method == "arena") snapshot_restore(snap);
    if (method != "replay" || e->started()) free_env(e);
    else free_env(e);
  }
  return out;
}

int cmd_throughput(const std::vector<Game> &games) {
  std::string method = arg("method", "arena");
  int workers = argi("workers", 1);
  double seconds = std::stod(arg("seconds", "10"));
  int cap = argi("cap", 5000);
  int per_root = argi("per-root", 16);
  for (const auto &g : games) (void)spec_for(g);
  std::vector<int> pipes;
  std::vector<pid_t> pids;
  for (int w = 0; w < workers; ++w) {
    int fds[2];
    if (pipe(fds)) return 1;
    pid_t pid = fork();
    if (pid == 0) {
      close(fds[0]);
      ThroughputOut o = throughput_worker(games, method, w, seconds, cap, per_root);
      (void)!write(fds[1], &o, sizeof o);
      _exit(0);
    }
    close(fds[1]);
    pipes.push_back(fds[0]);
    pids.push_back(pid);
  }
  ThroughputOut total{};
  double wall_branch = 0;
  for (int w = 0; w < workers; ++w) {
    ThroughputOut o{};
    if (read(pipes[w], &o, sizeof o) != sizeof o) {
      fprintf(stderr, "worker %d failed\n", w);
      return 1;
    }
    waitpid(pids[w], nullptr, 0);
    total.playouts += o.playouts;
    total.decisions += o.decisions;
    total.truncated += o.truncated;
    total.roots += o.roots;
    total.branch_seconds += o.branch_seconds;
    total.root_seconds += o.root_seconds;
    total.wins0 += o.wins0;
    wall_branch = std::max(wall_branch, o.branch_seconds + o.root_seconds);
  }
  char buf[500];
  snprintf(buf, sizeof buf,
           "{\"throughput\":\"%s\",\"workers\":%d,\"per_root\":%d,\"playouts\":%ld,"
           "\"playouts_per_sec_total\":%.1f,\"playouts_per_sec_per_worker\":%.1f,"
           "\"decisions_per_sec_total\":%.0f,\"decisions_per_playout\":%.1f,\"truncated\":%ld,"
           "\"roots\":%ld,\"root_ms_mean\":%.2f,\"wall_seconds\":%.2f,\"diagnostics\":%ld}",
           method.c_str(), workers, per_root, total.playouts, total.playouts / wall_branch,
           total.playouts / wall_branch / workers, total.decisions / wall_branch,
           total.decisions / double(std::max(1L, total.playouts)), total.truncated, total.roots,
           1000 * total.root_seconds / std::max(1L, total.roots), wall_branch, g_diagnostics);
  emit_kv(buf);
  return 0;
}

// Concurrent children under one fork parent.
int cmd_fork_conc(const std::vector<Game> &games) {
  int concurrent = argi("concurrent", 1);
  double seconds = std::stod(arg("seconds", "10"));
  int cap = argi("cap", 5000);
  auto positions = mixed_positions(games, 85100043);
  uint64_t x = 85100047;
  long done = 0, decisions = 0;
  double start = now_us();
  size_t cursor = 0;
  std::vector<double> fork_call;
  while ((now_us() - start) / 1e6 < seconds) {
    const Pos &p = positions[cursor++ % positions.size()];
    FmEnv *e = build_position(games[p.game], p.index);
    std::map<pid_t, int> active;
    for (int launched = 0; launched < 16 || !active.empty();) {
      while (launched < 16 && static_cast<int>(active.size()) < concurrent) {
        int fds[2];
        if (pipe(fds)) return 1;
        uint64_t s = splitmix(x);
        double t0 = now_us();
        pid_t pid = fork();
        if (pid == 0) {
          RunResult r = run_random(e, s, cap, false, false);
          int value = r.decisions;
          (void)!write(fds[1], &value, sizeof value);
          _exit(0);
        }
        fork_call.push_back(now_us() - t0);
        close(fds[1]);
        active[pid] = fds[0];
        ++launched;
      }
      int st;
      pid_t pid = waitpid(-1, &st, 0);
      int value = 0;
      (void)!read(active[pid], &value, sizeof value);
      close(active[pid]);
      active.erase(pid);
      ++done;
      decisions += value;
    }
    free_env(e);
  }
  double wall = (now_us() - start) / 1e6;
  emit("fork_conc/" + std::to_string(concurrent) + "/fork_call", fork_call);
  char buf[300];
  snprintf(buf, sizeof buf,
           "{\"fork_concurrent\":%d,\"playouts_per_sec\":%.1f,\"decisions_per_sec\":%.0f,"
           "\"decisions_per_playout\":%.1f,\"wall_seconds\":%.2f}",
           concurrent, done / wall, decisions / wall, decisions / double(std::max(1L, done)), wall);
  emit_kv(buf);
  return 0;
}

constexpr int kMaxOptions = 256;
struct OptionDigests {
  int n;
  uint64_t decision[kMaxOptions], deep[kMaxOptions], obs[kMaxOptions], bytes[kMaxOptions];
  int decisions[kMaxOptions], winner[kMaxOptions], reason[kMaxOptions];
  long diagnostics;
};

uint64_t option_seed(const Pos &p, int option) {
  return mix(mix(mix(85100053, p.game), p.index), option);
}

// Every byte the allocator has handed out or holds free, allocator metadata included.
uint64_t arena_digest() {
  uint64_t h = 0xa7;
  auto *p = reinterpret_cast<const uint64_t *>(kArenaBase);
  size_t words = arena_used() / 8;
  for (size_t i = 0; i < words; ++i) h = (h ^ p[i]) * 0x100000001b3ULL + (h >> 47);
  return h;
}

// One option from this position, then the seeded random policy to the end. The position's own full state
// goes into the digest first, so whatever a restore failed to put back is seen before anything moves.
RunResult run_option(FmEnv *e, const Pos &p, int option, int cap) {
  uint64_t root;
  {
    Scope s(g_use_arena);
    root = mix(e->deep_hash(), e->obs_hash());
    e->act(option);
  }
  RunResult r = run_random(e, option_seed(p, option), cap, true, true);
  r.deep_digest = mix(r.deep_digest, root);
  return r;
}

// A restore that also zeroes what the previous run left above the snapshot, so the memory matches a
// process that never ran anything there. Only the byte comparison needs this: the other restore
// checks deliberately leave that garbage in place, and one fills it.
void restore_with_clean_tail(FmEnv *e, const Snapshot &snap) {
  size_t prior = arena_used();
  restore_world(e, snap);
  if (prior > snap.size) std::memset(reinterpret_cast<void *>(kArenaBase + snap.size), 0, prior - snap.size);
}

// Struct copies carry padding bytes from the stack into the heap, so the arena's bytes also depend on
// what earlier calls left on the stack. Each run below starts with that region zeroed, at one fixed
// stack depth, or two exact runs could differ in bytes nothing can read.
// alloca lasts until its function returns, so the zeroing has its own frame.
__attribute__((noinline)) void zero_stack_below() {
  char *below = static_cast<char *>(alloca(size_t(1) << 20));
  std::memset(below, 0, size_t(1) << 20);
  asm volatile("" : : "r"(below) : "memory");  // This store is dead to the compiler, and must still happen.
}

__attribute__((noinline)) void byte_run(FmEnv *e, const Pos &p, int option, int cap) {
  zero_stack_below();
  run_option(e, p, option, cap);
}

struct ArenaRun {
  size_t used;
  uint64_t env;
};

constexpr size_t kByteCopy = size_t(1) << 25;

// Runs inside its own child, always in this one function. With a snapshot, other options run first and
// the world is restored over what they left; without one, the option runs straight from the root. The
// finished arena goes to shared memory for the parent to compare byte for byte.
__attribute__((noinline)) ArenaRun byte_child(FmEnv *e, const Snapshot *snap, const Pos &p, int option, int options, int cap, char *copy) {
  if (snap) {
    for (int other = 0; other < options && other < 3; ++other) {
      if (other == option) continue;
      restore_with_clean_tail(e, *snap);
      byte_run(e, p, other, cap);
    }
    restore_with_clean_tail(e, *snap);
  }
  byte_run(e, p, option, cap);
  ArenaRun r{arena_used(), 0};
  std::memcpy(copy, reinterpret_cast<void *>(kArenaBase), std::min(r.used, kByteCopy));
  Scope s(true);
  r.env = e->env_hash();
  return r;
}

// The size of the allocation holding this offset, and the offset inside it. Walks the arena's own
// block headers in a copy of it.
std::pair<size_t, size_t> block_at(const char *arena, size_t used, size_t offset) {
  size_t at = (sizeof(ArenaHeader) + 15) & ~size_t(15);
  while (at + sizeof(BlockHeader) <= used) {
    auto *block = reinterpret_cast<const BlockHeader *>(arena + at);
    if (offset < at + sizeof(BlockHeader) + block->size) {
      size_t inside = offset >= at + sizeof(BlockHeader) ? offset - at - sizeof(BlockHeader) : size_t(-1);
      return {block->size, inside};
    }
    at += sizeof(BlockHeader) + block->size;
  }
  return {0, 0};
}

struct CorrectOut {
  long branch_points, options, decisions;
  long replay_vs_replay_bad, fork_bad, arena_bad, arena_decision_bad, arena_deep_bad, arena_obs_bad, arena_reverse_bad, arena_bytes_bad,
      arena_bytes_compared, arena_bytes_total, arena_bytes_differing, arena_bytes_worst, env_state_bad, tracked_bad, scrub_bad, perturb_bad, return_bad,
      diagnostics, truncated, position_failures;
};

CorrectOut correct_worker(const std::vector<Game> &games, const std::vector<Pos> &positions,
                          int worker, int workers, int cap, FILE *log) {
  CorrectOut out{};
  track_install();
  for (size_t pi = worker; pi < positions.size(); pi += workers) {
    const Pos &p = positions[pi];
    const Game &g = games[p.game];
    auto mismatch = [&](const char *method, int option, const RunResult &a, uint64_t dec, uint64_t deep, uint64_t obs,
                        int winner, int reason, long *dec_count = nullptr, long *deep_count = nullptr, long *obs_count = nullptr) {
      bool bad_dec = a.decision_digest != dec || a.winner != winner || a.reason != reason;
      bool bad_deep = a.deep_digest != deep, bad_obs = a.obs_digest != obs;
      if (dec_count) *dec_count += bad_dec;
      if (deep_count) *deep_count += bad_deep;
      if (obs_count) *obs_count += bad_obs;
      bool bad = bad_dec || bad_deep || bad_obs;
      if (bad && log)
        fprintf(log, "MISMATCH %s game=%d(%s/%s seed=%u env=%d) index=%d option=%d decisions=%d winner %d/%d decision=%d deep=%d obs=%d\n", method,
                p.game, g.deck0.c_str(), g.deck1.c_str(), g.seed, g.env, p.index, option, a.decisions, a.winner, winner, bad_dec, bad_deep, bad_obs);
      return bad;
    };
    try {
      int n_options = g.options[p.index];
      // Reference: a straight uninterrupted run in a fresh process with no snapshots.
      auto straight = [&](int option) {
        return in_child<RunResult>([&] {
          g_use_arena = false;
          FmEnv *e = build_position(g, p.index);
          RunResult run = run_option(e, p, option, cap);
          run.diagnostics = g_diagnostics;
          return run;
        });
      };
      std::vector<RunResult> ref(n_options);
      for (int o = 0; o < n_options; ++o) {
        ref[o] = straight(o);
        out.options++;
        out.decisions += ref[o].decisions;
        out.diagnostics += ref[o].diagnostics;
        out.truncated += !ref[o].done;
      }
      RunResult ref_recorded = in_child<RunResult>([&] {
        g_use_arena = false;
        FmEnv *e = build_position(g, p.index);
        return run_recorded(e, g, p.index, true);
      });
      // The straight run again, to establish that the reference is itself stable.
      for (int o = 0; o < std::min(n_options, 2); ++o) {
        RunResult again = straight(o);
        out.replay_vs_replay_bad += mismatch("replay-repeat", o, again, ref[o].decision_digest, ref[o].deep_digest, ref[o].obs_digest,
                                             ref[o].winner, ref[o].reason);
      }
      // Replay in a deliberately scrambled heap: does any behavior follow addresses?
      for (int o = 0; o < std::min(n_options, 2); ++o) {
        RunResult perturbed = in_child<RunResult>([&] {
          g_use_arena = false;
          heap_scramble(mix(p.game, p.index));
          FmEnv *e = build_position(g, p.index);
          return run_option(e, p, o, cap);
        });
        out.perturb_bad += mismatch("replay-perturbed-heap", o, perturbed, ref[o].decision_digest, ref[o].deep_digest, ref[o].obs_digest,
                                    ref[o].winner, ref[o].reason);
      }
      // Fork: one holder process at the position; each option in a child of it.
      OptionDigests fork_digests = in_child<OptionDigests>([&] {
        g_use_arena = false;
        FmEnv *e = build_position(g, p.index);
        OptionDigests d{};
        d.n = n_options;
        for (int o = 0; o < n_options; ++o) {
          RunResult r = in_child<RunResult>([&] {
            return run_option(e, p, o, cap);
          });
          d.decision[o] = r.decision_digest;
          d.deep[o] = r.deep_digest;
          d.obs[o] = r.obs_digest;
          d.winner[o] = r.winner;
          d.reason[o] = r.reason;
        }
        return d;
      });
      for (int o = 0; o < n_options; ++o) {
        if (fork_digests.decision[o] != ref[o].decision_digest || fork_digests.deep[o] != ref[o].deep_digest ||
            fork_digests.obs[o] != ref[o].obs_digest || fork_digests.winner[o] != ref[o].winner ||
            fork_digests.reason[o] != ref[o].reason) {
          ++out.fork_bad;
          if (log) fprintf(log, "MISMATCH fork game=%d index=%d option=%d\n", p.game, p.index, o);
        }
      }
      // Arena: go back repeatedly within one process, forward and reversed order.
      g_use_arena = true;
      arena_clear();
      FmEnv *e = build_position(g, p.index);
      Snapshot snap;
      snapshot_take(snap);
      for (int o = 0; o < n_options; ++o) {
        restore_world(e, snap);
        RunResult r = run_option(e, p, o, cap);
        out.arena_bad += mismatch("arena", o, r, ref[o].decision_digest, ref[o].deep_digest, ref[o].obs_digest, ref[o].winner,
                                  ref[o].reason, &out.arena_decision_bad, &out.arena_deep_bad, &out.arena_obs_bad);
      }
      for (int o = n_options - 1; o >= 0; --o) {
        restore_world(e, snap);
        RunResult r = run_option(e, p, o, cap);
        out.arena_reverse_bad += mismatch("arena-reverse", o, r, ref[o].decision_digest, ref[o].deep_digest, ref[o].obs_digest,
                                          ref[o].winner, ref[o].reason);
      }
      // Whole arena against a straight child: every byte of every object, live or freed, allocator
      // included. This covers state no hash above thought of. Both start from this very root. One child
      // runs the option directly. The other runs other options first and restores over what they left.
      restore_world(e, snap);
      // The earlier runs left garbage above the root. Both children get clean memory there.
      madvise(reinterpret_cast<void *>(kArenaBase + snap.size), kArenaCap - snap.size, MADV_DONTNEED);
      static char *copies = static_cast<char *>(mmap(nullptr, 2 * kByteCopy, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
      for (int o = 0; o < n_options; ++o) {
        ArenaRun straight_run = in_child<ArenaRun>([&] { return byte_child(e, nullptr, p, o, n_options, cap, copies); });
        ArenaRun run = in_child<ArenaRun>([&] { return byte_child(e, &snap, p, o, n_options, cap, copies + kByteCopy); });
        ++out.arena_bytes_compared;
        out.arena_bytes_total += std::min(straight_run.used, kByteCopy);
        long differing = 0;
        if (run.used != straight_run.used || straight_run.used > kByteCopy) {
          differing = 1 << 30;
        } else {
          for (size_t i = 0; i < run.used; ++i) {
            if (copies[i] == copies[kByteCopy + i]) continue;
            if (log && differing < 64) {
              auto block = block_at(copies, run.used, i);
              fprintf(log, "BYTE game=%d(%s/%s) index=%d option=%d offset=%zu straight=%u restored=%u block_size=%zu in_block=%zd\n", p.game, g.deck0.c_str(),
                      g.deck1.c_str(), p.index, o, i, uint8_t(copies[i]), uint8_t(copies[kByteCopy + i]), block.first, ssize_t(block.second));
            }
            ++differing;
          }
        }
        if (differing) {
          ++out.arena_bytes_bad;
          out.arena_bytes_differing += differing;
          out.arena_bytes_worst = std::max(out.arena_bytes_worst, differing);
          if (log) fprintf(log, "MISMATCH arena-bytes game=%d(%s/%s) index=%d option=%d differing=%ld of %zu\n", p.game, g.deck0.c_str(), g.deck1.c_str(), p.index, o, differing, run.used);
        }
        if (run.env != straight_run.env) {
          ++out.env_state_bad;
          if (log) fprintf(log, "MISMATCH env-state game=%d(%s/%s) index=%d option=%d\n", p.game, g.deck0.c_str(), g.deck1.c_str(), p.index, o);
        }
      }
      // Uninitialised-memory probe: distinct garbage above the allocator top.
      for (int o = 0; o < n_options; ++o) {
        restore_world(e, snap);
        arena_scrub(static_cast<uint8_t>(0x11 * (o + 1)), 1 << 20);
        RunResult r = run_option(e, p, o, cap);
        out.scrub_bad += mismatch("arena-scrub", o, r, ref[o].decision_digest, ref[o].deep_digest, ref[o].obs_digest, ref[o].winner,
                                  ref[o].reason);
      }
      // Return to the position and finish the real game as recorded.
      restore_world(e, snap);
      RunResult rec = run_recorded(e, g, p.index, true);
      out.return_bad += mismatch("arena-return", -1, rec, ref_recorded.decision_digest, ref_recorded.deep_digest, ref_recorded.obs_digest,
                                 ref_recorded.winner, ref_recorded.reason);
      if (rec.winner != ref_recorded.winner || rec.reason != ref_recorded.reason) ++out.return_bad;
      out.diagnostics += g_diagnostics;
      g_diagnostics = 0;
      restore_world(e, snap);
      free_env(e);
      g_use_arena = false;
      __libc_free(snap.data);
      out.branch_points++;
    } catch (const std::exception &ex) {
      g_use_arena = false;
      g_active = false;
      ++out.position_failures;
      if (log) fprintf(log, "FAILED game=%d(%s/%s) index=%d: %s\n", p.game, g.deck0.c_str(), g.deck1.c_str(), p.index, ex.what());
    }
    if (log) fflush(log);
  }
  return out;
}

int cmd_correct(const std::vector<Game> &games) {
  int per_cell = argi("per-cell", 3);
  int workers = argi("workers", 4);
  int cap = argi("cap", 3000);
  std::string log_path = arg("log", "");
  auto positions = sample_positions_per_cell(games, per_cell, 85100051);
  if (argi("only-game", -1) >= 0) positions = {Pos{argi("only-game", 0), 0, argi("only-index", 0)}};  // To diagnose one logged case.
  std::set<std::string> lists;
  for (const auto &p : positions) {
    lists.insert(games[p.game].deck0);
    lists.insert(games[p.game].deck1);
  }
  for (const auto &g : games) (void)spec_for(g);
  std::vector<int> pipes;
  std::vector<pid_t> pids;
  for (int w = 0; w < workers; ++w) {
    int fds[2];
    if (pipe(fds)) return 1;
    pid_t pid = fork();
    if (pid == 0) {
      close(fds[0]);
      FILE *log = nullptr;
      if (!log_path.empty()) log = fopen((log_path + "." + std::to_string(w)).c_str(), "w");
      CorrectOut o = correct_worker(games, positions, w, workers, cap, log);
      if (log) fclose(log);
      (void)!write(fds[1], &o, sizeof o);
      _exit(0);
    }
    close(fds[1]);
    pipes.push_back(fds[0]);
    pids.push_back(pid);
  }
  CorrectOut t{};
  for (int w = 0; w < workers; ++w) {
    CorrectOut o{};
    size_t off = 0;
    auto *bytes = reinterpret_cast<char *>(&o);
    while (off < sizeof o) {
      ssize_t n = read(pipes[w], bytes + off, sizeof o - off);
      if (n <= 0) break;
      off += n;
    }
    if (off != sizeof o) {
      fprintf(stderr, "correct worker %d died\n", w);
      return 1;
    }
    waitpid(pids[w], nullptr, 0);
    auto *a = reinterpret_cast<long *>(&t);
    auto *b = reinterpret_cast<long *>(&o);
    long worst = std::max(t.arena_bytes_worst, o.arena_bytes_worst);
    for (size_t i = 0; i < sizeof(CorrectOut) / sizeof(long); ++i) a[i] += b[i];
    t.arena_bytes_worst = worst;
  }
  char buf[900];
  snprintf(buf, sizeof buf,
           "{\"correct\":true,\"mutant\":\"%s\",\"cap\":%d,\"lists\":%zu,\"branch_points\":%ld,\"options\":%ld,"
           "\"reference_decisions\":%ld,\"truncated_by_cap\":%ld,\"position_failures\":%ld,\"replay_repeat_bad\":%ld,"
           "\"replay_perturbed_heap_bad\":%ld,\"fork_bad\":%ld,\"arena_bad\":%ld,\"arena_decision_bad\":%ld,\"arena_deep_bad\":%ld,"
           "\"arena_obs_bad\":%ld,\"arena_reverse_bad\":%ld,\"arena_scrub_bad\":%ld,\"arena_bytes_compared\":%ld,\"arena_bytes_total\":%ld,\"arena_bytes_bad\":%ld,\"arena_bytes_differing\":%ld,\"arena_bytes_worst\":%ld,"
           "\"env_state_bad\":%ld,\"arena_return_bad\":%ld,\"diagnostics\":%ld}",
           arg("mutant", "none").c_str(), cap, lists.size(), t.branch_points, t.options, t.decisions, t.truncated, t.position_failures,
           t.replay_vs_replay_bad, t.perturb_bad, t.fork_bad, t.arena_bad, t.arena_decision_bad, t.arena_deep_bad, t.arena_obs_bad,
           t.arena_reverse_bad, t.scrub_bad, t.arena_bytes_compared, t.arena_bytes_total, t.arena_bytes_bad, t.arena_bytes_differing, t.arena_bytes_worst, t.env_state_bad, t.return_bad, t.diagnostics);
  emit_kv(buf);
  return 0;
}

// Which part of the deep hash differs between two runs of one option, plain and in a scrambled heap.
// A diagnosis tool, for cases the correct command logs.
struct DeepTrace {
  int n;
  uint64_t decision[4000], env[4000], obs[4000], core[4000];
  uint64_t query[4000][14];
  std::array<char, 200> text;
};

int cmd_deepdiff(const std::vector<Game> &games) {
  int gi = argi("game", 0), index = argi("index", 0), option = argi("option", 0), cap = argi("cap", 3000);
  const Game &g = games[gi];
  Pos p{gi, 0, index};
  static const int locations[] = {LOCATION_DECK, LOCATION_HAND, LOCATION_MZONE, LOCATION_SZONE, LOCATION_GRAVE, LOCATION_REMOVED, LOCATION_EXTRA};
  auto trace = [&](bool scramble) {
    auto *out = new DeepTrace();
    int fds[2];
    if (pipe(fds)) throw std::runtime_error("pipe");
    pid_t pid = fork();
    if (pid == 0) {
      g_use_arena = false;
      if (scramble) heap_scramble(mix(gi, index));
      FmEnv *e = build_position(g, index);
      e->act(option);
      uint64_t seed = option_seed(p, option);
      int n = 0;
      for (;;) {
        out->decision[n] = e->decision_hash();
        out->env[n] = e->env_hash();
        out->obs[n] = e->obs_hash();
        out->core[n] = e->deep_hash();
        if (e->started())
          for (int q = 0; q < 2; ++q)
            for (int l = 0; l < 7; ++l) {
              int bytes = query_field_card(reinterpret_cast<intptr_t>(e->core()), q, locations[l], kFullQuery, g_query_buf, 0);
              uint64_t h = 0;
              for (int i = 0; i < bytes; ++i) h = mix(h, g_query_buf[i]);
              out->query[n][q * 7 + l] = h;
            }
        ++n;
        if (e->done() || n >= cap) break;
        e->act(static_cast<int>(splitmix(seed) % e->actions().size()));
      }
      out->n = n;
      (void)!write(fds[1], out, sizeof *out);
      _exit(0);
    }
    close(fds[1]);
    size_t off = 0;
    while (off < sizeof *out) {
      ssize_t n = read(fds[0], reinterpret_cast<char *>(out) + off, sizeof *out - off);
      if (n <= 0) break;
      off += n;
    }
    waitpid(pid, nullptr, 0);
    return out;
  };
  DeepTrace *a = trace(false), *b = trace(true);
  for (int i = 0; i < std::min(a->n, b->n); ++i) {
    if (a->core[i] == b->core[i] && a->env[i] == b->env[i] && a->decision[i] == b->decision[i] && a->obs[i] == b->obs[i]) continue;
    printf("first difference at decision %d of %d/%d: decision=%d env=%d obs=%d deep=%d query:", i, a->n, b->n, a->decision[i] != b->decision[i],
           a->env[i] != b->env[i], a->obs[i] != b->obs[i], a->core[i] != b->core[i]);
    for (int k = 0; k < 14; ++k)
      if (a->query[i][k] != b->query[i][k]) printf(" player%d/location%d", k / 7, k % 7);
    printf("\n");
    fflush(stdout);
    return 0;
  }
  printf("no difference in %d/%d decisions\n", a->n, b->n);
  fflush(stdout);
  return 0;
}

// Does a playout change anything in this program's static data? The arena holds the heap, so only a
// static could carry state past a restore. Prints each changed range for the caller to map to symbols.
extern "C" char __data_start, _end;

int cmd_statics(const std::vector<Game> &games) {
  int gi = argi("game", 0), index = argi("index", 40);
  const Game &g = games[gi];
  g_use_arena = true;
  arena_clear();
  FmEnv *e = build_position(g, index);
  Snapshot snap;
  snapshot_take(snap);
  { Scope s(true); e->obs_hash(); }
  auto *begin = &__data_start;
  size_t size = &_end - begin;
  std::vector<char> before(begin, begin + size);
  auto play = [&](uint64_t seed) {
    snapshot_restore(snap);
    run_random(e, seed, 3000, true, true);
  };
  long changed = 0;
  for (uint64_t seed : {uint64_t(1), uint64_t(2), uint64_t(3)}) play(seed);
  std::map<size_t, size_t> ranges;
  for (size_t i = 0; i < size; ++i) {
    if (begin[i] == before[i]) continue;
    size_t start = i;
    while (i + 32 < size && std::memcmp(begin + i, before.data() + i, 32) != 0) i += 32;
    ranges[start] = i - start + 1;
    ++changed;
  }
  for (const auto &kv : ranges) printf("changed %zu size %zu\n", kv.first, kv.second);
  printf("static data %zu bytes, %ld changed ranges\n", size, changed);
  fflush(stdout);
  return 0;
}

// -- Failure inside sampled worlds ------------------------------------------------------------------
// Drives only the search API, so this tests what a search gets. Run from a directory whose ./script
// holds the planted card. One game and root, many deals. Each iteration restores, deals and plays
// randomly to the end, chaining every observation byte and the privileged deep hash into a digest.
constexpr int kVoidSeeds = 512;
struct VoidRun {
  uint8_t status[kVoidSeeds];       // 0 finished, 1 voided, 2 refused
  uint8_t reason[kVoidSeeds];       // VoidReason of a void
  uint64_t digest[kVoidSeeds];
  long voids[int(VoidReason::kCount)];
  long refused, finished, decisions, prefix_diagnostics;
  char sample[200];
};

struct VoidRoot {
  int game = -1, index = 0, viewer = 0;
};

void play_void_iteration(SearchWorld &world, uint64_t seed, const DealAssumption &assumption, VoidRun &out, int i, int cap) {
  world.restore(SearchWorld::kRoot);
  DealResult deal = world.deal(mix(seed, i), assumption);
  if (!deal.ok) {
    out.status[i] = 2;
    ++out.refused;
    return;
  }
  uint64_t x = mix(seed ^ 0x77, i), digest = 0;
  Status s = Status::kOk;
  int decisions = 0;
  while (s == Status::kOk && decisions < cap) {
    Observation obs;
    s = world.observe(&obs);
    if (s != Status::kOk) break;
    auto *e = TestAccess::env(world);
    {
      Scope arena(true);
      digest = mix(digest, e->obs_hash());
      digest = mix(digest, e->deep_hash());
    }
    s = world.step(static_cast<int>(splitmix(x) % world.option_count()));
    ++decisions;
  }
  out.decisions += decisions;
  if (s == Status::kVoid) {
    out.status[i] = 1;
    out.reason[i] = static_cast<uint8_t>(world.void_reason());
    ++out.voids[int(world.void_reason())];
    if (!out.sample[0]) snprintf(out.sample, sizeof out.sample, "%s", world.last_diagnostic());
    return;
  }
  ++out.finished;
  out.digest[i] = mix(digest, world.winner() + 1000);
}

int cmd_void(const std::vector<Game> &games) {
  uint32_t planted = static_cast<uint32_t>(argi("planted-card", 0));
  std::string plant = arg("plant", "lua");
  int seeds = std::min(argi("seeds", 256), kVoidSeeds), cap = argi("cap", 3000);
  uint64_t seed = 85100071;
  // A root where the planted card is not held by the viewer, and whose recorded line never fails: in
  // the true game the failure does not occur at all. Only a sampled world can reach it.
  struct Found {
    int game, index, viewer, whole_game_diagnostics;
  };
  // Each game replays in its own child with a CPU limit: on the way past an unsuitable game, the planted
  // failure can fire here with no guard around it, and a hang would stop the search for a root.
  Found root{-1, 0, 0, 0};
  g_use_arena = false;
  for (size_t gi = 0; gi < games.size() && root.game < 0; ++gi) {
    const Game &g = games[gi];
    const auto &m0 = ygopro::main_decks_.at(g.deck0), &m1 = ygopro::main_decks_.at(g.deck1);
    bool in0 = std::count(m0.begin(), m0.end(), planted), in1 = std::count(m1.begin(), m1.end(), planted);
    if (in0 == in1) continue;  // The planted card in exactly one list: the viewer can never hold it.
    int holder = in1 ? 1 : 0;
    try {
      root = in_child<Found>([&] {
        alarm(10);
        Found f{-1, 0, 0, 0};
        FmEnv *e = new_env(g, env_seed_of(g));
        int best = -1;
        bool held = false;
        try {
          e->reset();
          for (size_t i = 0; i < g.actions.size() && !held; ++i) {
            for (card *c : e->core()->game_field->player[holder].list_hand) held = held || c->data.code == planted;
            if (!held && e->to_play() == 1 - holder && i > g.actions.size() / 8 && i < g.actions.size() / 2) best = static_cast<int>(i);
            e->act(g.actions[i]);
          }
        } catch (const std::exception &) {
          held = true;
        }
        if (!g_diagnostics && !held && best >= 0) f = {static_cast<int>(gi), best, 1 - holder, 0};
        return f;
      });
    } catch (const std::exception &) {
    }
  }
  if (root.game < 0) {
    fprintf(stderr, "no game suits this planted card\n");
    return 1;
  }
  const Game &g = games[root.game];
  DealAssumption assumption;
  assumption.opponent_main = ygopro::main_decks_.at(root.viewer == 0 ? g.deck1 : g.deck0);
  auto build = [&]() {
    auto duel = std::make_unique<Referee>(g.deck0, g.deck1, uint64_t(g.seed) + g.env);
    for (int i = 0; i < root.index; ++i) duel->play(g.actions[i]);
    return duel;
  };
  auto mixed_run = [&](const std::vector<int> &order) {
    return in_child<VoidRun>([&] {
      VoidRun run{};
      auto duel = build();
      auto world = duel->open_search(root.viewer);
      for (int i : order) {
        if ((plant == "segv" || plant == "abort" || plant == "marker") && i % 16 == 3) {
          // A real fault inside the guarded call, as a crash in the core, a failed native assert, or
          // production printing its fail-closed marker and carrying on would be.
          world->restore(SearchWorld::kRoot);
          world->deal(mix(seed, i), assumption);
          if (plant == "segv")
            TestAccess::fault(*world, [] { *reinterpret_cast<volatile int *>(16) = 1; });
          else if (plant == "abort")
            TestAccess::fault(*world, [] { abort(); });
          else
            TestAccess::fault(*world, [] { fmt::println("Spec not found: planted"); });
          run.status[i] = 1;
          run.reason[i] = static_cast<uint8_t>(world->void_reason());
          ++run.voids[int(world->void_reason())];
          snprintf(run.sample, sizeof run.sample, "%s", world->last_diagnostic());
          continue;
        }
        play_void_iteration(*world, seed, assumption, run, i, cap);
      }
      world.reset();
      // The referee's true position must be back, and playable to its recorded end.
      try {
        for (size_t a = root.index; a < g.actions.size(); ++a) duel->play(g.actions[a]);
        if (!duel->done() || duel->winner() != g.winner || duel->win_reason() != g.reason) run.prefix_diagnostics = -1;
      } catch (const std::exception &) {
        run.prefix_diagnostics = -1;
      }
      return run;
    });
  };
  std::vector<int> all(seeds);
  std::iota(all.begin(), all.end(), 0);
  VoidRun mixed = mixed_run(all);
  // Clean run: fresh process, only the seeds that did not void, so nothing ever failed in it.
  std::vector<int> clean_order;
  for (int i = seeds - 1; i >= 0; --i)
    if (mixed.status[i] == 0) clean_order.push_back(i);
  VoidRun clean = mixed_run(clean_order);
  // Voided seeds again, alone: a void must reproduce, or it depended on something outside the world.
  std::vector<int> void_order;
  for (int i = 0; i < seeds; ++i)
    if (mixed.status[i] == 1) void_order.push_back(i);
  VoidRun again = mixed_run(void_order);
  long digest_bad = 0, status_bad = 0, reproduce_bad = 0;
  for (int i : clean_order) {
    status_bad += clean.status[i] != 0;
    digest_bad += clean.digest[i] != mixed.digest[i];
  }
  for (int i : void_order) reproduce_bad += again.status[i] != 1 || again.reason[i] != mixed.reason[i];
  for (char *c = mixed.sample; *c; ++c)
    if (*c == '"' || *c == '\n') *c = '\'';
  char buf[1200];
  snprintf(buf, sizeof buf,
           "{\"void_test\":\"%s\",\"mutant\":\"%s\",\"planted_card\":%u,\"game\":\"%s/%s seed=%u env=%d\",\"root_index\":%d,\"viewer\":%d,"
           "\"seeds\":%d,\"finished\":%ld,\"refused\":%ld,\"voids\":{\"lua_error\":%ld,\"environment_throw\":%ld,\"native_diagnostic\":%ld,"
           "\"fatal_signal\":%ld,\"hang\":%ld},\"clean_run_compared\":%zu,\"clean_status_differences\":%ld,\"clean_digest_differences\":%ld,"
           "\"voids_reproduced_alone\":%zu,\"void_reproduce_differences\":%ld,\"true_game_finished_after_search\":%d,\"sample\":\"%.80s\"}",
           plant.c_str(), arg("mutant", "none").c_str(), planted, g.deck0.c_str(), g.deck1.c_str(), g.seed, g.env, root.index, root.viewer, seeds,
           mixed.finished, mixed.refused, mixed.voids[int(VoidReason::kLuaError)], mixed.voids[int(VoidReason::kEnvironmentThrow)],
           mixed.voids[int(VoidReason::kNativeDiagnostic)], mixed.voids[int(VoidReason::kFatalSignal)], mixed.voids[int(VoidReason::kHang)],
           clean_order.size(), status_bad, digest_bad, void_order.size(), reproduce_bad, mixed.prefix_diagnostics == 0, mixed.sample);
  emit_kv(buf);
  return 0;
}

// -- Throughput of the search loop -------------------------------------------------------------------
// One worker process per thread, through the search API. A node is restore + deal + step + observe, and
// a playout is restore + deal, then observe and step to the end.
struct NodeOut {
  long roots, nodes, node_refused, node_terminal, node_void, playouts, playout_refused, playout_decisions, playout_void;
  double node_seconds, playout_seconds, root_seconds, restore_us, deal_us, step_us, observe_us;
  long parts;
};

NodeOut node_worker(const std::vector<Game> &games, const std::vector<Pos> &positions, int worker, double seconds, int per_root, int cap) {
  NodeOut o{};
  uint64_t x = 85100081 + worker * 1000003ULL;
  size_t cursor = splitmix(x) % positions.size();
  double start = now_us();
  while ((now_us() - start) / 1e6 < seconds) {
    const Pos &p = positions[cursor++ % positions.size()];
    const Game &g = games[p.game];
    double t0 = now_us();
    Referee duel(g.deck0, g.deck1, uint64_t(g.seed) + g.env);
    for (int i = 0; i < p.index; ++i) duel.play(g.actions[i]);
    int viewer = duel.to_play();
    DealAssumption assumption;
    assumption.opponent_main = ygopro::main_decks_.at(viewer == 0 ? g.deck1 : g.deck0);
    auto world = duel.open_search(viewer);
    o.root_seconds += (now_us() - t0) / 1e6;
    ++o.roots;
    t0 = now_us();
    for (int i = 0; i < per_root; ++i) {
      double a = now_us();
      world->restore(SearchWorld::kRoot);
      double b = now_us();
      DealResult deal = world->deal(splitmix(x), assumption);
      double c = now_us();
      if (!deal.ok) {
        ++o.node_refused;
        continue;
      }
      Status s = world->step(static_cast<int>(splitmix(x) % world->option_count()));
      double d = now_us();
      Observation obs;
      if (s == Status::kOk) s = world->observe(&obs);
      double e = now_us();
      ++o.nodes;
      o.restore_us += b - a;
      o.deal_us += c - b;
      o.step_us += d - c;
      o.observe_us += e - d;
      ++o.parts;
      o.node_terminal += s == Status::kTerminal;
      o.node_void += s == Status::kVoid;
    }
    o.node_seconds += (now_us() - t0) / 1e6;
    t0 = now_us();
    for (int i = 0; i < per_root / 8; ++i) {
      world->restore(SearchWorld::kRoot);
      if (!world->deal(splitmix(x), assumption).ok) {
        ++o.playout_refused;
        continue;
      }
      Status s = Status::kOk;
      int decisions = 0;
      while (s == Status::kOk && decisions < cap) {
        Observation obs;
        s = world->observe(&obs);
        if (s != Status::kOk) break;
        s = world->step(static_cast<int>(splitmix(x) % world->option_count()));
        ++decisions;
      }
      ++o.playouts;
      o.playout_decisions += decisions;
      o.playout_void += s == Status::kVoid;
    }
    o.playout_seconds += (now_us() - t0) / 1e6;
  }
  return o;
}

std::string read_loadavg() {
  std::ifstream in("/proc/loadavg");
  std::string a, b, c;
  in >> a >> b >> c;
  return a + " " + b + " " + c;
}

int cmd_node(const std::vector<Game> &games) {
  int workers = argi("workers", 1), per_root = argi("per-root", 64), cap = argi("cap", 3000);
  double seconds = std::stod(arg("seconds", "15"));
  auto positions = sample_positions_per_cell(games, argi("per-cell", 4), 85100083);
  for (const auto &g : games) (void)spec_for(g);
  std::string load_before = read_loadavg();
  std::vector<int> pipes;
  std::vector<pid_t> pids;
  for (int w = 0; w < workers; ++w) {
    int fds[2];
    if (pipe(fds)) return 1;
    pid_t pid = fork();
    if (pid == 0) {
      close(fds[0]);
      NodeOut o = node_worker(games, positions, w, seconds, per_root, cap);
      (void)!write(fds[1], &o, sizeof o);
      _exit(0);
    }
    close(fds[1]);
    pipes.push_back(fds[0]);
    pids.push_back(pid);
  }
  NodeOut t{};
  for (int w = 0; w < workers; ++w) {
    NodeOut o{};
    if (read(pipes[w], &o, sizeof o) != sizeof o) {
      fprintf(stderr, "node worker %d died\n", w);
      return 1;
    }
    waitpid(pids[w], nullptr, 0);
    t.roots += o.roots;
    t.nodes += o.nodes;
    t.node_refused += o.node_refused;
    t.node_terminal += o.node_terminal;
    t.node_void += o.node_void;
    t.playouts += o.playouts;
    t.playout_refused += o.playout_refused;
    t.playout_decisions += o.playout_decisions;
    t.playout_void += o.playout_void;
    t.node_seconds += o.node_seconds;
    t.playout_seconds += o.playout_seconds;
    t.root_seconds += o.root_seconds;
    t.restore_us += o.restore_us;
    t.deal_us += o.deal_us;
    t.step_us += o.step_us;
    t.observe_us += o.observe_us;
    t.parts += o.parts;
  }
  double parts = std::max(1L, t.parts);
  char buf[900];
  snprintf(buf, sizeof buf,
           "{\"node\":true,\"workers\":%d,\"per_root\":%d,\"seconds\":%.0f,\"loadavg_before\":\"%s\",\"loadavg_after\":\"%s\",\"roots\":%ld,"
           "\"root_ms_mean\":%.2f,\"nodes\":%ld,\"nodes_per_sec_per_worker\":%.0f,\"nodes_per_sec_total\":%.0f,\"node_us_mean\":%.1f,"
           "\"restore_us\":%.1f,\"deal_us\":%.1f,\"step_us\":%.1f,\"observe_us\":%.1f,\"deals_refused\":%ld,\"node_terminal\":%ld,\"node_void\":%ld,"
           "\"playouts\":%ld,\"playouts_per_sec_per_worker\":%.1f,\"playouts_per_sec_total\":%.1f,\"decisions_per_playout\":%.1f,"
           "\"playout_decisions_per_sec_per_worker\":%.0f,\"playout_void\":%ld}",
           workers, per_root, seconds, load_before.c_str(), read_loadavg().c_str(), t.roots, t.root_seconds * 1000 / std::max(1L, t.roots), t.nodes,
           t.nodes / std::max(1e-9, t.node_seconds), t.nodes / std::max(1e-9, t.node_seconds) * workers, t.node_seconds * 1e6 / std::max(1L, t.nodes),
           t.restore_us / parts, t.deal_us / parts, t.step_us / parts, t.observe_us / parts, t.node_refused, t.node_terminal, t.node_void, t.playouts,
           t.playouts / std::max(1e-9, t.playout_seconds), t.playouts / std::max(1e-9, t.playout_seconds) * workers,
           t.playout_decisions / double(std::max(1L, t.playouts)), t.playout_decisions / std::max(1e-9, t.playout_seconds), t.playout_void);
  emit_kv(buf);
  return 0;
}
}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: fm_bench <command> --root DIR --games FILE [--key value ...]\n");
    return 2;
  }
  {
    rlimit no_core{0, 0};  // A crash here maps a 2 GB arena, and dumping it takes tens of seconds.
    setrlimit(RLIMIT_CORE, &no_core);
  }
  std::string cmd = argv[1];
  g_encode = false;
  for (int i = 2; i + 1 < argc; i += 2) g_args[std::string(argv[i]).substr(2)] = argv[i + 1];
  std::string root = arg("root", ".");
  std::string out = arg("out", "");
  if (!out.empty()) g_out = fopen(out.c_str(), "a");
  g_encode = argi("encode", 0);
#ifdef FM_MUTANTS
  {
    std::string name = arg("mutant", "none");
    static const std::map<std::string, Mutant> kinds = {
        {"none", Mutant::None}, {"leak-hand", Mutant::LeakHand}, {"leak-seed", Mutant::LeakSeed}, {"leak-order", Mutant::LeakOrder},
        {"rollback-history", Mutant::RollbackHistory}, {"rollback-cursor", Mutant::RollbackCursor}, {"rollback-reveal", Mutant::RollbackReveal},
        {"rollback-bind", Mutant::RollbackBind}, {"rollback-rng", Mutant::RollbackRng}};
    g_mutant = kinds.at(name);
  }
#else
  if (arg("mutant", "none") != "none") {
    fprintf(stderr, "this build has no mutants: use fm_bench_mutant\n");
    return 2;
  }
#endif
  ForwardModelConfig config;
  config.root = root;
  config.registry = arg("registry", "");
  config.step_cpu_limit_ms = argi("step-cpu-limit-ms", 2000);
  init_forward_model(config);
  auto games = load_games(arg("games", root + "/build/forward_model/corpus/games.txt"));
  int limit = argi("limit-games", 0);
  if (limit) games.resize(std::min<size_t>(games.size(), limit));
  if (cmd == "verify") return cmd_verify(games);
  if (cmd == "replay") return cmd_replay(games);
  if (cmd == "fork") return cmd_fork(games);
  if (cmd == "forkmem") return cmd_forkmem(games);
  if (cmd == "arena") return cmd_arena(games);
  if (cmd == "throughput") return cmd_throughput(games);
  if (cmd == "fork-conc") return cmd_fork_conc(games);
  if (cmd == "correct") return cmd_correct(games);
  if (cmd == "redeal") {
    if (g_args.count("search-net")) install_search_pair(g_args);
    return cmd_redeal(games, g_args);
  }
  if (cmd == "construct") return cmd_construct(games, g_args);
  if (cmd == "uniform") return cmd_uniform(games, g_args);
  if (cmd == "deepdiff") return cmd_deepdiff(games);
  if (cmd == "statics") return cmd_statics(games);
  if (cmd == "void") return cmd_void(games);
  if (cmd == "node") return cmd_node(games);
  fprintf(stderr, "unknown command %s\n", cmd.c_str());
  return 2;
}
