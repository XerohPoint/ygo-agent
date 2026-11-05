// Proves the arena contract of edison_net.h against the real allocator in arena.h.
// Load and evaluation run with the arena active, and the test checks that they neither
// allocate from it nor write to it, and that a restore leaves weights and outputs intact.
#include "arena.h"
#include "edison_net.h"

#include <sys/wait.h>

#include <cinttypes>
#include <cstdio>
#include <vector>

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

int failures = 0;
void check(bool ok, const char *what) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

uint64_t arena_hash() {
  uint64_t h = 1469598103934665603ULL;
  const auto *p = reinterpret_cast<const uint8_t *>(fm::kArenaBase);
  for (size_t i = 0, n = fm::arena_used_pages() * fm::kPage; i < n; i++) h = (h ^ p[i]) * 1099511628211ULL;
  return h;
}

uint64_t bytes_hash(const void *data, size_t n) {
  uint64_t h = 1469598103934665603ULL;
  const auto *p = static_cast<const uint8_t *>(data);
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ULL;
  return h;
}

struct Outcome {
  float value = 0;
  uint64_t logits = 0;
  bool operator==(const Outcome &o) const { return std::memcmp(&value, &o.value, 4) == 0 && logits == o.logits; }
};

Outcome evaluate(const ednet::Net &net, ednet::Scratch &s, const Record &r) {
  ednet::RawObs obs{&r.cards[0][0], &r.selection[0][0], &r.actions[0][0], &r.hist[0][0], r.decision, r.global, r.counts, r.legal, r.overflow};
  Outcome out;
  const float *logits = nullptr;
  float only = 0;
  if (ednet::evaluate_full(net, s, obs, true, &out.value, &logits) != ednet::kOk) std::abort();
  if (ednet::evaluate_value(net, s, obs, true, &only) != ednet::kOk) std::abort();
  if (std::memcmp(&only, &out.value, 4) != 0) std::abort();
  out.logits = bytes_hash(logits, size_t(r.counts) * 4);
  return out;
}
}  // namespace

int main(int argc, char **argv) {
  if (argc != 5) {
    std::fprintf(stderr, "usage: net_arena_test net params_sha codes_sha decisions\n");
    return 2;
  }
  const size_t kSamples = 64;
  std::vector<Record> records(kSamples);  // Outside the arena: g_active is still false.
  {
    FILE *f = std::fopen(argv[4], "rb");
    if (!f || std::fread(records.data(), sizeof(Record), kSamples, f) != kSamples) {
      std::fprintf(stderr, "cannot read decisions\n");
      return 2;
    }
    std::fclose(f);
  }
  std::vector<Outcome> before(kSamples);

  // A heap-shaped arena, as a duel would leave it.
  fm::arena_clear();
  std::vector<void *> blocks;
  {
    fm::Scope active(true);
    for (int i = 0; i < 2000; i++) {
      void *p = std::malloc(size_t(16 + i % 700));
      std::memset(p, i & 255, size_t(16 + i % 700));
      if (i % 3 == 0) std::free(p);
    }
  }
  const auto *header = fm::header();

  // Probe: this harness must be able to see an allocation, or the checks below prove nothing.
  {
    uint64_t probe_top = header->top, probe_live = header->live_blocks;
    bool moved;
    {
      fm::Scope active(true);
      void *p = std::malloc(100);
      moved = header->top != probe_top || header->live_blocks != probe_live;
      std::free(p);
    }
    check(moved, "control: a malloc with the arena active moves the arena's counters");
  }

  uint64_t top = header->top, live = header->live_blocks, hash = arena_hash();
  const char *err = nullptr;
  ednet::Net net;
  ednet::Scratch scratch;
  ednet::Expect want{argv[2], argv[3], {509, 32, 64, 2, 4, 128}};
  {
    fm::Scope active(true);
    if (!ednet::load(net, argv[1], want, &err) || !ednet::create_scratch(scratch, net.config, &err)) {
      fm::Scope off(false);
      std::fprintf(stderr, "load failed: %s\n", err);
      return 2;
    }
  }
  check(header->top == top && header->live_blocks == live && arena_hash() == hash, "load and create_scratch with the arena active leave it byte-identical");
  check(!fm::in_arena(net.mapping) && !fm::in_arena(scratch.mapping), "weights and scratch are outside the arena");
  const uint64_t weights = bytes_hash(net.mapping, net.mapping_bytes);

  {
    fm::Scope active(true);
    for (size_t i = 0; i < kSamples; i++) before[i] = evaluate(net, scratch, records[i]);
  }
  check(header->top == top && header->live_blocks == live && arena_hash() == hash, "evaluation with the arena active leaves it byte-identical");

  // With the arena write-protected, any write would fault and be logged by arena.h's tracker.
  fm::Snapshot snap;
  fm::snapshot_take(snap);
  fm::track_install();
  fm::track_begin(snap);
  {
    fm::Scope active(true);
    for (size_t i = 0; i < kSamples; i++) evaluate(net, scratch, records[i]);
  }
  const size_t dirty_during_eval = fm::g_dirty_count;
  check(dirty_during_eval == 0, "evaluation under write tracking dirties no arena page");
  {
    // Probe: a real write does register.
    *reinterpret_cast<volatile uint8_t *>(fm::kArenaBase + fm::kPage) = 1;
    check(fm::g_dirty_count == 1, "control: a write into the arena registers as a dirty page");
    fm::track_restore(snap);
  }
  fm::track_end();

  // Trash the arena, restore it, and evaluate again. The outputs must be the same bits.
  std::memset(reinterpret_cast<void *>(fm::kArenaBase), 0x5A, snap.size);
  fm::snapshot_restore(snap);
  check(arena_hash() == hash, "snapshot restore brought the arena back");
  check(bytes_hash(net.mapping, net.mapping_bytes) == weights, "weights survive scrub and restore");
  bool same = true;
  {
    fm::Scope active(true);
    for (size_t i = 0; i < kSamples; i++) {
      Outcome again = evaluate(net, scratch, records[i]);
      if (!(again == before[i])) same = false;
    }
  }
  check(same, "outputs after restore are bit-identical");
  check(header->top == top && header->live_blocks == live, "allocation top and live blocks unchanged");

  // The allocator hands out the same addresses after evaluations, as a restored duel needs.
  void *first, *second;
  {
    fm::Scope active(true);
    first = std::malloc(4096);
  }
  fm::snapshot_restore(snap);
  {
    fm::Scope active(true);
    for (size_t i = 0; i < kSamples; i++) evaluate(net, scratch, records[i]);
    second = std::malloc(4096);
  }
  check(first == second, "next allocation address after restore and evaluation matches");

  // Weights are read-only: a stray write faults instead of corrupting.
  std::fflush(stdout);
  pid_t child = fork();
  if (child == 0) {
    *static_cast<volatile uint8_t *>(net.mapping) = 1;
    _exit(0);
  }
  int child_status = 0;
  waitpid(child, &child_status, 0);
  check(WIFSIGNALED(child_status) && WTERMSIG(child_status) == SIGSEGV, "a write to the weights faults");
  std::printf("failures=%d\n", failures);
  return failures ? 1 : 0;
}
