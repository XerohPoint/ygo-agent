// Determinized search over one decision: a fresh deal per iteration, the same deals for every option
// compared, a network rollout for both seats, and the value head at the leaf.
//
// This header sees only the search API (fm_world.h) and the network (edison_net.h), so what it decides
// from is the acting seat's own observation, deals made from public inputs and a seed, and what happens
// inside those deals. It holds no game loop. docs/edison-search.md says what it assumes.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "edison_net.h"
#include "fm_world.h"

namespace pimc {

struct Config {
  int budget = 128;        // rollouts per decision, across all options compared
  int depth = 16;          // network decisions per rollout before the value head. Forced steps are free
  int max_candidates = 6;  // options compared, best logits first. 0 compares all
  double margin_z = 1.0;   // leave the network's choice only if the paired gain exceeds this many errors
  bool halving = true;     // false gives every candidate the same number of deals
  bool audit = true;       // audit every observation in a sampled world
  bool keep_samples = false;  // keep each option's rollout results, so a ranking can show its working
  uint64_t seed = 0;
};

struct Counters {
  long deals_ok = 0, deals_refused = 0, rollouts = 0, worlds_void = 0, truncated = 0, net_errors = 0, net_calls = 0, forced_steps = 0,
       option_mismatch = 0, step_overruns = 0, guard_fires = 0;
  std::string first_void;  // what the first voided world reported, for the game record
};

struct OptionEstimate {
  float logit = 0;
  int deals = 0;  // finished rollouts behind this estimate. Zero for an option not compared
  double mean = 0, error = 0;
  bool finalist = false;
  std::vector<double> samples;  // only with Config::keep_samples
};

struct Result {
  int greedy = 0;    // the network's choice among the allowed options
  int chosen = 0;    // what the search plays
  int compared = 0;  // options the search compared
  bool fell_back = false;  // no usable worlds or too few paired deals, so the network's choice stands
  double gain = 0, gain_error = 0;
  int paired = 0;
  std::vector<OptionEstimate> options;  // one per root option, in option order
  Counters counters;
};

inline uint64_t mix(uint64_t a, uint64_t b) {
  uint64_t z = a + 0x9e3779b97f4a7c15ULL + (b * 0xbf58476d1ce4e5b9ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

inline ednet::RawObs to_raw(const fm::Observation &o) {
  return ednet::RawObs{o.cards, o.selection, o.actions, o.history, o.decision, o.global, o.n_options, o.n_options, 0};
}

// Greedy on logits. The forward model's option order is not fully canonical between worlds with one
// public history, so equal logits break on the option's own bytes, not its place in the list.
inline int canonical_greedy(const float *logits, const uint8_t *actions, int count) {
  int best = 0;
  for (int i = 1; i < count; ++i) {
    if (logits[i] > logits[best] ||
        (logits[i] == logits[best] && std::memcmp(actions + i * ednet::kActionCols, actions + best * ednet::kActionCols, ednet::kActionCols) < 0))
      best = i;
  }
  return best;
}

#ifdef SEARCH_TEST_HOOKS
inline std::string g_mutant;  // set by the checks in test_search.py, to show they fail on a broken search
#endif

// A hash of what actor.py's RepeatGuard keys on, the acting seat's observation without its history.
inline uint64_t guard_key(const fm::Observation &o) {
  uint64_t h = 0x243f6a8885a308d3ULL;
  auto add = [&h](const void *data, size_t size) {
    const auto *p = static_cast<const uint8_t *>(data);
    for (; size >= 8; size -= 8, p += 8) {
      uint64_t word;
      std::memcpy(&word, p, 8);
      h = mix(h, word);
    }
    uint64_t tail = size;
    for (size_t i = 0; i < size; ++i) tail = (tail << 8) | p[i];
    h = mix(h, tail);
  };
  add(o.cards, ednet::kMaxRows * ednet::kCardCols);
  add(o.selection, ednet::kMaxRows * ednet::kSelectionCols);
  add(o.global, ednet::kGlobalCols);
  add(o.decision, ednet::kDecisionCols * 4);
  add(o.actions, o.n_options * ednet::kActionCols);
  return h;
}

// The options each seat has tried at each observation, within one rollout. A real seat skips those
// unless it has tried them all, so a rollout that did not would loop where real play moves on.
struct RolloutGuard {
  std::vector<std::pair<uint64_t, int>> tried[2];
  bool has(int seat, uint64_t key, int option) const {
    for (const auto &entry : tried[seat])
      if (entry.first == key && entry.second == option) return true;
    return false;
  }
  void add(int seat, uint64_t key, int option) {
    if (!has(seat, key, option)) tried[seat].emplace_back(key, option);
  }
  // Greedy among the untried options, or among all of them if every one has been tried.
  int choose(int seat, uint64_t key, const float *logits, const uint8_t *actions, int count, bool *fired) const {
    int best = -1;
    bool any = false;
    for (int i = 0; i < count; ++i) {
      if (has(seat, key, i)) {
        any = true;
        continue;
      }
      if (best < 0 || logits[i] > logits[best] ||
          (logits[i] == logits[best] && std::memcmp(actions + i * ednet::kActionCols, actions + best * ednet::kActionCols, ednet::kActionCols) < 0))
        best = i;
    }
    *fired = any;
    return best >= 0 ? best : canonical_greedy(logits, actions, count);
  }
};

// Plays from the current world until its leaf. Returns false if the world produced no value.
inline bool rollout(fm::SearchWorld &world, const ednet::Net &net, ednet::Scratch &scratch, int viewer, const Config &config, Counters &counters,
                    RolloutGuard &guard, double *value) {
#ifdef SEARCH_TEST_HOOKS
  if (g_mutant == "no-rollouts") return false;
#endif
  constexpr int kMaxSteps = 4000;
  int left = config.depth;
  for (int steps = 0; steps < kMaxSteps; ++steps) {
    if (world.voided()) return false;
    if (world.terminal()) {
      if (world.truncated()) {
        ++counters.truncated;
        return false;
      }
      int winner = world.winner();
      *value = winner == viewer ? 1.0 : (winner == 1 - viewer ? -1.0 : 0.0);
#ifdef SEARCH_TEST_HOOKS
      if (g_mutant == "flip-terminal") *value = -*value;
#endif
      return true;
    }
    if (world.option_count() == 1) {
      ++counters.forced_steps;
      if (world.step(0) == fm::Status::kVoid) return false;
      continue;
    }
    fm::Observation obs;
    if (world.observe(&obs) != fm::Status::kOk) return false;
    ednet::RawObs raw = to_raw(obs);
    float head = 0;
    ++counters.net_calls;
    if (left == 0) {
      if (ednet::evaluate_value(net, scratch, raw, config.audit, &head) != ednet::kOk) {
        ++counters.net_errors;
        return false;
      }
      *value = obs.to_play == viewer ? head : -head;
#ifdef SEARCH_TEST_HOOKS
      if (g_mutant == "flip-leaf") *value = -*value;
#endif
      return true;
    }
    const float *logits = nullptr;
    if (ednet::evaluate_full(net, scratch, raw, config.audit, &head, &logits) != ednet::kOk) {
      ++counters.net_errors;
      return false;
    }
    --left;
    const uint64_t key = guard_key(obs);
    bool fired = false;
    int choice = guard.choose(obs.to_play, key, logits, obs.actions, obs.n_options, &fired);
#ifdef SEARCH_TEST_HOOKS
    if (g_mutant == "no-guard") choice = canonical_greedy(logits, obs.actions, obs.n_options), fired = false;
#endif
    counters.guard_fires += fired;
    guard.add(obs.to_play, key, choice);
    if (world.step(choice) == fm::Status::kVoid) return false;
  }
  ++counters.step_overruns;
  return false;
}

// One decision. root_logits are the network's on the viewer's observation, allowed the options it may
// play. The world must be at its undealt root, and is left at some root or dealt state: closing it puts
// the true position back. decision_key must come from what the viewer knows, such as how many decisions it has made. The duel's
// whole decision count will not do: it counts the opponent's private prompts.
inline Result search(fm::SearchWorld &world, const ednet::Net &net, ednet::Scratch &scratch, const float *root_logits, const std::vector<int> &allowed,
                     const fm::DealAssumption &assumption, const Config &config, uint64_t decision_key) {
  const int options = world.option_count();
  const int viewer = world.viewer();
  uint64_t root_key = 0;
  {
    fm::Observation root;
    if (world.observe(&root) == fm::Status::kOk) root_key = guard_key(root);
  }
  Result result;
  result.options.resize(options);
  for (int i = 0; i < options; ++i) result.options[i].logit = root_logits[i];
  std::vector<int> candidates = allowed;
  std::stable_sort(candidates.begin(), candidates.end(), [&](int a, int b) { return root_logits[a] > root_logits[b]; });
  if (config.max_candidates > 0 && static_cast<int>(candidates.size()) > config.max_candidates) candidates.resize(config.max_candidates);
  const int greedy = candidates.front();
  result.greedy = result.chosen = greedy;
  result.compared = static_cast<int>(candidates.size());
  if (candidates.size() < 2) return result;

  const double missing = std::numeric_limits<double>::quiet_NaN();
  std::vector<std::vector<double>> values(options);
  Counters &counters = result.counters;
  uint64_t attempt = 0;
  const uint64_t base = mix(config.seed, decision_key);
  const int max_attempts = 2 * config.budget + 8;

  // A world for each candidate on one seed, or false when no seed gives a world.
  auto add_deal = [&](const std::vector<int> &alive) {
    while (static_cast<int>(attempt) < max_attempts) {
      uint64_t seed = mix(base, attempt++);
      world.restore(fm::SearchWorld::kRoot);
      fm::DealResult dealt = world.deal(seed, assumption);
      if (!dealt.ok) {
        ++counters.deals_refused;
        if (world.voided()) ++counters.worlds_void;
        // Refusal usually belongs to the position. Treat it as no signal and stop early.
        if (counters.deals_ok == 0 && counters.deals_refused >= 8) return false;
        continue;
      }
      ++counters.deals_ok;
      bool first = true;
      for (int option : alive) {
        if (!first) {
          world.restore(fm::SearchWorld::kRoot);
          if (!world.deal(seed, assumption).ok) {
            ++counters.worlds_void;
            values[option].push_back(missing);
            continue;
          }
        }
        first = false;
        double value = missing;
        ++counters.rollouts;
        // The rollout starts knowing what this seat may no longer play here, and that it has just played this option.
        RolloutGuard guard;
        for (int i = 0; i < options; ++i)
          if (i == option || std::find(allowed.begin(), allowed.end(), i) == allowed.end()) guard.add(viewer, root_key, i);
        if (world.option_count() != options) {
          ++counters.option_mismatch;
        } else if (world.step(option) == fm::Status::kVoid || !rollout(world, net, scratch, viewer, config, counters, guard, &value)) {
          value = missing;
          if (world.voided()) {
            ++counters.worlds_void;
            if (counters.first_void.empty()) counters.first_void = world.last_diagnostic();
          }
        }
        values[option].push_back(value);
      }
      return true;
    }
    return false;
  };

  auto mean_of = [&](int option) {
    double total = 0;
    int count = 0;
    for (double v : values[option])
      if (!std::isnan(v)) total += v, ++count;
    return count ? total / count : -2.0;
  };

  std::vector<int> alive = candidates;
  int rounds = 1;
  if (config.halving)
    while ((1 << rounds) < static_cast<int>(alive.size())) ++rounds;
  bool worlds_left = true;
  while (worlds_left) {
    int deals = std::max(1, config.budget / rounds / static_cast<int>(alive.size()));
    for (int i = 0; i < deals && worlds_left; ++i) worlds_left = add_deal(alive);
    if (!config.halving || alive.size() <= 2) break;
    std::stable_sort(alive.begin(), alive.end(), [&](int a, int b) { return mean_of(a) > mean_of(b); });
    alive.resize(std::max<size_t>(2, (alive.size() + 1) / 2));
    if (std::find(alive.begin(), alive.end(), greedy) == alive.end()) alive.back() = greedy;
  }

  for (int option : candidates) {
    auto &estimate = result.options[option];
    double total = 0;
    int count = 0;
    for (double v : values[option])
      if (!std::isnan(v)) {
        total += v, ++count;
        if (config.keep_samples) estimate.samples.push_back(v);
      }
    estimate.deals = count;
    estimate.mean = count ? total / count : 0;
    double squares = 0;
    for (double v : values[option])
      if (!std::isnan(v)) squares += (v - estimate.mean) * (v - estimate.mean);
    estimate.error = count > 1 ? std::sqrt(squares / (count - 1) / count) : 0;
  }
  for (int option : alive) result.options[option].finalist = true;

  int best = greedy;
  double best_mean = mean_of(greedy);
  for (int option : alive) {
    double m = mean_of(option);
    if (m > best_mean) best = option, best_mean = m;
  }
  // Greedy's values line up with every finalist's, deal by deal: survivors share every deal.
  double total = 0, squares = 0;
  int paired = 0;
  size_t shared = std::min(values[best].size(), values[greedy].size());
  for (size_t i = 0; i < shared; ++i) {
    double a = values[best][i], b = values[greedy][i];
    if (std::isnan(a) || std::isnan(b)) continue;
    total += a - b, squares += (a - b) * (a - b), ++paired;
  }
  result.paired = paired;
  if (paired < 2) {
    result.fell_back = true;
    return result;
  }
  result.gain = total / paired;
  result.gain_error = std::sqrt(std::max(0.0, (squares - total * total / paired) / (paired - 1)) / paired);
  if (best != greedy && result.gain > 0 && result.gain > config.margin_z * result.gain_error) result.chosen = best;
  return result;
}

}  // namespace pimc
