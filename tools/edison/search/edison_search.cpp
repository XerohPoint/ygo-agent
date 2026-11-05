// Plays duels with the determinized search in pimc.h, and ranks the options of a recorded position.
// Includes only the search API and the network, so it cannot name the core or a hidden card.
//
//   edison_search play --root R --registry F --net NAME=PATH=PARAMS_SHA --codes-sha S --jobs F --out D --workers N
//   edison_search rank --root R --registry F --net NAME=PATH=PARAMS_SHA --codes-sha S --deck0 A --deck1 B --seed N --moves 0,1,0
//                      [--budget N --depth N --search-seed N --max-candidates N --assume-opponent DECK]
//
// The working directory must hold ./script. A job is one tab-separated line:
//   id deck0 deck1 seed_plus_env search_seat net budget depth max_candidates margin_z halving search_seed mode opponent_net
// search_seat -1 plays net in seat 0 and opponent_net in seat 1. Otherwise the search uses net, in its rollouts for both seats too,
// and the other seat plays opponent_net without search. Mode 0 plays the search's choice. Mode 1 runs the search and plays the
// network's choice anyway, which must not change the game.
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "pimc.h"

namespace {

struct NamedNet {
  ednet::Net net;
};

std::map<std::string, std::string> g_registry;
std::map<std::string, std::unique_ptr<NamedNet>> g_nets;

[[noreturn]] void die(const std::string &what) {
  fprintf(stderr, "edison_search: %s\n", what.c_str());
  exit(2);
}

std::vector<uint32_t> main_deck_of(const std::string &name) {
  auto path = g_registry.find(name);
  if (path == g_registry.end()) die("unknown deck " + name);
  std::ifstream in(path->second);
  if (!in) die("cannot read " + path->second);
  std::vector<uint32_t> codes;
  std::string line;
  bool in_main = false;
  while (std::getline(in, line)) {
    if (line.rfind("#main", 0) == 0) in_main = true;
    else if (!line.empty() && (line[0] == '#' || line[0] == '!')) in_main = false;
    else if (in_main && !line.empty() && line[0] >= '0' && line[0] <= '9') codes.push_back(static_cast<uint32_t>(std::stoul(line)));
  }
  if (codes.empty()) die("no Main Deck in " + path->second);
  return codes;
}

void load_registry(const std::string &path) {
  std::ifstream in(path);
  if (!in) die("cannot read " + path);
  std::string name, file;
  while (in >> name >> file) g_registry[name] = file;
}

void load_net(const std::string &spec, const std::string &codes_sha) {
  size_t a = spec.find('='), b = spec.rfind('=');
  if (a == std::string::npos || b == a) die("--net wants NAME=PATH=PARAMS_SHA");
  std::string name = spec.substr(0, a), path = spec.substr(a + 1, b - a - 1), params = spec.substr(b + 1);
  ednet::Expect expect{params.c_str(), codes_sha.c_str(), {509, 32, 64, 2, 4, 128}};
  auto holder = std::make_unique<NamedNet>();
  const char *err = nullptr;
  if (!ednet::load(holder->net, path.c_str(), expect, &err)) die("net " + name + ": " + err);
  g_nets[name] = std::move(holder);
}

uint32_t crc_step(uint32_t crc, const void *data, size_t size) {
  static uint32_t table[256];
  if (!table[1]) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
  }
  crc = ~crc;
  const auto *p = static_cast<const uint8_t *>(data);
  for (size_t i = 0; i < size; ++i) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
  return ~crc;
}

// Every field production's observation holds, in collect-side order. zlib.crc32 computes the same chain.
uint32_t chain_observation(uint32_t crc, const fm::Observation &o) {
  crc = crc_step(crc, o.cards, ednet::kMaxRows * ednet::kCardCols);
  crc = crc_step(crc, o.selection, ednet::kMaxRows * ednet::kSelectionCols);
  crc = crc_step(crc, o.actions, o.n_options * ednet::kActionCols);
  crc = crc_step(crc, o.history, ednet::kHistRows * ednet::kHistCols);
  crc = crc_step(crc, o.decision, ednet::kDecisionCols * 4);
  crc = crc_step(crc, o.global, ednet::kGlobalCols);
  return crc;
}

// As actor.py's RepeatGuard: the key is the acting seat's observation, and tried options are skipped.
struct RepeatGuard {
  std::map<std::string, std::set<int>> tried;
  long fires = 0;
  static std::string key_of(const fm::Observation &o) {
    std::string key;
    key.reserve(ednet::kMaxRows * (ednet::kCardCols + ednet::kSelectionCols) + 128 + o.n_options * ednet::kActionCols);
    key.append(reinterpret_cast<const char *>(o.cards), ednet::kMaxRows * ednet::kCardCols);
    key.append(reinterpret_cast<const char *>(o.selection), ednet::kMaxRows * ednet::kSelectionCols);
    key.append(reinterpret_cast<const char *>(o.global), ednet::kGlobalCols);
    key.append(reinterpret_cast<const char *>(o.decision), ednet::kDecisionCols * 4);
    key.append(reinterpret_cast<const char *>(o.actions), o.n_options * ednet::kActionCols);
    return key;
  }
  // The options the seat may play, or all of them if every one has been tried.
  std::vector<int> allowed(const std::string &key, int count) {
    auto &seen = tried[key];
    fires += !seen.empty();
    std::vector<int> out;
    for (int i = 0; i < count; ++i)
      if (!seen.count(i)) out.push_back(i);
    if (out.empty())
      for (int i = 0; i < count; ++i) out.push_back(i);
    return out;
  }
};

int best_allowed(const float *logits, const std::vector<int> &allowed) {
  int best = allowed.front();
  for (int option : allowed)
    if (logits[option] > logits[best]) best = option;
  return best;
}

struct Job {
  std::string id, deck0, deck1, net, opponent_net, text;
  uint64_t seed = 0, search_seed = 0;
  int search_seat = -1, mode = 0;
  pimc::Config config;
};

std::vector<Job> read_jobs(const std::string &path) {
  std::ifstream in(path);
  if (!in) die("cannot read " + path);
  std::vector<Job> jobs;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::istringstream row(line);
    Job j;
    int halving = 1;
    if (!(row >> j.id >> j.deck0 >> j.deck1 >> j.seed >> j.search_seat >> j.net >> j.config.budget >> j.config.depth >> j.config.max_candidates >>
          j.config.margin_z >> halving >> j.search_seed >> j.mode >> j.opponent_net))
      die("bad job: " + line);
    j.config.halving = halving != 0;
    j.config.seed = j.search_seed;
    j.text = line;
    std::replace(j.text.begin(), j.text.end(), '\t', ' ');
    if (!g_nets.count(j.net) || !g_nets.count(j.opponent_net)) die("job names an unloaded net: " + j.net + " or " + j.opponent_net);
    jobs.push_back(j);
  }
  return jobs;
}

void write_all(const std::string &path, const std::string &text) {
  std::string temporary = path + ".tmp";
  int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) die("cannot write " + temporary);
  size_t done = 0;
  while (done < text.size()) {
    ssize_t n = write(fd, text.data() + done, text.size() - done);
    if (n < 0) {
      if (errno == EINTR) continue;
      die("write failed");
    }
    done += n;
  }
  close(fd);
  rename(temporary.c_str(), path.c_str());
}

std::string json_text(const std::string &s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    if (static_cast<unsigned char>(c) >= 32) out += c;
  }
  return out + "\"";
}

// Names this executable in each game it writes, so a directory cannot resume under another build. Set before any fork.
std::string g_binary_id;

std::string binary_id() {
  std::ifstream in("/proc/self/exe", std::ios::binary);
  std::vector<char> buffer(1 << 20);
  uint64_t hash = 14695981039346656037ULL;
  while (in.read(buffer.data(), buffer.size()) || in.gcount() > 0) {
    for (std::streamsize i = 0; i < in.gcount(); ++i) hash = (hash ^ static_cast<unsigned char>(buffer[i])) * 1099511628211ULL;
    if (in.eof()) break;
  }
  char text[32];
  snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(hash));
  return text;
}

struct Totals {
  pimc::Counters c;
  long searched = 0, departed = 0, fell_back = 0, net_decisions = 0;
  long voids[static_cast<int>(fm::VoidReason::kCount)] = {};
  long refusals[static_cast<int>(fm::DealRefusal::kCount)] = {};
  void add_stats(const fm::WorldStats &stats) {
    for (int i = 0; i < static_cast<int>(fm::VoidReason::kCount); ++i) voids[i] += stats.voids[i];
    for (int i = 0; i < static_cast<int>(fm::DealRefusal::kCount); ++i) refusals[i] += stats.deals_refused[i];
  }
  void add(const pimc::Result &r) {
    c.deals_ok += r.counters.deals_ok, c.deals_refused += r.counters.deals_refused, c.rollouts += r.counters.rollouts;
    if (c.first_void.empty()) c.first_void = r.counters.first_void;
    c.worlds_void += r.counters.worlds_void, c.truncated += r.counters.truncated, c.net_errors += r.counters.net_errors;
    c.net_calls += r.counters.net_calls, c.forced_steps += r.counters.forced_steps, c.option_mismatch += r.counters.option_mismatch;
    c.step_overruns += r.counters.step_overruns;
    c.guard_fires += r.counters.guard_fires;
    ++searched;
    departed += r.chosen != r.greedy;
    fell_back += r.fell_back;
  }
};

double cpu_seconds() {
  timespec t{};
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

void play_one(const Job &job, const std::string &out_dir) {
  const ednet::Net &search_net = g_nets.at(job.net)->net;
  const ednet::Net *seat_net[2] = {&search_net, &g_nets.at(job.opponent_net)->net};
  if (job.search_seat == 1) std::swap(seat_net[0], seat_net[1]);
  ednet::Scratch scratch;
  const char *err = nullptr;
  if (!ednet::create_scratch(scratch, search_net.config, &err)) die(err);
  fm::DealAssumption assumption[2];
  assumption[0].opponent_main = main_deck_of(job.deck1);  // for the viewer in seat 0
  assumption[1].opponent_main = main_deck_of(job.deck0);

  std::string text = "{\"id\":" + json_text(job.id) + ",\"job\":" + json_text(job.text) + ",\"binary\":" + json_text(g_binary_id);
  std::string moves, seats, counts, log;
  uint32_t crc = 0;
  int decision = 0;
  Totals totals;
  RepeatGuard guards[2];
  long own_decisions[2] = {0, 0};  // a seat cannot count the other's private prompts, so its deal seeds come from these
  double search_seconds = 0;
  std::string status = "OK", reason;
  int winner = -1, win_reason = 0;
  try {
    fm::Referee duel(job.deck0, job.deck1, job.seed);
    while (!duel.done()) {
      int seat = duel.to_play();
      int count = duel.option_count();
      fm::Observation obs = duel.observe();
      crc = chain_observation(crc, obs);
      int choice = 0;
      if (count > 1) {
        ednet::RawObs raw = pimc::to_raw(obs);
        float value = 0;
        const float *logits = nullptr;
        int verdict = ednet::evaluate_full(*seat_net[seat], scratch, raw, true, &value, &logits);
        if (verdict != ednet::kOk) throw std::runtime_error("network refused the true observation: status " + std::to_string(verdict));
        ++totals.net_decisions;
        std::vector<float> root(logits, logits + count);
        std::string key = RepeatGuard::key_of(obs);
        std::vector<int> allowed = guards[seat].allowed(key, count);
        int plain = best_allowed(root.data(), allowed);
        choice = plain;
        if (seat == job.search_seat) {
          double started = cpu_seconds();
          auto world = duel.open_search(seat);
          if (!world) throw std::runtime_error("no search world at the acting seat");
          long key = own_decisions[seat];
#ifdef SEARCH_TEST_HOOKS
          if (pimc::g_mutant == "total-key") key = decision;
          if (pimc::g_mutant == "half-key") key = own_decisions[seat] + (decision - own_decisions[seat]) / 2;
#endif
          pimc::Result result = pimc::search(*world, search_net, scratch, root.data(), allowed, assumption[seat], job.config, static_cast<uint64_t>(key));
          totals.add_stats(world->stats());
          world.reset();
          search_seconds += cpu_seconds() - started;
          totals.add(result);
          if (job.mode == 0) choice = result.chosen;
          char row[200];
          const auto &best = result.options[result.chosen];
          const auto &base = result.options[result.greedy];
          snprintf(row, sizeof row, "%s[%d,%d,%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%.3f,%ld]", log.empty() ? "" : ",", decision, count, result.compared, result.greedy, result.chosen,
                   value, base.mean, best.mean, result.gain, result.gain_error, key);
          log += row;
        }
        guards[seat].tried[key].insert(choice);
      }
      seats += (moves.empty() ? "" : ",") + std::to_string(seat);
      counts += (moves.empty() ? "" : ",") + std::to_string(count);
      moves += (moves.empty() ? "" : ",") + std::to_string(choice);
      duel.play(choice);
      ++own_decisions[seat];
      ++decision;
    }
    winner = duel.winner();
    win_reason = duel.win_reason();
    if (duel.truncated()) {
      status = "VOID";
      reason = "decision limit";
    }
  } catch (const std::exception &ex) {
    status = "VOID";
    reason = ex.what();
  }
  char numbers[800];
  snprintf(numbers, sizeof numbers,
           ",\"status\":\"%s\",\"winner\":%d,\"win_reason\":%d,\"decisions\":%d,\"crc\":%u,\"search_seat\":%d,\"searched\":%ld,\"departed\":%ld,\"fell_back\":%ld,"
           "\"deals_ok\":%ld,\"deals_refused\":%ld,\"rollouts\":%ld,\"worlds_void\":%ld,\"truncated\":%ld,\"net_errors\":%ld,\"net_calls\":%ld,\"forced_steps\":%ld,"
           "\"option_mismatch\":%ld,\"step_overruns\":%ld,\"guard_fires\":[%ld,%ld],\"search_cpu\":%.3f,\"total_cpu\":%.3f",
           status.c_str(), winner, win_reason, decision, crc, job.search_seat, totals.searched, totals.departed, totals.fell_back, totals.c.deals_ok, totals.c.deals_refused,
           totals.c.rollouts, totals.c.worlds_void, totals.c.truncated, totals.c.net_errors, totals.c.net_calls, totals.c.forced_steps, totals.c.option_mismatch,
           totals.c.step_overruns, guards[0].fires, guards[1].fires, search_seconds, cpu_seconds());
  text += numbers;
  auto list = [](const long *values, int count) {
    std::string out = "[";
    for (int i = 0; i < count; ++i) out += (i ? "," : "") + std::to_string(values[i]);
    return out + "]";
  };
  text += ",\"rollout_guard_fires\":" + std::to_string(totals.c.guard_fires);
  text += ",\"void_reasons\":" + list(totals.voids, static_cast<int>(fm::VoidReason::kCount));
  text += ",\"refusal_reasons\":" + list(totals.refusals, static_cast<int>(fm::DealRefusal::kCount));
  text += ",\"first_void\":" + json_text(totals.c.first_void);
  text += ",\"reason\":" + json_text(reason) + ",\"moves\":[" + moves + "],\"seats\":[" + seats + "],\"counts\":[" + counts + "],\"log\":[" + log + "]}\n";
  write_all(out_dir + "/" + job.id + ".json", text);
}

int command_play(const std::string &jobs_path, const std::string &out_dir, int workers, int timeout_seconds) {
  std::vector<Job> jobs = read_jobs(jobs_path);
  mkdir(out_dir.c_str(), 0755);
  g_binary_id = binary_id();
  size_t next = 0;
  std::map<pid_t, std::pair<size_t, time_t>> running;
  long started = 0, failed = 0;
  auto path_of = [&](size_t i) { return out_dir + "/" + jobs[i].id + ".json"; };
  auto mark_void = [&](size_t i, const std::string &why) {
    ++failed;
    write_all(path_of(i), "{\"id\":" + json_text(jobs[i].id) + ",\"job\":" + json_text(jobs[i].text) + ",\"binary\":" + json_text(g_binary_id) + ",\"status\":\"VOID\",\"reason\":" + json_text(why) + "}\n");
  };
  while (next < jobs.size() || !running.empty()) {
    while (next < jobs.size() && static_cast<int>(running.size()) < workers) {
      size_t i = next++;
      if (access(path_of(i).c_str(), F_OK) == 0) {  // finished in an earlier run, which must have been this job and this build
        std::ifstream earlier(path_of(i));
        std::string found((std::istreambuf_iterator<char>(earlier)), std::istreambuf_iterator<char>());
        if (found.find("\"job\":" + json_text(jobs[i].text) + ",\"binary\":" + json_text(g_binary_id)) == std::string::npos) die(path_of(i) + " holds a different job or build");
        continue;
      }
      fflush(nullptr);
      pid_t pid = fork();
      if (pid < 0) die("fork failed");
      if (pid == 0) {
        play_one(jobs[i], out_dir);
        _exit(0);
      }
      running[pid] = {i, time(nullptr)};
      ++started;
    }
    int code = 0;
    pid_t done = waitpid(-1, &code, WNOHANG);
    if (done > 0) {
      auto it = running.find(done);
      if (it == running.end()) continue;
      size_t i = it->second.first;
      running.erase(it);
      if (!WIFEXITED(code) || WEXITSTATUS(code) != 0 || access(path_of(i).c_str(), F_OK) != 0)
        mark_void(i, "worker ended with status " + std::to_string(code));
      continue;
    }
    struct timespec nap{0, 20000000};
    nanosleep(&nap, nullptr);
    time_t now = time(nullptr);
    for (auto &entry : running)
      if (now - entry.second.second > timeout_seconds) kill(entry.first, SIGKILL);
  }
  printf("{\"jobs\":%zu,\"started\":%ld,\"worker_failures\":%ld}\n", jobs.size(), started, failed);
  return 0;
}

std::vector<int> parse_moves(const std::string &text) {
  std::vector<int> moves;
  std::istringstream in(text);
  std::string item;
  while (std::getline(in, item, ','))
    if (!item.empty()) moves.push_back(std::stoi(item));
  return moves;
}

std::string hex_row(const uint8_t *row, int size) {
  std::string out;
  char byte[3];
  for (int i = 0; i < size; ++i) {
    snprintf(byte, sizeof byte, "%02x", row[i]);
    out += byte;
  }
  return out;
}

int command_rank(std::map<std::string, std::string> &args) {
  if (g_nets.size() != 1) die("rank wants exactly one --net");
  const ednet::Net &net = g_nets.begin()->second->net;
  ednet::Scratch scratch;
  const char *err = nullptr;
  if (!ednet::create_scratch(scratch, net.config, &err)) die(err);
  std::string deck0 = args.at("deck0"), deck1 = args.at("deck1");
  fm::DealAssumption assumption[2];
  assumption[0].opponent_main = main_deck_of(deck1);
  assumption[1].opponent_main = main_deck_of(deck0);
  fm::Referee duel(deck0, deck1, std::strtoull(args.at("seed").c_str(), nullptr, 10));
  std::vector<int> moves = parse_moves(args["moves"]);
  // The guard's memory is part of what the network would be allowed to play, so rebuild it on the way.
  RepeatGuard guards[2];
  long own_decisions[2] = {0, 0};
  int decision = 0;
  for (int move : moves) {
    if (duel.done()) die("the game ended before the recorded moves did");
    if (move < 0 || move >= duel.option_count()) die("recorded move " + std::to_string(decision) + " is not an option");
    int seat = duel.to_play();
    if (duel.option_count() > 1) guards[seat].tried[RepeatGuard::key_of(duel.observe())].insert(move);
    duel.play(move);
    ++own_decisions[seat];
    ++decision;
  }
  if (duel.done()) die("the game is over at this position");
  int seat = duel.to_play();
  int count = duel.option_count();
  fm::Observation obs = duel.observe();
  ednet::RawObs raw = pimc::to_raw(obs);
  float value = 0;
  const float *logits = nullptr;
  int verdict = ednet::evaluate_full(net, scratch, raw, true, &value, &logits);
  if (verdict != ednet::kOk) die("network refused the observation: status " + std::to_string(verdict));
  std::vector<float> root(logits, logits + count);
  std::vector<std::string> rows;
  for (int i = 0; i < count; ++i) rows.push_back(hex_row(obs.actions + i * ednet::kActionCols, ednet::kActionCols));
  std::vector<int> allowed = guards[seat].allowed(RepeatGuard::key_of(obs), count);
#ifdef SEARCH_TEST_HOOKS
  if (args.count("replace-truth") && !fm::SearchTestAccess::replace_truth(duel, seat, std::strtoull(args["replace-truth"].c_str(), nullptr, 10), assumption[seat])) {
    printf("{\"status\":\"REFUSED\"}\n");
    return 0;
  }
  if (args.count("mutant")) fm::SearchTestAccess::set_mutant(args["mutant"]);
#endif
  if (args.count("assume-opponent")) assumption[seat].opponent_main = main_deck_of(args["assume-opponent"]);
  pimc::Config config;
  config.budget = args.count("budget") ? std::stoi(args["budget"]) : 512;
  config.depth = args.count("depth") ? std::stoi(args["depth"]) : 16;
  config.max_candidates = args.count("max-candidates") ? std::stoi(args["max-candidates"]) : 0;
  config.margin_z = 1.0;
  config.halving = false;
  config.seed = args.count("search-seed") ? std::strtoull(args["search-seed"].c_str(), nullptr, 10) : 1;
  config.keep_samples = args.count("samples") != 0;
  auto world = duel.open_search(seat);
  if (!world) die("no search world at the acting seat");
  pimc::Result result = pimc::search(*world, net, scratch, root.data(), allowed, assumption[seat], config, static_cast<uint64_t>(own_decisions[seat]));
  world.reset();
#ifdef SEARCH_TEST_HOOKS
  if (args["rank-mutant"] == "zero")
    for (auto &o : result.options) o.mean = 0, o.error = 0;
  if (args["rank-mutant"] == "blank")
    for (auto &o : result.options) {
      o.mean = 0, o.error = 0;
      for (double &v : o.samples) v = 0;
    }
  if (args["rank-mutant"] == "rotate" && count > 1) {  // every number is right, on the wrong option
    pimc::OptionEstimate first = result.options[0];
    for (int i = 0; i + 1 < count; ++i) {
      float logit = result.options[i].logit;
      result.options[i] = result.options[i + 1];
      result.options[i].logit = logit;
    }
    first.logit = result.options[count - 1].logit;
    result.options[count - 1] = first;
  }
#endif
  std::string text = "{\"status\":\"OK\",\"seat\":" + std::to_string(seat) + ",\"decision\":" + std::to_string(decision) + ",\"chosen\":" + std::to_string(result.chosen) +
                     ",\"greedy\":" + std::to_string(result.greedy) + ",\"fell_back\":" + (result.fell_back ? "true" : "false");
  char number[260];
  snprintf(number, sizeof number, ",\"network_value\":%.17g,\"gain\":%.17g,\"gain_error\":%.17g,\"paired\":%d", value, result.gain, result.gain_error, result.paired);
  text += number;
  const auto &c = result.counters;
  snprintf(number, sizeof number, ",\"deals_ok\":%ld,\"deals_refused\":%ld,\"rollouts\":%ld,\"worlds_void\":%ld,\"truncated\":%ld,\"net_errors\":%ld", c.deals_ok, c.deals_refused,
           c.rollouts, c.worlds_void, c.truncated, c.net_errors);
  text += number;
  text += ",\"options\":[";
  for (int i = 0; i < count; ++i) {
    const auto &o = result.options[i];
    snprintf(number, sizeof number, "{\"option\":%d,\"logit\":%.9g,\"deals\":%d,\"mean\":%.17g,\"error\":%.17g,\"row\":\"%s\"}", i, o.logit, o.deals, o.mean, o.error, rows[i].c_str());
    std::string item = number;
    if (config.keep_samples) {
      item.pop_back();
      item += ",\"samples\":[";
      for (size_t k = 0; k < o.samples.size(); ++k) {
        snprintf(number, sizeof number, "%s%.17g", k ? "," : "", o.samples[k]);
        item += number;
      }
      item += "]}";
    }
    text += (i ? "," : "") + item;
  }
  text += "]}\n";
  fputs(text.c_str(), stdout);
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) die("usage: edison_search play|rank --option value ...");
  std::string command = argv[1];
  std::map<std::string, std::string> args;
  std::vector<std::string> nets;
  for (int i = 2; i + 1 < argc; i += 2) {
    std::string key = argv[i];
    if (key.rfind("--", 0) != 0) die("bad argument " + key);
    if (key == "--net") nets.push_back(argv[i + 1]);
    else args[key.substr(2)] = argv[i + 1];
  }
  try {
    fm::ForwardModelConfig config;
    config.root = args.at("root");
    config.registry = args.at("registry");
    fm::init_forward_model(config);
    load_registry(args.at("registry"));
    for (const auto &spec : nets) load_net(spec, args.at("codes-sha"));
#ifdef SEARCH_TEST_HOOKS
    if (command == "play" && args.count("mutant")) pimc::g_mutant = args["mutant"];
#endif
    if (command == "play") return command_play(args.at("jobs"), args.at("out"), std::stoi(args.at("workers")), args.count("timeout") ? std::stoi(args["timeout"]) : 3600);
    if (command == "rank") return command_rank(args);
  } catch (const std::exception &ex) {
    die(ex.what());
  }
  die("unknown command " + command);
}
