// Differential check of edison_net.h against files written by the Python side. Emits JSON.
// Compares verdicts, normalized history, packed bytes, value, logits and greedy choices.
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "edison_net.h"

namespace {
#pragma pack(push, 1)
struct Record {
  uint8_t cards[ednet::kMaxRows][ednet::kCardCols];
  uint8_t selection[ednet::kMaxRows][ednet::kSelectionCols];
  uint8_t actions[ednet::kMaxOptions][ednet::kActionCols];
  uint8_t hist[ednet::kHistRows][ednet::kHistCols];
  int32_t decision[ednet::kDecisionCols];
  uint8_t global[ednet::kGlobalCols];
  uint8_t pad;
  int32_t counts, legal, overflow, to_play, deck0, deck1, game, step;
};
#pragma pack(pop)
static_assert(sizeof(Record) == 10440);

[[noreturn]] void die(const std::string &why) {
  std::fprintf(stderr, "net_diff: %s\n", why.c_str());
  std::exit(2);
}

struct Reader {
  FILE *f = nullptr;
  explicit Reader(const std::string &path) {
    f = std::fopen(path.c_str(), "rb");
    if (!f) die("cannot open " + path);
  }
  void read(void *p, size_t n) {
    if (std::fread(p, 1, n, f) != n) die("short read");
  }
  bool at_end() {
    int c = std::fgetc(f);
    if (c == EOF) return true;
    std::ungetc(c, f);
    return false;
  }
};

struct Tail {
  size_t n = 0;
  double sum = 0, top = 0;
  std::vector<float> values;
  void add(float v) {
    n++;
    sum += v;
    top = std::max(top, double(v));
    values.push_back(v);
  }
  double quantile(double q) {
    if (values.empty()) return 0;
    size_t at = std::min(values.size() - 1, size_t(q * double(values.size())));
    std::nth_element(values.begin(), values.begin() + at, values.end());
    return values[at];
  }
  void json(FILE *o, const char *name) {
    std::fprintf(o, "\"%s\":{\"n\":%zu,\"max\":%.6g,\"mean\":%.6g,\"p50\":%.6g,\"p99\":%.6g,\"p99.9\":%.6g},", name, n, top,
                 n ? sum / double(n) : 0.0, quantile(0.5), quantile(0.99), quantile(0.999));
  }
};

ednet::Config parse_config(const char *text) {
  ednet::Config g{};
  if (std::sscanf(text, "%d,%d,%d,%d,%d,%d", &g.n_codes, &g.id_dim, &g.channels, &g.layers, &g.heads, &g.critic_hidden) != 6)
    die("bad --config");
  return g;
}

const char *arg_value(int &i, int argc, char **argv) {
  if (i + 1 >= argc) die(std::string("missing value for ") + argv[i]);
  return argv[++i];
}
}  // namespace

int main(int argc, char **argv) {
  std::string decisions_path, expect_path, reference_path, json_path, net_path, params_sha, codes_sha, dump_path;
  ednet::Config config{509, 32, 64, 2, 4, 128};
  long limit = 0;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--decisions") decisions_path = arg_value(i, argc, argv);
    else if (a == "--expect") expect_path = arg_value(i, argc, argv);
    else if (a == "--reference") reference_path = arg_value(i, argc, argv);
    else if (a == "--json") json_path = arg_value(i, argc, argv);
    else if (a == "--net") net_path = arg_value(i, argc, argv);
    else if (a == "--params-sha") params_sha = arg_value(i, argc, argv);
    else if (a == "--codes-sha") codes_sha = arg_value(i, argc, argv);
    else if (a == "--config") config = parse_config(arg_value(i, argc, argv));
    else if (a == "--dump") dump_path = arg_value(i, argc, argv);
    else if (a == "--limit") limit = std::atol(arg_value(i, argc, argv));
    else die("unknown argument " + a);
  }
  if (decisions_path.empty() || expect_path.empty() || net_path.empty()) die("need --decisions, --expect and --net");

  const char *err = nullptr;
  ednet::Net net;
  ednet::Expect want{params_sha.c_str(), codes_sha.c_str(), config};
  if (!ednet::load(net, net_path.c_str(), want, &err)) die(std::string("load refused: ") + err);
  ednet::Scratch scratch;
  if (!ednet::create_scratch(scratch, net.config, &err)) die(err);
  const int c = net.config.channels;

  Reader decisions(decisions_path), expect(expect_path);
  bool have_reference = !reference_path.empty();
  Reader *reference = have_reference ? new Reader(reference_path) : nullptr;

  FILE *dump = dump_path.empty() ? nullptr : std::fopen(dump_path.c_str(), "wb");
  size_t records = 0, verdict_mismatch = 0, verdict_rejected = 0, native_stricter = 0;
  size_t hist_mismatch = 0, pack_rows_mismatch = 0, pack_cards_mismatch = 0, pack_actions_mismatch = 0, pack_dec_mismatch = 0, pack_global_mismatch = 0;
  size_t eval_fail = 0, value_only_mismatch = 0, greedy_total = 0, nonfinite = 0;
  size_t greedy_same_bytes = 0;
  Tail value_err, value_only_err, logit_err, logit_scaled, state_err, row_err, decision_logit_err;
  std::vector<std::string> greedy_mismatch, verdict_examples;
  std::vector<long> verdict_counts(1000, 0);
  int max_rows = 0, max_options = 0;
  std::vector<uint8_t> want_cards, want_actions;
  std::vector<float> want_logits, want_state, want_rows;
  Record rec;
  while (!decisions.at_end() && (limit == 0 || long(records) < limit)) {
    decisions.read(&rec, sizeof(rec));
    int32_t want_verdict;
    expect.read(&want_verdict, 4);
    ednet::RawObs obs{&rec.cards[0][0], &rec.selection[0][0], &rec.actions[0][0], &rec.hist[0][0], rec.decision, rec.global, rec.counts, rec.legal, rec.overflow};

    int verdict = ednet::audit(obs, net.config.n_codes);
    if (verdict == ednet::kOk) verdict = ednet::normalize_history_turn(obs.hist, obs.global, scratch.hist);
    if (want_verdict >= 0 && want_verdict < 1000) verdict_counts[want_verdict]++;
    records++;
    if (verdict != want_verdict) {
      verdict_mismatch++;
      if (verdict_examples.size() < 20) {
        char line[160];
        std::snprintf(line, sizeof(line), "{\"record\":%zu,\"python\":%d,\"native\":%d}", records - 1, want_verdict, verdict);
        verdict_examples.push_back(line);
      }
    }
    if (want_verdict != 0) {
      verdict_rejected++;
      continue;
    }

    int32_t sizes[2];
    expect.read(sizes, 8);
    const int want_rows_n = sizes[0], want_count = sizes[1];
    uint8_t want_hist[ednet::kHistRows * ednet::kHistCols], want_global[ednet::kGlobalCols];
    int32_t want_decision[ednet::kDecisionCols];
    expect.read(want_hist, sizeof(want_hist));
    expect.read(want_global, sizeof(want_global));
    expect.read(want_decision, sizeof(want_decision));
    want_cards.resize(size_t(want_rows_n) * ednet::kPackedCols);
    want_actions.resize(size_t(want_count) * ednet::kActionCols);
    if (!want_cards.empty()) expect.read(want_cards.data(), want_cards.size());
    if (!want_actions.empty()) expect.read(want_actions.data(), want_actions.size());
    max_rows = std::max(max_rows, want_rows_n);
    max_options = std::max(max_options, want_count);

    // Python's reference entry is read before anything can skip this record.
    float want_value = 0, want_only = 0;
    int32_t ref_head[3] = {0, 0, 0};
    if (reference) {
      reference->read(ref_head, sizeof(ref_head));
      if (ref_head[0] != want_count) die("reference and expect disagree on the option count");
      reference->read(&want_value, 4);
      reference->read(&want_only, 4);
      want_logits.resize(size_t(want_count));
      if (want_count) reference->read(want_logits.data(), size_t(want_count) * 4);
      if (ref_head[2]) {
        want_state.resize(size_t(c));
        reference->read(want_state.data(), size_t(c) * 4);
        want_rows.resize(size_t(want_rows_n + 1) * c);
        reference->read(want_rows.data(), want_rows.size() * 4);
      }
    }

    if (verdict != ednet::kOk) {
      // Native refused what Python's audit let through. Nothing else to compare.
      continue;
    }
    if (std::memcmp(scratch.hist, want_hist, sizeof(want_hist)) != 0) hist_mismatch++;
    const int packed = ednet::pack(obs, scratch);
    if (packed != ednet::kOk) {
      if (obs.decision[3] < 0 && packed == ednet::kBadSource) native_stricter++;
      else eval_fail++;
      continue;
    }
    if (scratch.n_rows != want_rows_n || scratch.count != want_count) {
      pack_rows_mismatch++;
    } else {
      if (!want_cards.empty() && std::memcmp(scratch.packed_cards, want_cards.data(), want_cards.size()) != 0) pack_cards_mismatch++;
      if (!want_actions.empty() && std::memcmp(scratch.packed_actions, want_actions.data(), want_actions.size()) != 0) pack_actions_mismatch++;
    }
    if (std::memcmp(scratch.decision, want_decision, sizeof(want_decision)) != 0) pack_dec_mismatch++;
    if (std::memcmp(scratch.global, want_global, sizeof(want_global)) != 0) pack_global_mismatch++;
    if (!reference) continue;

    float value = 0, only = 0;
    const float *logits = nullptr;
    int status = ednet::evaluate_value(net, scratch, obs, false, &only);
    if (status != ednet::kOk) {
      eval_fail++;
      continue;
    }
    status = ednet::evaluate_full(net, scratch, obs, false, &value, &logits);
    if (status != ednet::kOk) {
      eval_fail++;
      continue;
    }
    if (std::memcmp(&value, &only, 4) != 0) value_only_mismatch++;
    if (dump) {
      // Native outputs per record: value, then logits. noise_floor.py reads these.
      std::fwrite(&value, 4, 1, dump);
      std::fwrite(logits, 4, size_t(want_count), dump);
    }
    if (!std::isfinite(value)) nonfinite++;
    value_err.add(std::fabs(value - want_value));
    value_only_err.add(std::fabs(only - want_only));
    float worst = 0;
    for (int i = 0; i < want_count; i++) {
      if (!std::isfinite(logits[i])) nonfinite++;
      const float e = std::fabs(logits[i] - want_logits[i]);
      logit_err.add(e);
      // Logits reach the thousands, where one float32 ulp is about 1e-4. Scale the error out.
      logit_scaled.add(e / std::max(std::fabs(want_logits[i]), 1.0f));
      worst = std::max(worst, e);
    }
    decision_logit_err.add(worst);
    if (ref_head[2]) {
      for (int i = 0; i < c; i++) state_err.add(std::fabs(scratch.state[i] - want_state[i]));
      if (scratch.n_rows == want_rows_n)
        for (size_t i = size_t(c); i < want_rows.size(); i++) row_err.add(std::fabs(scratch.x[i] - want_rows[i]));
    }
    const int best = ednet::greedy(logits, want_count);
    greedy_total++;
    if (best != ref_head[1]) {
      const int py = ref_head[1];
      const bool identical = std::memcmp(want_actions.data() + size_t(best) * ednet::kActionCols, want_actions.data() + size_t(py) * ednet::kActionCols, ednet::kActionCols) == 0;
      if (identical) greedy_same_bytes++;
      if (greedy_mismatch.size() < 2000) {
        char line[320];
        std::snprintf(line, sizeof(line),
                      "{\"record\":%zu,\"game\":%d,\"step\":%d,\"count\":%d,\"python\":%d,\"native\":%d,\"python_gap\":%.6g,\"native_gap\":%.6g,\"worst_logit_error\":%.6g,\"same_action_bytes\":%s}",
                      records - 1, rec.game, rec.step, want_count, py, best, double(want_logits[py]) - double(want_logits[best]),
                      double(logits[best]) - double(logits[py]), double(worst), identical ? "true" : "false");
        greedy_mismatch.push_back(line);
      }
    }
  }
  if (dump) std::fclose(dump);

  FILE *o = json_path.empty() ? stdout : std::fopen(json_path.c_str(), "w");
  if (!o) die("cannot write json");
  std::fprintf(o, "{\"records\":%zu,\"verdict_mismatch\":%zu,\"verdict_rejected\":%zu,\"native_stricter\":%zu,", records, verdict_mismatch, verdict_rejected, native_stricter);
  std::fprintf(o, "\"hist_mismatch\":%zu,\"pack_size_mismatch\":%zu,\"pack_cards_mismatch\":%zu,\"pack_actions_mismatch\":%zu,\"pack_decision_mismatch\":%zu,\"pack_global_mismatch\":%zu,",
               hist_mismatch, pack_rows_mismatch, pack_cards_mismatch, pack_actions_mismatch, pack_dec_mismatch, pack_global_mismatch);
  std::fprintf(o, "\"eval_fail\":%zu,\"nonfinite\":%zu,\"value_only_vs_full_bit_mismatch\":%zu,\"max_rows\":%d,\"max_options\":%d,", eval_fail, nonfinite, value_only_mismatch, max_rows, max_options);
  value_err.json(o, "value_error");
  value_only_err.json(o, "value_only_error");
  logit_err.json(o, "logit_error");
  logit_scaled.json(o, "logit_error_scaled");
  decision_logit_err.json(o, "worst_logit_error_per_decision");
  state_err.json(o, "state_error");
  row_err.json(o, "row_error");
  std::fprintf(o, "\"greedy_total\":%zu,\"greedy_mismatch_count\":%zu,\"greedy_mismatch_identical_action_bytes\":%zu,\"greedy_mismatch\":[", greedy_total, greedy_mismatch.size(), greedy_same_bytes);
  for (size_t i = 0; i < greedy_mismatch.size(); i++) std::fprintf(o, "%s%s", i ? "," : "", greedy_mismatch[i].c_str());
  std::fprintf(o, "],\"verdict_examples\":[");
  for (size_t i = 0; i < verdict_examples.size(); i++) std::fprintf(o, "%s%s", i ? "," : "", verdict_examples[i].c_str());
  std::fprintf(o, "],\"verdict_counts\":{");
  bool first = true;
  for (size_t i = 0; i < verdict_counts.size(); i++)
    if (verdict_counts[i]) {
      std::fprintf(o, "%s\"%zu\":%ld", first ? "" : ",", i, verdict_counts[i]);
      first = false;
    }
  std::fprintf(o, "}}\n");
  if (o != stdout) std::fclose(o);
  return 0;
}
