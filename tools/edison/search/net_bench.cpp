// Latency of edison_net.h on the real observation mix. Single thread, batch 1.
// Each decision is timed separately and passes run in a shuffled order, so the branch and
// cache state is not a replay of one fixed sequence. Emits JSON.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
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

double now_us() {
  return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void stats(FILE *o, const char *name, std::vector<double> &v, bool comma = true) {
  if (v.empty()) {
    std::fprintf(o, "\"%s\":{\"n\":0}%s", name, comma ? "," : "");
    return;
  }
  std::sort(v.begin(), v.end());
  auto q = [&](double p) { return v[std::min(v.size() - 1, size_t(p * double(v.size())))]; };
  double sum = 0;
  for (double x : v) sum += x;
  std::fprintf(o, "\"%s\":{\"n\":%zu,\"mean\":%.2f,\"p50\":%.2f,\"p90\":%.2f,\"p99\":%.2f,\"max\":%.2f}%s", name, v.size(), sum / double(v.size()), q(0.5), q(0.9), q(0.99), v.back(), comma ? "," : "");
}

constexpr int kBuckets[] = {16, 24, 32, 48, 72, 112, 160};
constexpr int kBucketCount = sizeof(kBuckets) / sizeof(kBuckets[0]);
int bucket_of(int rows) {
  for (int i = 0; i < kBucketCount; i++)
    if (rows <= kBuckets[i]) return i;
  return kBucketCount - 1;
}
}  // namespace

int main(int argc, char **argv) {
  if (argc < 6) {
    std::fprintf(stderr, "usage: net_bench net params_sha codes_sha decisions out.json [samples] [passes]\n");
    return 2;
  }
  const size_t want_samples = argc > 6 ? size_t(std::atol(argv[6])) : 20000;
  const int passes = argc > 7 ? std::atoi(argv[7]) : 3;
  const char *err = nullptr;
  ednet::Net net;
  ednet::Expect want{argv[2], argv[3], {509, 32, 64, 2, 4, 128}};
  if (!ednet::load(net, argv[1], want, &err)) {
    std::fprintf(stderr, "%s\n", err);
    return 2;
  }
  ednet::Scratch s;
  if (!ednet::create_scratch(s, net.config, &err)) return 2;

  FILE *f = std::fopen(argv[4], "rb");
  if (!f) return 2;
  std::fseek(f, 0, SEEK_END);
  const size_t total = size_t(std::ftell(f)) / sizeof(Record);
  std::rewind(f);
  std::vector<Record> all(total);
  if (std::fread(all.data(), sizeof(Record), total, f) != total) return 2;
  std::fclose(f);

  // A uniform sample of decisions keeps the real mix.
  std::mt19937_64 rng(20261105);
  std::vector<size_t> order(total);
  for (size_t i = 0; i < total; i++) order[i] = i;
  std::shuffle(order.begin(), order.end(), rng);
  order.resize(std::min(want_samples, total));

  std::vector<double> audit_us, norm_pack_us, pack_only_us, value_us, full_us, value_noaudit_total;
  std::vector<double> by_value[kBucketCount], by_full[kBucketCount], by_pack_value[kBucketCount];
  std::vector<double> by_options_full[5];
  volatile float sink = 0;
  size_t rows_sum = 0, options_sum = 0;
  for (int pass = 0; pass < passes; pass++) {
    std::shuffle(order.begin(), order.end(), rng);
    for (size_t index : order) {
      const Record &r = all[index];
      ednet::RawObs obs{&r.cards[0][0], &r.selection[0][0], &r.actions[0][0], &r.hist[0][0], r.decision, r.global, r.counts, r.legal, r.overflow};
      double t0 = now_us();
      int rule = ednet::audit(obs, net.config.n_codes, net.config.unknown_row != 0);
      double t1 = now_us();
      if (rule != 0) std::abort();
      rule = ednet::normalize_history_turn(obs.hist, obs.global, s.hist);
      double t2 = now_us();
      rule |= ednet::pack(obs, s);
      double t3 = now_us();
      if (rule != 0) std::abort();
      if (ednet::detail::encode(net, s) != ednet::kOk) std::abort();
      float value = ednet::detail::value_head(net, s);
      double t4 = now_us();
      sink = value;
      const int rows = s.n_rows, options = s.count;
      float full_value = 0;
      const float *logits = nullptr;
      double t5 = now_us();
      int status = ednet::evaluate_full(net, s, obs, false, &full_value, &logits);
      double t6 = now_us();
      if (status != ednet::kOk) std::abort();
      sink = logits[0];
      audit_us.push_back(t1 - t0);
      norm_pack_us.push_back(t3 - t1);
      pack_only_us.push_back(t3 - t2);
      value_us.push_back(t4 - t3);
      value_noaudit_total.push_back(t4 - t1);
      full_us.push_back(t6 - t5);
      const int b = bucket_of(rows);
      by_value[b].push_back(t4 - t3);
      by_pack_value[b].push_back(t4 - t1);
      by_full[b].push_back(t6 - t5);
      by_options_full[options <= 4 ? 0 : options <= 8 ? 1 : options <= 16 ? 2 : options <= 32 ? 3 : 4].push_back(t6 - t5);
      if (pass == 0) rows_sum += size_t(rows), options_sum += size_t(options);
    }
  }
  (void)sink;
  FILE *o = std::fopen(argv[5], "w");
  if (!o) return 2;
  std::fprintf(o, "{\"samples\":%zu,\"passes\":%d,\"mean_rows\":%.2f,\"mean_options\":%.2f,\"lanes\":%d,", order.size(), passes, double(rows_sum) / double(order.size()), double(options_sum) / double(order.size()), EDNET_LANES);
  std::fprintf(o, "\"all\":{");
  stats(o, "audit", audit_us);
  stats(o, "normalize_and_pack", norm_pack_us);
  stats(o, "pack_only", pack_only_us);
  stats(o, "value_forward", value_us);
  stats(o, "normalize_pack_value", value_noaudit_total);
  stats(o, "full_with_normalize_pack", full_us, false);
  std::fprintf(o, "},\"by_rows\":{");
  for (int i = 0; i < kBucketCount; i++) {
    std::fprintf(o, "\"rows<=%d\":{", kBuckets[i]);
    stats(o, "value_forward", by_value[i]);
    stats(o, "normalize_pack_value", by_pack_value[i]);
    stats(o, "full_with_normalize_pack", by_full[i], false);
    std::fprintf(o, "}%s", i + 1 < kBucketCount ? "," : "");
  }
  std::fprintf(o, "},\"by_options\":{");
  const char *labels[] = {"options<=4", "options<=8", "options<=16", "options<=32", "options>32"};
  for (int i = 0; i < 5; i++) {
    stats(o, labels[i], by_options_full[i], i < 4);
  }
  std::fprintf(o, "}}\n");
  std::fclose(o);
  return 0;
}
