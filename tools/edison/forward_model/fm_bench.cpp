// Forward-model spike: replay, fork and arena snapshots over real recorded duels.
// Usage: fm_bench <command> [--key value ...]   (see docs/edison-forward-model.md)
#include "fm_env.h"
#include "fm_hidden.h"

using namespace fm;

namespace {
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

// verify: replay each recorded game; check against the production trace and
// write per-game digests so separate processes and heaps can be compared.
int cmd_verify(const std::vector<Game> &games) {
  std::string heap = arg("heap", "glibc");
  bool deep = argi("deep", 1);
  bool hash = argi("hash", 1);
  std::string out = arg("chains", "");
  FILE *f = out.empty() ? nullptr : fopen(out.c_str(), "w");
  g_use_arena = heap == "arena";
  long mismatched = 0, decisions = 0;
  for (size_t gi = 0; gi < games.size(); ++gi) {
    const Game &g = games[gi];
    if (heap == "perturb") heap_scramble(gi * 7919 + 13);
    if (g_use_arena) arena_clear();
    (void)spec_for(g);
    FmEnv *e = new_env(g, env_seed_of(g));
    bool bad = false;
    RunResult r;
    {
      Scope s(g_use_arena);
      e->reset();
      if (hash) absorb(e, r, deep);
      for (size_t i = 0; i < g.actions.size(); ++i) {
        if (e->done() || e->to_play() != g.to_play[i] ||
            static_cast<int>(e->actions().size()) != g.options[i]) {
          bad = true;
          fprintf(stderr, "game %zu decision %zu: trace mismatch to_play %d/%d options %zu/%d\n",
                  gi, i, e->to_play(), g.to_play[i], e->actions().size(), g.options[i]);
          break;
        }
        e->act(g.actions[i]);
        ++r.decisions;
        if (hash) absorb(e, r, deep);
      }
      if (!bad && (!e->done() || e->winner() != g.winner || e->win_reason() != g.reason)) {
        bad = true;
        fprintf(stderr, "game %zu: result mismatch %d/%d %d/%d\n", gi, e->winner(), g.winner,
                e->win_reason(), g.reason);
      }
      finish(e, r);
    }
    decisions += r.decisions;
    mismatched += bad;
    if (f)
      fprintf(f, "%zu %d %016" PRIx64 " %016" PRIx64 " %d %d\n", gi, r.decisions,
              r.decision_digest, r.deep_digest, r.winner, r.reason);
    free_env(e);
  }
  if (f) fclose(f);
  char buf[256];
  snprintf(buf, sizeof buf,
           "{\"verify\":\"%s\",\"deep\":%d,\"games\":%zu,\"decisions\":%ld,\"trace_mismatches\":%ld,"
           "\"diagnostics\":%ld}",
           heap.c_str(), deep, games.size(), decisions, mismatched, g_diagnostics);
  emit_kv(buf);
  return mismatched != 0;
}

// obsdump: the exact observation bytes production hashes, for a check against the
// sha256 recorded in the production traces (see check_observations.py).
int cmd_obsdump(const std::vector<Game> &games) {
  FILE *f = fopen(arg("dump", "").c_str(), "wb");
  if (!f) return 1;
  for (const Game &g : games) {
    (void)spec_for(g);
    FmEnv *e = new_env(g, env_seed_of(g));
    e->reset();
    for (size_t i = 0; i < g.actions.size(); ++i) {
      e->encode();
      auto &st = *FmEnv::g_obs;
      auto put = [&](auto &a, size_t bytes) { fwrite(a.Data(), 1, bytes, f); };
      auto &cards = st["obs:cards_"_];
      auto &global = st["obs:global_"_];
      auto &decision = st["obs:decision_"_];
      auto &selection = st["obs:selection_"_];
      auto &acts = st["obs:actions_"_];
      put(cards, cards.size * cards.element_size);
      put(global, global.size * global.element_size);
      put(decision, decision.size * decision.element_size);
      put(selection, selection.size * selection.element_size);
      put(acts, size_t(g.options[i]) * acts.Shape(1) * acts.element_size);
      e->step(g.actions[i]);
    }
    free_env(e);
  }
  fclose(f);
  return 0;
}

// replay: the cost of recreating a position from seed plus action prefix.
int cmd_replay(const std::vector<Game> &games) {
  int reps = argi("reps", 3);
  std::string heap = arg("heap", "glibc");
  g_use_arena = heap == "arena";
  auto positions = sample_positions(games, 100000, 70000017);
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
  auto positions = sample_positions(games, per_bucket, 70000023);
  std::vector<double> bare[3], node[3], playout[3], play_dec[3], dirty_node[3], dirty_play[3],
      rss[3], self_step[3];
  uint64_t x = 70000031;
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
  auto positions = sample_positions(games, 1, 70000029);
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
  auto positions = sample_positions(games, per_bucket, 70000023);
  std::vector<double> size_kb[3], take[3], restore[3], node_full[3], step_only[3], node_tracked[3],
      dirty_node[3], restore_tracked[3], playout[3], play_dec[3], dirty_play[3], root_build[3],
      restore_play[3], node_tracked_after_play[3];
  uint64_t x = 70000031;
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
  auto positions = mixed_positions(games, 70000037);
  ThroughputOut out{};
  uint64_t x = 70000041 + worker * 1000003ULL;
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
  auto positions = mixed_positions(games, 70000043);
  uint64_t x = 70000047;
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
  uint64_t decision[kMaxOptions], deep[kMaxOptions];
  int decisions[kMaxOptions], winner[kMaxOptions], reason[kMaxOptions];
  long diagnostics;
};

uint64_t option_seed(const Pos &p, int option) {
  return mix(mix(mix(70000053, p.game), p.index), option);
}

struct CorrectOut {
  long branch_points, options, decisions;
  long replay_vs_replay_bad, fork_bad, arena_bad, arena_reverse_bad, tracked_bad, scrub_bad,
      perturb_bad, return_bad, diagnostics, truncated;
};

CorrectOut correct_worker(const std::vector<Game> &games, const std::vector<Pos> &positions,
                          int worker, int workers, int cap, FILE *log) {
  CorrectOut out{};
  track_install();
  for (size_t pi = worker; pi < positions.size(); pi += workers) {
    const Pos &p = positions[pi];
    const Game &g = games[p.game];
    auto mismatch = [&](const char *method, int option, const RunResult &a, uint64_t dec, uint64_t deep,
                        int winner, int reason) {
      bool bad = a.decision_digest != dec || a.deep_digest != deep || a.winner != winner ||
                 a.reason != reason;
      if (bad && log)
        fprintf(log, "MISMATCH %s game=%d index=%d option=%d decisions=%d winner %d/%d\n", method,
                p.game, p.index, option, a.decisions, a.winner, winner);
      return bad;
    };
    int n_options = g.options[p.index];
    // Reference: a straight uninterrupted run in a fresh process with no snapshots.
    auto straight = [&](int option) {
      RunResult r = in_child<RunResult>([&] {
        g_use_arena = false;
        FmEnv *e = build_position(g, p.index);
        e->act(option);
        RunResult run = run_random(e, option_seed(p, option), cap, true, true);
        run.diagnostics = g_diagnostics;
        return run;
      });
      return r;
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
      out.replay_vs_replay_bad += mismatch("replay-repeat", o, again, ref[o].decision_digest,
                                           ref[o].deep_digest, ref[o].winner, ref[o].reason);
    }
    // Replay in a deliberately scrambled heap: does any behavior follow addresses?
    for (int o = 0; o < std::min(n_options, 2); ++o) {
      RunResult perturbed = in_child<RunResult>([&] {
        g_use_arena = false;
        heap_scramble(mix(p.game, p.index));
        FmEnv *e = build_position(g, p.index);
        e->act(o);
        return run_random(e, option_seed(p, o), cap, true, true);
      });
      out.perturb_bad += mismatch("replay-perturbed-heap", o, perturbed, ref[o].decision_digest,
                                  ref[o].deep_digest, ref[o].winner, ref[o].reason);
    }
    // Fork: one holder process at the position; each option in a child of it.
    OptionDigests fork_digests = in_child<OptionDigests>([&] {
      g_use_arena = false;
      FmEnv *e = build_position(g, p.index);
      OptionDigests d{};
      d.n = n_options;
      for (int o = 0; o < n_options; ++o) {
        RunResult r = in_child<RunResult>([&] {
          e->act(o);
          return run_random(e, option_seed(p, o), cap, true, true);
        });
        d.decision[o] = r.decision_digest;
        d.deep[o] = r.deep_digest;
        d.winner[o] = r.winner;
        d.reason[o] = r.reason;
      }
      return d;
    });
    for (int o = 0; o < n_options; ++o) {
      if (fork_digests.decision[o] != ref[o].decision_digest ||
          fork_digests.deep[o] != ref[o].deep_digest || fork_digests.winner[o] != ref[o].winner ||
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
      snapshot_restore(snap);
      { Scope s(true); e->act(o); }
      RunResult r = run_random(e, option_seed(p, o), cap, true, true);
      out.arena_bad += mismatch("arena", o, r, ref[o].decision_digest, ref[o].deep_digest,
                                ref[o].winner, ref[o].reason);
    }
    for (int o = n_options - 1; o >= 0; --o) {
      snapshot_restore(snap);
      { Scope s(true); e->act(o); }
      RunResult r = run_random(e, option_seed(p, o), cap, true, true);
      out.arena_reverse_bad += mismatch("arena-reverse", o, r, ref[o].decision_digest,
                                        ref[o].deep_digest, ref[o].winner, ref[o].reason);
    }
    // Uninitialised-memory probe: distinct garbage above the allocator top.
    for (int o = 0; o < n_options; ++o) {
      snapshot_restore(snap);
      arena_scrub(static_cast<uint8_t>(0x11 * (o + 1)), 1 << 20);
      { Scope s(true); e->act(o); }
      RunResult r = run_random(e, option_seed(p, o), cap, true, true);
      out.scrub_bad += mismatch("arena-scrub", o, r, ref[o].decision_digest, ref[o].deep_digest,
                                ref[o].winner, ref[o].reason);
    }
    // Write-tracked restore across the same options.
    snapshot_restore(snap);
    for (int o = 0; o < n_options; ++o) {
      track_begin(snap);
      { Scope s(true); e->act(o); }
      RunResult r = run_random(e, option_seed(p, o), cap, true, true);
      track_restore(snap);
      track_end();
      out.tracked_bad += mismatch("arena-tracked", o, r, ref[o].decision_digest,
                                  ref[o].deep_digest, ref[o].winner, ref[o].reason);
    }
    // Return to the position and finish the real game as recorded.
    snapshot_restore(snap);
    RunResult rec = run_recorded(e, g, p.index, true);
    out.return_bad += mismatch("arena-return", -1, rec, ref_recorded.decision_digest,
                               ref_recorded.deep_digest, ref_recorded.winner, ref_recorded.reason);
    if (rec.winner != g.winner || rec.reason != g.reason) ++out.return_bad;
    out.diagnostics += g_diagnostics;
    g_diagnostics = 0;
    snapshot_restore(snap);
    free_env(e);
    g_use_arena = false;
    __libc_free(snap.data);
    out.branch_points++;
    if (log) fflush(log);
  }
  return out;
}

int cmd_correct(const std::vector<Game> &games) {
  int per_bucket = argi("positions", 110);
  int workers = argi("workers", 4);
  int cap = argi("cap", 3000);
  std::string log_path = arg("log", "");
  auto positions = sample_positions(games, per_bucket, 70000051);
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
    if (read(pipes[w], &o, sizeof o) != sizeof o) {
      fprintf(stderr, "correct worker %d died\n", w);
      return 1;
    }
    waitpid(pids[w], nullptr, 0);
    t.branch_points += o.branch_points;
    t.options += o.options;
    t.decisions += o.decisions;
    t.replay_vs_replay_bad += o.replay_vs_replay_bad;
    t.perturb_bad += o.perturb_bad;
    t.fork_bad += o.fork_bad;
    t.arena_bad += o.arena_bad;
    t.arena_reverse_bad += o.arena_reverse_bad;
    t.scrub_bad += o.scrub_bad;
    t.tracked_bad += o.tracked_bad;
    t.return_bad += o.return_bad;
    t.diagnostics += o.diagnostics;
    t.truncated += o.truncated;
  }
  char buf[600];
  snprintf(buf, sizeof buf,
           "{\"correct\":true,\"cap\":%d,\"branch_points\":%ld,\"options\":%ld,"
           "\"reference_decisions\":%ld,\"truncated_by_cap\":%ld,\"replay_repeat_bad\":%ld,"
           "\"replay_perturbed_heap_bad\":%ld,\"fork_bad\":%ld,\"arena_bad\":%ld,"
           "\"arena_reverse_bad\":%ld,\"arena_scrub_bad\":%ld,\"arena_tracked_bad\":%ld,"
           "\"arena_return_bad\":%ld,\"diagnostics\":%ld}",
           cap, t.branch_points, t.options, t.decisions, t.truncated, t.replay_vs_replay_bad,
           t.perturb_bad, t.fork_bad, t.arena_bad, t.arena_reverse_bad, t.scrub_bad, t.tracked_bad,
           t.return_bad, t.diagnostics);
  emit_kv(buf);
  return 0;
}
}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: fm_bench <command> --root DIR --games FILE [--key value ...]\n");
    return 2;
  }
  std::string cmd = argv[1];
  g_encode = false;
  for (int i = 2; i + 1 < argc; i += 2) g_args[std::string(argv[i]).substr(2)] = argv[i + 1];
  std::string root = arg("root", ".");
  std::string out = arg("out", "");
  if (!out.empty()) g_out = fopen(out.c_str(), "a");
  g_encode = argi("encode", 0);
  init_assets(root);
  auto games = load_games(arg("games", root + "/build/forward_model/games/games.txt"));
  int limit = argi("limit-games", 0);
  if (limit) games.resize(std::min<size_t>(games.size(), limit));
  if (cmd == "verify") return cmd_verify(games);
  if (cmd == "obsdump") return cmd_obsdump(games);
  if (cmd == "replay") return cmd_replay(games);
  if (cmd == "fork") return cmd_fork(games);
  if (cmd == "forkmem") return cmd_forkmem(games);
  if (cmd == "arena") return cmd_arena(games);
  if (cmd == "throughput") return cmd_throughput(games);
  if (cmd == "fork-conc") return cmd_fork_conc(games);
  if (cmd == "correct") return cmd_correct(games);
  if (cmd == "redeal") return cmd_redeal(games, g_args);
  if (cmd == "construct") return cmd_construct(games, g_args);
  fprintf(stderr, "unknown command %s\n", cmd.c_str());
  return 2;
}
