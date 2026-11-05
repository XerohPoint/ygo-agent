// Native forward pass of the Edison network: a port of tools/edison/learner/net.py
// and of row_lookup, pack, normalize_history_turn and audit in obs.py.
// Dependency-free C++17, using GCC vector extensions. Tested with GCC 11 only. Build with
// -O3 -march=native and no -ffast-math: the float32 operations follow the JAX original.
//
// One decision at a time, from the raw observation arrays:
//
//   const char *err = nullptr;
//   ednet::Expect expect{params_sha256_hex, codes_sha256_hex, {509, 32, 64, 2, 4, 128}};
//   ednet::Net net;
//   if (!ednet::load(net, "gauntlet-1702.ednet", expect, &err)) die(err);
//   ednet::Scratch scratch;
//   if (!ednet::create_scratch(scratch, net.config, &err)) die(err);
//   ednet::RawObs obs{cards, selection, actions, hist, decision, global, n, legal, overflow};
//   float value;
//   int status = ednet::evaluate_value(net, scratch, obs, /*audit=*/false, &value);
//   const float *logits;   // valid until the next call that uses this scratch
//   status = ednet::evaluate_full(net, scratch, obs, /*audit=*/true, &value, &logits);
//
// A nonzero status means no output was produced and the decision cannot count.
// The audit is optional. The history rule, row range checks and embedding range
// checks on what a path reads always run, so an unaudited bad observation never
// reads outside a table. (Python returns NaN there.) Without the audit, the value
// path does not look at action fields, so bad ones can still return a value. Leave
// the audit on for any observation not fresh from the certified encoder.
//
// Arena and allocator contract (tools/edison/forward_model/arena.h):
//   - Nothing here calls malloc, new, stdio or any other interposed allocator, in
//     load, in create_scratch or per call. Weights and scratch come from direct
//     anonymous mmap, outside the arena, so load and create_scratch may run with
//     the arena active or inactive and before or after arena_map. Evaluation writes
//     only to the Scratch, the stack and its output pointers.
//   - A snapshot restore therefore does not touch weights or scratch, and evaluating
//     does not change the arena's bytes, allocation top or addresses.
//     search/net_arena_test.cpp proves this with arena.h's write tracking.
//   - Weights are read-only after load. A stray write faults instead of corrupting.
//   - Worker processes: load once before fork and the pages stay shared. Create one
//     Scratch per process or thread. A Net may be shared; a Scratch may not.
//   - Scratch holds no state between calls, except the outputs of the last call.
#pragma once
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

// Test-only fault injection for the negative controls. Zero in every real build.
#ifndef EDNET_FAULT
#define EDNET_FAULT 0
#endif

#if defined(__AVX512F__)
#define EDNET_LANES 16
#elif defined(__AVX__)
#define EDNET_LANES 8
#else
#define EDNET_LANES 4
#endif

namespace ednet {

inline constexpr int kFault = EDNET_FAULT;
enum Fault {
  kFaultNone = 0,
  kFaultGeluExact = 1,       // erf GELU instead of jax.nn.gelu's default tanh form
  kFaultLayerNormEps = 2,    // 1e-5 (the PyTorch default) instead of flax's 1e-6
  kFaultShuffleRemapped = 3, // reversed rows, references remapped consistently
  kFaultShuffleRaw = 4,      // reversed rows, references left as they were
  kFaultPackRefOffByOne = 5, // action reference one row off in pack
  kFaultForwardRefOffByOne = 6,  // pointed row one off in the forward pass only
  kFaultNoRowUnused = 7,     // pointer 0 reads the context token, not no_row
  kFaultUnknownBit = 8,      // thermometer has no unknown bit
  kFaultHintOffByOne = 9,    // hint index misses the +1
  kFaultPoolSum = 10,        // action pool is a sum, not the mean over the decision
  kFaultNoHistoryNorm = 11,  // history turn column left absolute
  kFaultSourceFlagMissing = 12,  // is-source flag not set in pack
  kFaultPackDropSelected = 13,   // selection_ rows not kept by row_lookup
  kFaultContextInPool = 14,  // state pool includes the context token
  kFaultAuditSkipFinish = 15,    // audit does not check finish and cancel flags
};

// Observation shapes. These are the env's arrays; only the whitelisted keys appear.
inline constexpr int kMaxRows = 160, kCardCols = 41, kSelectionCols = 3;
inline constexpr int kMaxOptions = 256, kActionCols = 12;
inline constexpr int kHistRows = 16, kHistCols = 14;
inline constexpr int kDecisionCols = 12, kGlobalCols = 23;
inline constexpr int kPackedCols = 45;
inline constexpr int kHintMin = 500, kHintMax = 575, kHints = kHintMax - kHintMin + 2;
inline constexpr int kBins = 32, kThermometer = kBins + 1;
inline constexpr int kMaxLayers = 8;

// Status codes. 0 is success. Audit rules keep obs.py's check order, and the
// differential compares them with the exact message Python raised.
enum Status : int {
  kOk = 0,
  kAuditOptions = 1,       // action overflow or option-count mismatch
  kAuditVisibleId = 2,     // visible id beyond the code list
  kAuditOwnHand = 3,       // own hand identity missing
  kAuditHiddenStats = 4,   // hidden statistics leaked
  kAuditOfferedData = 7,   // data beyond offered actions
  kAuditActionId = 8,      // action id beyond the code list
  kAuditRowRef = 9,        // action reference past the last row: IndexError in Python
  kAuditActionCard = 10,   // action/card identity mismatch
  kAuditSchema = 11,       // unknown decision schema
  kAuditSelCounters = 12,  // selection counters
  kAuditSelFlags = 13,     // selection flags
  kAuditSelPadding = 14,   // selection points to padding
  kAuditStale = 15,        // stale selection context
  kAuditHint = 16,         // private hint payload
  kAuditStage = 17,        // unknown stage
  kAuditSelBounds = 18,    // selection bounds
  kAuditSourceRef = 19,    // source past the last row: IndexError in Python
  kAuditSourcePrivate = 20,  // source reveals a private row
  kAuditCandidates = 21,   // selection candidates disagree with offered actions
  kAuditFinishCancel = 22, // finish/cancel flags
  kAuditHistId = 25,       // history id beyond the code list
  kHistoryTurn = 26,       // history turn offset out of range
  kAuditCardColumn = 100,    // + column
  kAuditActionColumn = 200,  // + column
  kAuditGlobalColumn = 300,  // + column
  kAuditHistColumn = 400,    // + column
  kBadRowRef = 500,    // pack: an offered action refers past the last row
  kBadSource = 501,    // pack: decision source outside 0..160
  kBadOptions = 502,   // pack: option count outside 0..256
  kEmbedRange = 503,   // forward: an embedded column or id is outside its table
};

struct Config {
  int n_codes, id_dim, channels, layers, heads, critic_hidden;
};

struct Expect {
  const char *params_sha256;  // hex of the source msgpack. Required.
  const char *codes_sha256;   // hex of the code list. Required.
  Config config;
};

struct RawObs {
  const uint8_t *cards;      // kMaxRows x kCardCols
  const uint8_t *selection;  // kMaxRows x kSelectionCols
  const uint8_t *actions;    // kMaxOptions x kActionCols
  const uint8_t *hist;       // kHistRows x kHistCols
  const int32_t *decision;   // kDecisionCols
  const uint8_t *global;     // kGlobalCols
  int32_t num_options, legal_options;
  int32_t overflow;
};

// ---------------------------------------------------------------------------
// Vector primitives. GCC vector extensions select the width from the build flags.
namespace detail {
typedef float vf __attribute__((vector_size(EDNET_LANES * 4)));
typedef int32_t vi __attribute__((vector_size(EDNET_LANES * 4)));

#define EDNET_INLINE inline __attribute__((always_inline))

// Unaligned, may-alias access. GCC 11 can split a memcpy of a zmm into two ymm moves, and
// reading that back through the stack costs a store-forward stall on every load.
typedef float vf_unaligned __attribute__((vector_size(EDNET_LANES * 4), aligned(4), may_alias));
EDNET_INLINE vf load(const float *p) { return *reinterpret_cast<const vf_unaligned *>(p); }
EDNET_INLINE void store(float *p, vf v) { *reinterpret_cast<vf_unaligned *>(p) = v; }
EDNET_INLINE vf broadcast(float a) { return vf{} + a; }
EDNET_INLINE float hsum(vf v) {
  float s = 0;
  for (int i = 0; i < EDNET_LANES; i++) s += v[i];
  return s;
}
EDNET_INLINE float hmax(vf v) {
  float s = v[0];
  for (int i = 1; i < EDNET_LANES; i++) s = v[i] > s ? v[i] : s;
  return s;
}

// The core product: out[o] += sum_i x[i] * m[i * stride + o]. Accumulators stay in registers
// across the inputs. Updating `out` in memory per input forces a store-forward on each one,
// and that made the whole forward pass measure over twice as slow.
template <int V>
EDNET_INLINE void accumulate_block(float *out, const float *x, int in, const float *m, size_t stride) {
  vf acc[V];
  for (int v = 0; v < V; v++) acc[v] = load(out + v * EDNET_LANES);
  for (int i = 0; i < in; i++) {
    const vf a = broadcast(x[i]);
    const float *row = m + size_t(i) * stride;
    for (int v = 0; v < V; v++) acc[v] += a * load(row + v * EDNET_LANES);
  }
  for (int v = 0; v < V; v++) store(out + v * EDNET_LANES, acc[v]);
}

inline void accumulate(float *out, const float *x, int in, const float *m, size_t stride, int width) {
  int o = 0;
  for (; o + 4 * EDNET_LANES <= width; o += 4 * EDNET_LANES) accumulate_block<4>(out + o, x, in, m + o, stride);
  if (o + 2 * EDNET_LANES <= width) {
    accumulate_block<2>(out + o, x, in, m + o, stride);
    o += 2 * EDNET_LANES;
  }
  if (o + EDNET_LANES <= width) {
    accumulate_block<1>(out + o, x, in, m + o, stride);
    o += EDNET_LANES;
  }
  for (; o < width; o++)
    for (int i = 0; i < in; i++) out[o] += x[i] * m[size_t(i) * stride + o];
}

// The same, skipping inputs that are zero. For rows, where most of the input is empty bins.
template <int V>
EDNET_INLINE void accumulate_listed(float *out, const int *index, const float *value, int n, const float *m, size_t stride) {
  vf acc[V];
  for (int v = 0; v < V; v++) acc[v] = load(out + v * EDNET_LANES);
  for (int i = 0; i < n; i++) {
    const vf a = broadcast(value[i]);
    const float *row = m + size_t(index[i]) * stride;
    for (int v = 0; v < V; v++) acc[v] += a * load(row + v * EDNET_LANES);
  }
  for (int v = 0; v < V; v++) store(out + v * EDNET_LANES, acc[v]);
}

EDNET_INLINE void add_to(float *y, const float *x, int n) {
  int i = 0;
  for (; i + EDNET_LANES <= n; i += EDNET_LANES) store(y + i, load(y + i) + load(x + i));
  for (; i < n; i++) y[i] += x[i];
}

EDNET_INLINE float dot(const float *a, const float *b, int n) {
  vf s{};
  int i = 0;
  for (; i + EDNET_LANES <= n; i += EDNET_LANES) s += load(a + i) * load(b + i);
  float t = hsum(s);
  for (; i < n; i++) t += a[i] * b[i];
  return t;
}

// XLA's EmitFastTanh float32 rational approximation, with its clamp.
EDNET_INLINE vf tanh_v(vf x) {
#ifdef __FMA__
  const vf limit = broadcast(7.99881172180175781f);  // With FMA, the result here is exactly 1.
#else
  const vf limit = broadcast(7.90531110763549805f);
#endif
  vf c = x > limit ? limit : x;
  c = c < -limit ? -limit : c;
  vf z = c * c;
  vf p = broadcast(-2.76076847742355e-16f);
  p = p * z + 2.00018790482477e-13f;
  p = p * z + -8.60467152213735e-11f;
  p = p * z + 5.12229709037114e-08f;
  p = p * z + 1.48572235717979e-05f;
  p = p * z + 6.37261928875436e-04f;
  p = p * z + 4.89352455891786e-03f;
  p = p * c;
  vf q = broadcast(1.19825839466702e-06f);
  q = q * z + 1.18534705686654e-04f;
  q = q * z + 2.26843463243900e-03f;
  q = q * z + 4.89352518554385e-03f;
  vf r = p / q;
  vf abs = x < 0.0f ? -x : x;
  return abs < 0.0004f ? x : r;
}

inline float tanh_s(float x) {
  float in[EDNET_LANES] = {x};
  vf r = tanh_v(load(in));
  return r[0];
}

// exp(x) for x <= 0. Anything under -60 is exactly 0: it is under one ulp of a softmax
// sum that holds a 1, and this keeps denormals out of the attention products.
EDNET_INLINE vf exp_nonpositive(vf x) {
  vi under = x < -60.0f;
  x = under ? broadcast(-60.0f) : x;
  vf t = x * 1.44269504088896341f - 0.5f;
  vi n = __builtin_convertvector(t, vi);
  vf k = __builtin_convertvector(n, vf);
  vf r = x - k * 0.693359375f;
  r = r - k * -2.12194440e-4f;
  vf p = broadcast(1.9875691500E-4f);
  p = p * r + 1.3981999507E-3f;
  p = p * r + 8.3334519073E-3f;
  p = p * r + 4.1665795894E-2f;
  p = p * r + 1.6666665459E-1f;
  p = p * r + 5.0000001201E-1f;
  vf y = p * r * r + r + 1.0f;
  vi bits = (n + 127) << 23;
  return under ? vf{} : y * (vf)bits;
}

// jax.nn.gelu with its default approximate=True, as float32.
EDNET_INLINE vf gelu_v(vf x) {
  const float sqrt_2_over_pi = 0.797884560802865f;
  vf cube = x * x * x;
  vf cdf = (tanh_v((x + cube * 0.044715f) * sqrt_2_over_pi) + 1.0f) * 0.5f;
  return x * cdf;
}

inline void gelu(float *p, int n) {
  if constexpr (kFault == kFaultGeluExact) {
    for (int i = 0; i < n; i++) p[i] = p[i] * (std::erf(p[i] / 1.41421356237f) + 1.0f) / 2.0f;
    return;
  }
  int i = 0;
  for (; i + EDNET_LANES <= n; i += EDNET_LANES) store(p + i, gelu_v(load(p + i)));
  if (i < n) {
    float tail[EDNET_LANES] = {};
    std::memcpy(tail, p + i, sizeof(float) * (n - i));
    store(tail, gelu_v(load(tail)));
    std::memcpy(p + i, tail, sizeof(float) * (n - i));
  }
}

inline void sha256_block(uint32_t *h, const uint8_t *b) {
  static const uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  auto rot = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = uint32_t(b[i * 4]) << 24 | uint32_t(b[i * 4 + 1]) << 16 | uint32_t(b[i * 4 + 2]) << 8 | b[i * 4 + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = rot(w[i - 15], 7) ^ rot(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = rot(w[i - 2], 17) ^ rot(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t v[8];
  std::memcpy(v, h, sizeof(v));
  for (int i = 0; i < 64; i++) {
    uint32_t s1 = rot(v[4], 6) ^ rot(v[4], 11) ^ rot(v[4], 25);
    uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
    uint32_t t1 = v[7] + s1 + ch + K[i] + w[i];
    uint32_t s0 = rot(v[0], 2) ^ rot(v[0], 13) ^ rot(v[0], 22);
    uint32_t mj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
    uint32_t t2 = s0 + mj;
    v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
    v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
  }
  for (int i = 0; i < 8; i++) h[i] += v[i];
}

inline void sha256(const uint8_t *data, size_t n, uint8_t out[32]) {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  size_t at = 0;
  for (; at + 64 <= n; at += 64) sha256_block(h, data + at);
  uint8_t last[128] = {};
  size_t rest = n - at;
  std::memcpy(last, data + at, rest);
  last[rest] = 0x80;
  size_t blocks = rest + 9 <= 64 ? 1 : 2;
  uint64_t bits = uint64_t(n) * 8;
  for (int i = 0; i < 8; i++) last[blocks * 64 - 1 - i] = uint8_t(bits >> (8 * i));
  for (size_t i = 0; i < blocks; i++) sha256_block(h, last + i * 64);
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 4; j++) out[i * 4 + j] = uint8_t(h[i] >> (24 - j * 8));
}

inline bool hex_matches(const char *hex, const uint8_t bytes[32]) {
  if (hex == nullptr) return false;
  auto nibble = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
  for (int i = 0; i < 32; i++) {
    int a = nibble(hex[i * 2]);
    if (a < 0) return false;
    int b = nibble(hex[i * 2 + 1]);
    if (b < 0 || ((a << 4) | b) != bytes[i]) return false;
  }
  return hex[64] == 0;
}
}  // namespace detail

// ---------------------------------------------------------------------------
// The file written by export_net.py. Little-endian.
inline constexpr char kMagic[8] = {'E', 'D', 'N', 'E', 'T', '0', '1', '\n'};
inline constexpr uint32_t kFormatVersion = 1;

struct FileHeader {
  char magic[8];
  uint32_t format_version, header_bytes;  // header_bytes covers this struct and the table
  uint32_t n_codes, id_dim, channels, layers, heads, critic_hidden;
  uint32_t max_rows, max_options, hint_min, hint_max, bins, n_entries;
  uint64_t payload_bytes;
  uint8_t params_sha256[32];  // sha256 of the source .msgpack
  uint8_t codes_sha256[32];   // sha256 of the code list it was trained on
  uint8_t payload_sha256[32];
  uint8_t header_sha256[32];  // of the header and table, with this field zeroed
};
static_assert(sizeof(FileHeader) == 200);

struct FileEntry {
  char name[72];  // NUL-padded
  uint32_t rank;
  uint32_t dims[3];
  uint64_t offset, count;  // from the start of the payload, in bytes and in floats
};
static_assert(sizeof(FileEntry) == 104);

struct Embed {
  const float *table = nullptr;
  int n = 0, dim = 0;
};
struct Dense {
  const float *kernel = nullptr, *bias = nullptr;  // bias is null where use_bias=False
  int in = 0, out = 0;
};
struct Norm {
  const float *scale = nullptr, *bias = nullptr;
};
struct Block {
  Norm norm_attn, norm_mlp;
  Dense qkv, attn_out, mlp_in, mlp_out;
};
struct ActionEmbeds {
  Embed msg, act, finish, effect, phase, position, number, place, attrib;
};

struct Net {
  Config config{};
  void *mapping = nullptr;
  size_t mapping_bytes = 0;
  const float *bin_points = nullptr, *bin_widths = nullptr, *no_row = nullptr;
  Embed card_id;
  Embed loc, seq, owner, pos, overlay, attr, race, level, counter, negated;
  Embed turn, phase, first, my_turn, hint, stage, h_turns, h_phase;
  ActionEmbeds hist_fields, action_fields;
  Dense row_in, row_out, hist_in, token_in, token_out, state_in, state_out;
  Block blocks[kMaxLayers];
  Norm final_norm;
  Dense action_in, action_pool, action_state, logit_hidden, logit_out, value_hidden, value_out;
};

namespace detail {
// One expected array. The loader demands that a file's table is exactly this set.
struct ParamSpec {
  char name[72];
  int rank, d0, d1;
  const float **slot;
};

struct SpecList {
  ParamSpec items[160];
  int count = 0;
  bool overflow = false;

  void add(const char *a, const char *b, const char *c, int rank, int d0, int d1, const float **slot) {
    if (count == 160) {
      overflow = true;
      return;
    }
    ParamSpec &s = items[count++];
    size_t at = 0;
    for (const char *part : {a, b, c})
      for (const char *q = part; *q; q++)
        if (at < sizeof(s.name) - 1) s.name[at++] = *q;
    std::memset(s.name + at, 0, sizeof(s.name) - at);
    s.rank = rank, s.d0 = d0, s.d1 = d1, s.slot = slot;
  }
  void embed(const char *prefix, const char *name, Embed &e, int n, int dim) {
    e.n = n, e.dim = dim;
    add(prefix, name, "/embedding", 2, n, dim, &e.table);
  }
  void dense(const char *prefix, const char *name, Dense &d, int in, int out, bool bias = true) {
    d.in = in, d.out = out;
    add(prefix, name, "/kernel", 2, in, out, &d.kernel);
    if (bias) add(prefix, name, "/bias", 1, out, 0, &d.bias);
  }
  void norm(const char *prefix, const char *name, Norm &n, int c) {
    add(prefix, name, "/scale", 1, c, 0, &n.scale);
    add(prefix, name, "/bias", 1, c, 0, &n.bias);
  }
};

inline void describe_params(Net &net, SpecList &s) {
  const Config &g = net.config;
  const int c = g.channels;
  const char *E = "Encoder_0/";
  s.add("const/bin_points", "", "", 1, kBins, 0, &net.bin_points);
  s.add("const/bin_widths", "", "", 1, kBins, 0, &net.bin_widths);
  s.add("card_id", "", "", 2, g.n_codes + 1, g.id_dim, &net.card_id.table);
  net.card_id.n = g.n_codes + 1, net.card_id.dim = g.id_dim;
  s.add(E, "no_row", "", 1, c, 0, &net.no_row);

  s.embed(E, "loc", net.loc, 9, 6);
  s.embed(E, "seq", net.seq, 80, 6);
  s.embed(E, "owner", net.owner, 2, 2);
  s.embed(E, "pos", net.pos, 9, 4);
  s.embed(E, "overlay", net.overlay, 2, 2);
  s.embed(E, "attr", net.attr, 8, 4);
  s.embed(E, "race", net.race, 27, 4);
  s.embed(E, "level", net.level, 14, 4);
  s.embed(E, "counter", net.counter, 16, 4);
  s.embed(E, "negated", net.negated, 2, 2);
  s.embed(E, "turn", net.turn, 17, 6);
  s.embed(E, "phase", net.phase, 11, 6);
  s.embed(E, "first", net.first, 2, 2);
  s.embed(E, "my_turn", net.my_turn, 2, 2);
  s.embed(E, "hint", net.hint, kHints, 12);
  s.embed(E, "stage", net.stage, 4, 4);
  s.embed(E, "h_turns", net.h_turns, 17, 4);
  s.embed(E, "h_phase", net.h_phase, 11, 4);

  auto fields = [&](const char *prefix, const char *p, ActionEmbeds &a) {
    char name[32];
    auto nm = [&](const char *f) {
      size_t i = 0;
      for (const char *q = p; *q; q++) name[i++] = *q;
      for (const char *q = f; *q; q++) name[i++] = *q;
      name[i] = 0;
      return name;
    };
    s.embed(prefix, nm("msg"), a.msg, 17, 8);
    s.embed(prefix, nm("act"), a.act, 10, 8);
    s.embed(prefix, nm("finish"), a.finish, 2, 2);
    s.embed(prefix, nm("effect"), a.effect, 256, 16);
    s.embed(prefix, nm("phase"), a.phase, 4, 4);
    s.embed(prefix, nm("position"), a.position, 9, 4);
    s.embed(prefix, nm("number"), a.number, 14, 4);
    s.embed(prefix, nm("place"), a.place, 32, 8);
    s.embed(prefix, nm("attrib"), a.attrib, 10, 4);
  };
  fields(E, "hist_", net.hist_fields);
  fields("", "action_", net.action_fields);

  // Flax numbers a module when it is constructed, so in nn.Dense(c)(gelu(nn.Dense(c)(x)))
  // the outer Dense takes the lower number. The shapes in net.py's order:
  const int row_inputs = g.id_dim + 38 + 2 * kThermometer + 25 + 4;
  const int hist_inputs = 58 + g.id_dim + 8;
  const int context = 2 * kThermometer + 16 + 14 + 16 + 9;
  s.dense(E, "Dense_0", net.row_out, c, c);
  s.dense(E, "Dense_1", net.row_in, row_inputs, c);
  s.dense(E, "Dense_2", net.hist_in, hist_inputs, c);
  s.dense(E, "Dense_3", net.token_out, c, c);
  s.dense(E, "Dense_4", net.token_in, context + c, c);
  s.dense(E, "Dense_5", net.state_out, 2 * c, c);
  s.dense(E, "Dense_6", net.state_in, 3 * c, 2 * c);
  s.norm(E, "LayerNorm_0", net.final_norm, c);

  for (int i = 0; i < g.layers; i++) {
    char p[32] = "Encoder_0/Block_0/";
    p[16] = char('0' + i);
    Block &b = net.blocks[i];
    s.norm(p, "LayerNorm_0", b.norm_attn, c);
    s.norm(p, "LayerNorm_1", b.norm_mlp, c);
    s.dense(p, "Attention_0/Dense_0", b.qkv, c, 3 * c, false);
    s.dense(p, "Attention_0/Dense_1", b.attn_out, c, c, false);
    s.dense(p, "Dense_0", b.mlp_in, c, 2 * c);
    s.dense(p, "Dense_1", b.mlp_out, 2 * c, c);
  }

  s.dense("", "action_in", net.action_in, 58 + g.id_dim + c, c);
  s.dense("", "action_pool", net.action_pool, c, c);
  s.dense("", "action_state", net.action_state, c, c);
  s.dense("", "logit_hidden", net.logit_hidden, 4 * c, c);
  s.dense("", "logit_out", net.logit_out, c, 1);
  s.dense("", "value_hidden", net.value_hidden, c, g.critic_hidden);
  s.dense("", "value_out", net.value_out, g.critic_hidden, 1);
}

inline bool fail(const char **err, const char *why) {
  if (err) *err = why;
  return false;
}

inline bool valid_config(const Config &g) {
  return g.n_codes > 0 && g.n_codes < 65536 && g.id_dim > 0 && g.channels > 0 && g.layers > 0 &&
         g.layers <= kMaxLayers && g.layers < 10 && g.heads > 0 && g.channels % g.heads == 0 && g.critic_hidden > 0;
}
}  // namespace detail

inline void unload(Net &net) {
  if (net.mapping) munmap(net.mapping, net.mapping_bytes);
  net = Net{};
}

// Read a file written by export_net.py. Fails closed: wrong magic, version, size, hash,
// config, table entry or compiled-in constant all refuse, and `net` stays empty.
inline bool load(Net &net, const char *path, const Expect &expect, const char **err) {
  using detail::fail;
  net = Net{};
  if (!detail::valid_config(expect.config)) return fail(err, "expected config is invalid");
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return fail(err, "cannot open net file");
  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size < off_t(sizeof(FileHeader))) {
    close(fd);
    return fail(err, "net file shorter than its header");
  }
  size_t size = size_t(st.st_size);
  size_t map_bytes = (size + 4095) & ~size_t(4095);
  void *map = mmap(nullptr, map_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (map == MAP_FAILED) {
    close(fd);
    return fail(err, "cannot map net memory");
  }
  auto reject = [&](const char *why) {
    munmap(map, map_bytes);
    return fail(err, why);
  };
  size_t got = 0;
  while (got < size) {
    ssize_t n = read(fd, static_cast<uint8_t *>(map) + got, size - got);
    if (n <= 0) break;
    got += size_t(n);
  }
  close(fd);
  if (got != size) return reject("short read of net file");
  auto *bytes = static_cast<uint8_t *>(map);
  auto *h = static_cast<FileHeader *>(map);
  if (std::memcmp(h->magic, kMagic, 8) != 0) return reject("bad magic");
  if (h->format_version != kFormatVersion) return reject("unknown format version");
  if (h->max_rows != kMaxRows || h->max_options != kMaxOptions || h->hint_min != kHintMin ||
      h->hint_max != kHintMax || h->bins != kBins)
    return reject("file constants differ from this build");
  if (h->n_entries > 160 || h->header_bytes % 64 != 0 ||
      uint64_t(h->header_bytes) < sizeof(FileHeader) + uint64_t(h->n_entries) * sizeof(FileEntry) ||
      uint64_t(h->header_bytes) + h->payload_bytes != uint64_t(size))
    return reject("file size disagrees with its header");
  uint8_t stored[32], digest[32];
  std::memcpy(stored, h->header_sha256, 32);
  std::memset(h->header_sha256, 0, 32);
  detail::sha256(bytes, h->header_bytes, digest);
  std::memcpy(h->header_sha256, stored, 32);
  if (std::memcmp(digest, stored, 32) != 0) return reject("header hash mismatch");
  detail::sha256(bytes + h->header_bytes, h->payload_bytes, digest);
  if (std::memcmp(digest, h->payload_sha256, 32) != 0) return reject("payload hash mismatch");
  if (!detail::hex_matches(expect.params_sha256, h->params_sha256)) return reject("params_sha256 mismatch");
  if (!detail::hex_matches(expect.codes_sha256, h->codes_sha256)) return reject("codes_sha256 mismatch");
  const Config &g = expect.config;
  if (h->n_codes != uint32_t(g.n_codes) || h->id_dim != uint32_t(g.id_dim) || h->channels != uint32_t(g.channels) ||
      h->layers != uint32_t(g.layers) || h->heads != uint32_t(g.heads) || h->critic_hidden != uint32_t(g.critic_hidden))
    return reject("network config mismatch");

  Net built;
  built.config = g;
  built.mapping = map;
  built.mapping_bytes = map_bytes;
  detail::SpecList specs;
  detail::describe_params(built, specs);
  if (specs.overflow || uint32_t(specs.count) != h->n_entries) return reject("parameter count mismatch");
  auto *table = reinterpret_cast<const FileEntry *>(bytes + sizeof(FileHeader));
  const float *payload = reinterpret_cast<const float *>(bytes + h->header_bytes);
  uint64_t next_offset = 0;
  for (uint32_t i = 0; i < h->n_entries; i++) {
    const FileEntry &e = table[i];
    if (e.name[sizeof(e.name) - 1] != 0) return reject("unterminated parameter name");
    detail::ParamSpec *spec = nullptr;
    for (int j = 0; j < specs.count; j++)
      if (std::memcmp(specs.items[j].name, e.name, sizeof(e.name)) == 0) spec = &specs.items[j];
    if (!spec) return reject("unknown parameter in file");
    if (*spec->slot != nullptr) return reject("duplicate parameter in file");
    uint64_t expected = spec->rank == 1 ? uint64_t(spec->d0) : uint64_t(spec->d0) * spec->d1;
    if (e.rank != uint32_t(spec->rank) || e.dims[0] != uint32_t(spec->d0) ||
        e.dims[1] != uint32_t(spec->rank == 1 ? 0 : spec->d1) || e.dims[2] != 0 || e.count != expected)
      return reject("parameter shape mismatch");
    if (e.offset != next_offset) return reject("parameter table is not contiguous");
    next_offset += (e.count * sizeof(float) + 63) & ~uint64_t(63);
    if (next_offset > h->payload_bytes) return reject("parameter outside the payload");
    *spec->slot = payload + e.offset / sizeof(float);
  }
  for (int j = 0; j < specs.count; j++)
    if (*specs.items[j].slot == nullptr) return reject("parameter missing from file");
  if (next_offset != h->payload_bytes) return reject("payload longer than its parameters");
  if (mprotect(map, map_bytes, PROT_READ) != 0) return reject("cannot protect weights");
  net = built;
  return true;
}

// ---------------------------------------------------------------------------
// One per process or thread. All from one anonymous mapping, outside any arena.
struct Scratch {
  void *mapping = nullptr;
  size_t mapping_bytes = 0;
  // Packed observation, as obs.pack returns it for a batch of one.
  uint8_t *packed_cards = nullptr;    // n_rows x kPackedCols
  uint8_t *packed_actions = nullptr;  // count x kActionCols
  uint8_t *hist = nullptr;            // kHistRows x kHistCols, after normalization
  int32_t *decision = nullptr;
  uint8_t *global = nullptr;
  int n_rows = 0, count = 0;
  // Forward pass.
  float *x = nullptr;       // (kMaxRows + 1) x channels. x[0] is the context token
  float *norm = nullptr;    // same shape
  float *wide = nullptr;    // (kMaxRows + 1) x 3 channels
  float *key = nullptr;     // head size x (kMaxRows + 1), one column per key dimension
  float *score = nullptr;   // kMaxRows + 1, padded
  float *mixed = nullptr;   // (kMaxRows + 1) x channels
  float *state = nullptr;   // channels, the encoder's state output
  float *input = nullptr;   // the longest concatenated input
  float *hidden = nullptr;  // the longest hidden vector
  float *total = nullptr;   // channels, running action sum
  float *action = nullptr;  // kMaxOptions x channels
  float *logits = nullptr;  // kMaxOptions
};

inline void destroy_scratch(Scratch &s) {
  if (s.mapping) munmap(s.mapping, s.mapping_bytes);
  s = Scratch{};
}

inline bool create_scratch(Scratch &s, const Config &g, const char **err) {
  s = Scratch{};
  if (!detail::valid_config(g)) return detail::fail(err, "scratch config is invalid");
  const size_t c = size_t(g.channels), rows = size_t(kMaxRows) + 1;
  const size_t widest = size_t(c) * 4 + size_t(g.id_dim) + 256 + size_t(g.critic_hidden) * 2 + EDNET_LANES;
  auto fbytes = [](size_t floats) { return (floats * sizeof(float) + 63) & ~size_t(63); };
  size_t head = c / size_t(g.heads);
  const size_t sizes[] = {fbytes(rows * c), fbytes(rows * c), fbytes(rows * 3 * c), fbytes(head * (rows + EDNET_LANES)),
                          fbytes(rows + EDNET_LANES), fbytes(rows * c), fbytes(c), fbytes(widest), fbytes(widest),
                          fbytes(c), fbytes(size_t(kMaxOptions) * c), fbytes(size_t(kMaxOptions))};
  size_t floats_at = 0;
  for (size_t n : sizes) floats_at += n;
  const size_t byte_part = (size_t(kMaxRows) * kPackedCols + size_t(kMaxOptions) * kActionCols +
                            size_t(kHistRows) * kHistCols + kDecisionCols * sizeof(int32_t) + kGlobalCols + 4095) & ~size_t(4095);
  const size_t total = (floats_at + byte_part + 4095) & ~size_t(4095);
  void *map = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (map == MAP_FAILED) return detail::fail(err, "cannot map scratch memory");
  auto *at = static_cast<uint8_t *>(map);
  float **slots[] = {&s.x, &s.norm, &s.wide, &s.key, &s.score, &s.mixed, &s.state, &s.input, &s.hidden, &s.total, &s.action, &s.logits};
  for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) {
    *slots[i] = reinterpret_cast<float *>(at);
    at += sizes[i];
  }
  s.decision = reinterpret_cast<int32_t *>(at);
  at += kDecisionCols * sizeof(int32_t);
  s.packed_cards = at;
  at += size_t(kMaxRows) * kPackedCols;
  s.packed_actions = at;
  at += size_t(kMaxOptions) * kActionCols;
  s.hist = at;
  at += size_t(kHistRows) * kHistCols;
  s.global = at;
  s.mapping = map;
  s.mapping_bytes = total;
  return true;
}

// ---------------------------------------------------------------------------
// obs.audit for a batch of one. Same checks, same order. Returns kOk or the rule.
inline int audit(const RawObs &o, int n_codes) {
  const uint8_t *cards = o.cards, *selection = o.selection, *actions = o.actions, *glob = o.global, *hist = o.hist;
  const int32_t *d = o.decision;
  const int count = o.num_options;
  if (count <= 0 || count > kMaxOptions || o.overflow != 0 || count != o.legal_options) return kAuditOptions;
  auto card = [&](int r, int col) { return int(cards[r * kCardCols + col]); };
  auto act = [&](int a, int col) { return int(actions[a * kActionCols + col]); };
  auto sel = [&](int r, int col) { return int(selection[r * kSelectionCols + col]); };
  auto ident = [&](int r) { return (card(r, 0) << 8) + card(r, 1); };
  for (int r = 0; r < kMaxRows; r++)
    if (ident(r) > n_codes) return kAuditVisibleId;
  for (int r = 0; r < kMaxRows; r++)
    if (card(r, 2) == 2 && card(r, 4) == 0 && ident(r) == 0) return kAuditOwnHand;
  for (int r = 0; r < kMaxRows; r++)
    if (ident(r) == 0 && card(r, 2) > 0)
      for (int col = 7; col < kCardCols; col++)
        if (card(r, col) != 0) return kAuditHiddenStats;
  static constexpr int card_bounds[][2] = {{2, 8}, {3, 79}, {4, 1}, {5, 8}, {6, 1}, {7, 7}, {8, 26}, {9, 13}, {10, 15}, {11, 1}};
  for (auto &b : card_bounds)
    for (int r = 0; r < kMaxRows; r++)
      if (card(r, b[0]) > b[1]) return kAuditCardColumn + b[0];
  static constexpr int action_bounds[][2] = {{3, 16}, {4, 9}, {5, 1}, {6, 255}, {7, 3}, {8, 8}, {9, 13}, {10, 31}, {11, 9}};
  for (auto &b : action_bounds)
    for (int a = 0; a < count; a++)
      if (act(a, b[0]) > b[1]) return kAuditActionColumn + b[0];
  for (int i = count * kActionCols; i < kMaxOptions * kActionCols; i++)
    if (actions[i] != 0) return kAuditOfferedData;
  for (int a = 0; a < count; a++)
    if ((act(a, 1) << 8) + act(a, 2) > n_codes) return kAuditActionId;
  for (int a = 0; a < count; a++)
    if (act(a, 0) > kMaxRows) return kAuditRowRef;
  for (int a = 0; a < count; a++) {
    int ref = act(a, 0);
    if (ref > 0 && (card(ref - 1, 0) != act(a, 1) || card(ref - 1, 1) != act(a, 2))) return kAuditActionCard;
  }
  if (d[0] != 1) return kAuditSchema;
  int sum1 = 0, sum2 = 0;
  for (int r = 0; r < kMaxRows; r++) sum1 += sel(r, 1), sum2 += sel(r, 2);
  if (d[6] != sum1 || d[7] != sum2) return kAuditSelCounters;
  for (int r = 0; r < kMaxRows; r++) {
    if (sel(r, 0) > 1 || sel(r, 1) > 1 || sel(r, 2) > 1) return kAuditSelFlags;
    if (sel(r, 2) > sel(r, 1)) return kAuditSelFlags;
  }
  for (int r = 0; r < kMaxRows; r++)
    if ((sel(r, 0) | sel(r, 1) | sel(r, 2)) != 0 && card(r, 2) == 0) return kAuditSelPadding;
  if (d[11] == 0) {
    for (int i = 1; i < kDecisionCols; i++)
      if (d[i] != 0) return kAuditStale;
    for (int i = 0; i < kMaxRows * kSelectionCols; i++)
      if (selection[i] != 0) return kAuditStale;
  } else {
    const int hint = d[1], stage = d[2];
    if (hint != 0 && (hint < kHintMin || hint > kHintMax)) return kAuditHint;
    if (stage < 0 || stage > 3) return kAuditStage;
    if (d[4] < 0 || d[5] < d[4]) return kAuditSelBounds;
    if (d[3] > 0) {
      if (d[3] > kMaxRows) return kAuditSourceRef;
      if (ident(d[3] - 1) <= 0) return kAuditSourcePrivate;
    }
    uint8_t expected[kMaxRows] = {};
    for (int a = 0; a < count; a++)
      if (act(a, 0) > 0) expected[act(a, 0) - 1] = 1;
    for (int r = 0; r < kMaxRows; r++)
      if (sel(r, 0) != expected[r]) return kAuditCandidates;
    bool finish = false, cancel = false;
    for (int a = 0; a < kMaxOptions; a++) {
      finish = finish || act(a, 5) != 0;
      cancel = cancel || act(a, 4) == 9;
    }
    if constexpr (kFault != kFaultAuditSkipFinish)
      if (d[8] != int(finish) || d[9] != int(cancel)) return kAuditFinishCancel;
  }
  static constexpr int global_bounds[][2] = {{4, 16}, {5, 10}, {6, 1}, {7, 1}};
  for (auto &b : global_bounds)
    if (glob[b[0]] > b[1]) return kAuditGlobalColumn + b[0];
  static constexpr int hist_bounds[][2] = {{3, 16}, {4, 9}, {5, 1}, {6, 255}, {7, 3}, {8, 8}, {9, 13}, {10, 31}, {11, 9}, {12, 255}, {13, 10}};
  for (auto &b : hist_bounds)
    for (int h = 0; h < kHistRows; h++)
      if (hist[h * kHistCols + b[0]] > b[1]) return kAuditHistColumn + b[0];
  for (int h = 0; h < kHistRows; h++)
    if ((int(hist[h * kHistCols + 1]) << 8) + hist[h * kHistCols + 2] > n_codes) return kAuditHistId;
  return kOk;
}

// obs.normalize_history_turn. Column 12 is absolute until the ring buffer wraps.
inline int normalize_history_turn(const uint8_t *hist, const uint8_t *global, uint8_t *out) {
  std::memcpy(out, hist, size_t(kHistRows) * kHistCols);
  if constexpr (kFault == kFaultNoHistoryNorm) return kOk;
  const bool unfilled = hist[3] == 0;
  const int turn = global[4];
  int top = 0;
  for (int h = 0; h < kHistRows; h++) {
    uint8_t *row = out + h * kHistCols;
    if (unfilled && row[3] != 0) {
      int absolute = turn - int(row[12]);
      row[12] = uint8_t(absolute < 0 ? 0 : absolute > 16 ? 16 : absolute);
    }
    top = row[12] > top ? row[12] : top;
  }
  return top > 16 ? kHistoryTurn : kOk;
}

// obs.pack with row_lookup, for a batch of one. Expects s.hist already normalized.
// Python wraps a negative source around the end of its index table, and a negative
// source passes its audit. Here it is an error.
inline int pack(const RawObs &o, Scratch &s) {
  const int count = o.num_options;
  if (count <= 0 || count > kMaxOptions) return kBadOptions;
  const int source = o.decision[3];
  if (source < 0 || source > kMaxRows) return kBadSource;
  uint8_t keep[kMaxRows];
  for (int r = 0; r < kMaxRows; r++) {
    const uint8_t *row = o.cards + r * kCardCols;
    const int loc = row[2];
    const bool hidden_private = row[0] == 0 && row[1] == 0 && (loc == 1 || loc == 2 || loc == 7);
    const uint8_t *flags = o.selection + r * kSelectionCols;
    bool selected = (flags[0] | flags[1] | flags[2]) != 0;
    if constexpr (kFault == kFaultPackDropSelected) selected = false;
    keep[r] = uint8_t((loc > 0 && !hidden_private) || selected);
  }
  if (source > 0) keep[source - 1] = 1;
  for (int a = 0; a < count; a++) {
    const int ref = o.actions[a * kActionCols];
    if (ref > kMaxRows) return kBadRowRef;
    if (ref > 0) keep[ref - 1] = 1;
  }
  uint8_t new_index[kMaxRows + 1];
  new_index[0] = 0;
  int n = 0;
  if constexpr (kFault == kFaultShuffleRemapped || kFault == kFaultShuffleRaw) {
    int kept = 0;
    for (int r = 0; r < kMaxRows; r++) kept += keep[r];
    for (int r = 0; r < kMaxRows; r++) {
      if (keep[r]) {
        n++;
        int to = kept - n;
        std::memcpy(s.packed_cards + to * kPackedCols, o.cards + r * kCardCols, kCardCols);
        std::memcpy(s.packed_cards + to * kPackedCols + kCardCols, o.selection + r * kSelectionCols, kSelectionCols);
        s.packed_cards[to * kPackedCols + 44] = 0;
        new_index[r + 1] = uint8_t(kFault == kFaultShuffleRemapped ? to + 1 : n);
      } else {
        new_index[r + 1] = 0;
      }
    }
  } else {
    for (int r = 0; r < kMaxRows; r++) {
      if (keep[r]) {
        uint8_t *to = s.packed_cards + n * kPackedCols;
        std::memcpy(to, o.cards + r * kCardCols, kCardCols);
        std::memcpy(to + kCardCols, o.selection + r * kSelectionCols, kSelectionCols);
        to[44] = 0;
        n++;
        new_index[r + 1] = uint8_t(n);
      } else {
        new_index[r + 1] = 0;
      }
    }
  }
  s.n_rows = n;
  s.count = count;
  std::memcpy(s.decision, o.decision, kDecisionCols * sizeof(int32_t));
  const int mapped = new_index[source];
  s.decision[3] = mapped;
  if (mapped > 0) {
    if constexpr (kFault != kFaultSourceFlagMissing) s.packed_cards[(mapped - 1) * kPackedCols + 44] = 1;
  }
  std::memcpy(s.packed_actions, o.actions, size_t(count) * kActionCols);
  for (int a = 0; a < count; a++) {
    uint8_t *ref = s.packed_actions + a * kActionCols;
    ref[0] = new_index[ref[0]];
    if constexpr (kFault == kFaultPackRefOffByOne)
      if (ref[0] > 1) ref[0]--;
  }
  std::memcpy(s.global, o.global, kGlobalCols);
  return kOk;
}

// ---------------------------------------------------------------------------
// Forward pass of EdisonNet over the packed observation in `s`.
namespace detail {

// nn.Dense over one input vector: out = bias + x @ kernel. Kernels are (in, out).
EDNET_INLINE void dense(const Dense &d, const float *x, float *out) {
  if (d.bias)
    std::memcpy(out, d.bias, sizeof(float) * d.out);
  else
    std::memset(out, 0, sizeof(float) * d.out);
  accumulate(out, x, d.in, d.kernel, size_t(d.out), d.out);
}

// dense for a mostly-zero input of at most kMaxListed entries.
inline constexpr int kMaxListed = 1024;
inline void dense_listed(const Dense &d, const float *x, float *out) {
  if (d.in > kMaxListed) return dense(d, x, out);
  int index[kMaxListed];
  float value[kMaxListed];
  int n = 0;
  for (int i = 0; i < d.in; i++) {
    index[n] = i;
    value[n] = x[i];
    n += x[i] != 0.0f;
  }
  std::memcpy(out, d.bias, sizeof(float) * d.out);
  int o = 0;
  for (; o + 4 * EDNET_LANES <= d.out; o += 4 * EDNET_LANES)
    accumulate_listed<4>(out + o, index, value, n, d.kernel + o, size_t(d.out));
  for (; o + EDNET_LANES <= d.out; o += EDNET_LANES)
    accumulate_listed<1>(out + o, index, value, n, d.kernel + o, size_t(d.out));
  for (; o < d.out; o++)
    for (int i = 0; i < n; i++) out[o] += value[i] * d.kernel[size_t(index[i]) * d.out + o];
}

// flax nn.LayerNorm defaults: epsilon 1e-6, mean and scale on, fast variance, rsqrt.
EDNET_INLINE void layer_norm(const Norm &n, const float *x, float *out, int c) {
  const float epsilon = kFault == kFaultLayerNormEps ? 1e-5f : 1e-6f;
  float sum = 0, square = 0;
  {
    vf a{}, b{};
    int i = 0;
    for (; i + EDNET_LANES <= c; i += EDNET_LANES) {
      vf v = load(x + i);
      a += v;
      b += v * v;
    }
    sum = hsum(a), square = hsum(b);
    for (; i < c; i++) sum += x[i], square += x[i] * x[i];
  }
  const float mean = sum / float(c), mean2 = square / float(c);
  float var = mean2 - mean * mean;
  var = var > 0.0f ? var : 0.0f;
  const float rsqrt = 1.0f / std::sqrt(var + epsilon);
  int i = 0;
  vf vmean = broadcast(mean), vmul = broadcast(rsqrt);
  for (; i + EDNET_LANES <= c; i += EDNET_LANES)
    store(out + i, (load(x + i) - vmean) * (vmul * load(n.scale + i)) + load(n.bias + i));
  for (; i < c; i++) out[i] = (x[i] - mean) * (rsqrt * n.scale[i]) + n.bias[i];
}

EDNET_INLINE void put(float *&p, const Embed &e, unsigned index, bool &bad) {
  if (index >= unsigned(e.n)) {
    bad = true;
    index = 0;
  }
  const float *from = e.table + size_t(index) * e.dim;
  for (int i = 0; i < e.dim; i++) p[i] = from[i];
  p += e.dim;
}

// net.thermometer. Bins and the unknown bit.
EDNET_INLINE void thermometer(float *&p, const Net &net, int high, int low) {
  const float value = float(high) * 256.0f + float(low);
  const bool unknown = value > 60000.0f;
  const float known = unknown ? 0.0f : value;
  vf k = broadcast(known);
  int i = 0;
  for (; i + EDNET_LANES <= kBins; i += EDNET_LANES) {
    vf w = load(net.bin_widths + i);
    vf v = (k - load(net.bin_points + i) + w) / w;
    v = v < 0.0f ? vf{} : v;
    v = v > 1.0f ? broadcast(1.0f) : v;
    store(p + i, v);
  }
  for (; i < kBins; i++) {
    float v = (known - net.bin_points[i] + net.bin_widths[i]) / net.bin_widths[i];
    p[i] = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
  }
  p[kBins] = (unknown && kFault != kFaultUnknownBit) ? 1.0f : 0.0f;
  p += kThermometer;
}

EDNET_INLINE void put_fields(float *&p, const ActionEmbeds &e, const uint8_t *a, bool &bad) {
  put(p, e.msg, a[3], bad);
  put(p, e.act, a[4], bad);
  put(p, e.finish, a[5], bad);
  put(p, e.effect, a[6], bad);
  put(p, e.phase, a[7], bad);
  put(p, e.position, a[8], bad);
  put(p, e.number, a[9], bad);
  put(p, e.place, a[10], bad);
  put(p, e.attrib, a[11], bad);
}

inline void attention(const Block &b, Scratch &s, int length, int c, int heads) {
  const int size = c / heads;
  const float root = std::sqrt(float(size));
  // Dense(3c, use_bias=False) over the normalized input.
  for (int m = 0; m < length; m++) dense(b.qkv, s.norm + size_t(m) * c, s.wide + size_t(m) * 3 * c);
  const int padded = (length + EDNET_LANES - 1) / EDNET_LANES * EDNET_LANES;
  float *score = s.score;
  for (int h = 0; h < heads; h++) {
    const int base = h * size;
    // Keys, one column per dimension, so scores can run along the key axis.
    for (int d = 0; d < size; d++) {
      float *col = s.key + size_t(d) * (kMaxRows + 1 + EDNET_LANES);
      for (int n = 0; n < length; n++) col[n] = s.wide[size_t(n) * 3 * c + c + base + d];
      for (int n = length; n < padded; n++) col[n] = 0.0f;
    }
    for (int m = 0; m < length; m++) {
      const float *q = s.wide + size_t(m) * 3 * c + base;
      // Softmax over the valid rows. There are no masked rows in a batch of one:
      // JAX's masked -1e9 scores go to exactly 0 after exp.
      vf top = broadcast(-INFINITY);
      vf vroot = broadcast(root);
      for (int n = 0; n < padded; n += EDNET_LANES) {
        vf v{};
        for (int d = 0; d < size; d++) v += broadcast(q[d]) * load(s.key + size_t(d) * (kMaxRows + 1 + EDNET_LANES) + n);
        v = v / vroot;
        store(score + n, v);
        // Mask the zero-padded tail out of the max.
        if (n + EDNET_LANES > length) {
          vi tail;
          for (int i = 0; i < EDNET_LANES; i++) tail[i] = (n + i) < length ? -1 : 0;
          v = tail ? v : top;
        }
        top = v > top ? v : top;
      }
      vf vtop = broadcast(hmax(top));
      vf sum{};
      for (int n = 0; n < padded; n += EDNET_LANES) {
        vf e = exp_nonpositive(load(score + n) - vtop);
        if (n + EDNET_LANES > length) {
          vi tail;
          for (int i = 0; i < EDNET_LANES; i++) tail[i] = (n + i) < length ? -1 : 0;
          e = tail ? e : vf{};
        }
        store(score + n, e);
        sum += e;
      }
      const vf total = broadcast(hsum(sum));
      for (int n = 0; n < padded; n += EDNET_LANES) store(score + n, load(score + n) / total);
      float *out = s.mixed + size_t(m) * c + base;
      std::memset(out, 0, sizeof(float) * size);
      accumulate(out, score, length, s.wide + 2 * c + base, size_t(3 * c), size);
    }
  }
}

inline void transformer(const Net &net, Scratch &s, int length) {
  const int c = net.config.channels;
  for (int layer = 0; layer < net.config.layers; layer++) {
    const Block &b = net.blocks[layer];
    for (int m = 0; m < length; m++) layer_norm(b.norm_attn, s.x + size_t(m) * c, s.norm + size_t(m) * c, c);
    attention(b, s, length, c, net.config.heads);
    for (int m = 0; m < length; m++) {
      dense(b.attn_out, s.mixed + size_t(m) * c, s.hidden);
      add_to(s.x + size_t(m) * c, s.hidden, c);
    }
    for (int m = 0; m < length; m++) {
      layer_norm(b.norm_mlp, s.x + size_t(m) * c, s.norm, c);
      dense(b.mlp_in, s.norm, s.input);
      gelu(s.input, 2 * c);
      dense(b.mlp_out, s.input, s.hidden);
      add_to(s.x + size_t(m) * c, s.hidden, c);
    }
  }
}

// Encoder: fills s.x (rows after the final norm, with the token at 0) and s.state.
inline int encode(const Net &net, Scratch &s) {
  const int c = net.config.channels;
  const int n_rows = s.n_rows;
  bool bad = false;

  // Rows. Index 0 of x is the context token, so row r (zero-based) is x[r + 1].
  for (int r = 0; r < n_rows; r++) {
    const uint8_t *row = s.packed_cards + size_t(r) * kPackedCols;
    float *p = s.input;
    put(p, net.card_id, (unsigned(row[0]) << 8) + row[1], bad);
    put(p, net.loc, row[2], bad);
    put(p, net.seq, row[3], bad);
    put(p, net.owner, row[4], bad);
    put(p, net.pos, row[5], bad);
    put(p, net.overlay, row[6], bad);
    put(p, net.attr, row[7], bad);
    put(p, net.race, row[8], bad);
    put(p, net.level, row[9], bad);
    put(p, net.counter, row[10], bad);
    put(p, net.negated, row[11], bad);
    thermometer(p, net, row[12], row[13]);
    thermometer(p, net, row[14], row[15]);
    for (int i = 16; i < kPackedCols; i++) *p++ = float(row[i]);
    dense_listed(net.row_in, s.input, s.hidden);
    gelu(s.hidden, c);
    dense(net.row_out, s.hidden, s.x + size_t(r + 1) * c);
  }

  // Own recent actions, pooled over the valid ones.
  float *pool = s.total;
  std::memset(pool, 0, sizeof(float) * c);
  float valid = 0.0f;
  for (int h = 0; h < kHistRows; h++) {
    const uint8_t *a = s.hist + h * kHistCols;
    float *p = s.input;
    put_fields(p, net.hist_fields, a, bad);
    put(p, net.card_id, (unsigned(a[1]) << 8) + a[2], bad);
    put(p, net.h_turns, a[12], bad);
    put(p, net.h_phase, a[13], bad);
    if (a[3] == 0) continue;
    dense(net.hist_in, s.input, s.hidden);
    gelu(s.hidden, c);
    add_to(pool, s.hidden, c);
    valid += 1.0f;
  }
  const float divisor = valid > 1.0f ? valid : 1.0f;

  // Context token.
  const uint8_t *g = s.global;
  const int32_t *d = s.decision;
  float *p = s.input;
  thermometer(p, net, g[0], g[1]);
  thermometer(p, net, g[2], g[3]);
  put(p, net.turn, g[4], bad);
  put(p, net.phase, g[5], bad);
  put(p, net.first, g[6], bad);
  put(p, net.my_turn, g[7], bad);
  for (int i = 8; i < 22; i++) *p++ = float(g[i]) / 40.0f;
  const int64_t hint_index = d[1] == 0 ? 0 : int64_t(d[1]) - kHintMin + (kFault == kFaultHintOffByOne ? 0 : 1);
  put(p, net.hint, unsigned(hint_index), bad);
  put(p, net.stage, unsigned(d[2]), bad);
  *p++ = float(d[4]) / 5.0f;
  *p++ = float(d[5]) / 5.0f;
  *p++ = float(d[6]) / 5.0f;
  *p++ = float(d[7]) / 5.0f;
  *p++ = float(d[8]);
  *p++ = float(d[9]);
  *p++ = float(d[10]) / 12.0f;
  *p++ = float(d[11]);
  *p++ = d[3] > 0 ? 1.0f : 0.0f;
  for (int i = 0; i < c; i++) *p++ = pool[i] / divisor;
  dense(net.token_in, s.input, s.hidden);
  gelu(s.hidden, c);
  dense(net.token_out, s.hidden, s.x);
  if (bad) return kEmbedRange;

  const int length = n_rows + 1;
  transformer(net, s, length);
  for (int m = 0; m < length; m++) {
    layer_norm(net.final_norm, s.x + size_t(m) * c, s.norm, c);
    std::memcpy(s.x + size_t(m) * c, s.norm, sizeof(float) * c);
  }

  // State from the token, the source row and the mean over rows.
  std::memcpy(s.input, s.x, sizeof(float) * c);
  const int source = d[3];
  if (source < 0 || source > n_rows) return kEmbedRange;
  const float *pointed = source == 0 ? (kFault == kFaultNoRowUnused ? s.x : net.no_row) : s.x + size_t(source) * c;
  std::memcpy(s.input + c, pointed, sizeof(float) * c);
  float *mean = s.input + 2 * c;
  std::memset(mean, 0, sizeof(float) * c);
  if constexpr (kFault == kFaultContextInPool) add_to(mean, s.x, c);
  for (int r = 0; r < n_rows; r++) add_to(mean, s.x + size_t(r + 1) * c, c);
  const float rows = float(n_rows > 1 ? n_rows : 1);
  for (int i = 0; i < c; i++) mean[i] /= rows;
  dense(net.state_in, s.input, s.hidden);
  gelu(s.hidden, 2 * c);
  dense(net.state_out, s.hidden, s.state);
  return kOk;
}

inline float value_head(const Net &net, Scratch &s) {
  dense(net.value_hidden, s.state, s.hidden);
  gelu(s.hidden, net.config.critic_hidden);
  const float out = dot(s.hidden, net.value_out.kernel, net.config.critic_hidden) + net.value_out.bias[0];
  return tanh_s(out);
}

// Logits for s.count options. Needs s.x and s.state from encode.
inline int logit_head(const Net &net, Scratch &s) {
  const int c = net.config.channels, count = s.count;
  bool bad = false;
  float *total = s.total;
  std::memset(total, 0, sizeof(float) * c);
  for (int a = 0; a < count; a++) {
    const uint8_t *row = s.packed_actions + size_t(a) * kActionCols;
    float *p = s.input;
    put_fields(p, net.action_fields, row, bad);
    put(p, net.card_id, (unsigned(row[1]) << 8) + row[2], bad);
    int ref = row[0];
    if (ref > s.n_rows) return kEmbedRange;
    const float *pointed;
    if constexpr (kFault == kFaultForwardRefOffByOne) {
      pointed = ref == 0 ? net.no_row : s.x + size_t(ref > 1 ? ref - 1 : 1) * c;
    } else if constexpr (kFault == kFaultNoRowUnused) {
      pointed = s.x + size_t(ref) * c;
    } else {
      pointed = ref == 0 ? net.no_row : s.x + size_t(ref) * c;
    }
    std::memcpy(p, pointed, sizeof(float) * c);
    float *out = s.action + size_t(a) * c;
    dense(net.action_in, s.input, out);
    gelu(out, c);
    add_to(total, out, c);
  }
  if (bad) return kEmbedRange;
  // Mean offered action in this decision.
  const float divisor = kFault == kFaultPoolSum ? 1.0f : float(count > 1 ? count : 1);
  for (int i = 0; i < c; i++) total[i] /= divisor;
  float *pool = s.input;
  dense(net.action_pool, total, pool);
  float *state = s.input + c;
  dense(net.action_state, s.state, state);
  // The state and pool parts of logit_hidden are the same for every option.
  const Dense &lh = net.logit_hidden;
  float *base = s.input + 2 * c;
  std::memcpy(base, lh.bias, sizeof(float) * c);
  accumulate(base, state, c, lh.kernel + size_t(c) * c, size_t(c), c);
  accumulate(base, pool, c, lh.kernel + size_t(3 * c) * c, size_t(c), c);
  float *product = s.input + 3 * c;
  for (int a = 0; a < count; a++) {
    const float *action = s.action + size_t(a) * c;
    float *hidden = s.hidden;
    std::memcpy(hidden, base, sizeof(float) * c);
    accumulate(hidden, action, c, lh.kernel, size_t(c), c);
    for (int i = 0; i < c; i++) product[i] = action[i] * state[i];
    accumulate(hidden, product, c, lh.kernel + size_t(2 * c) * c, size_t(c), c);
    gelu(hidden, c);
    s.logits[a] = dot(hidden, net.logit_out.kernel, c) + net.logit_out.bias[0];
  }
  return kOk;
}

inline int prepare(const Net &net, Scratch &s, const RawObs &o, bool run_audit) {
  if (run_audit) {
    const int rule = audit(o, net.config.n_codes);
    if (rule != kOk) return rule;
  }
  int status = normalize_history_turn(o.hist, o.global, s.hist);
  if (status != kOk) return status;
  return pack(o, s);
}
}  // namespace detail

// The value path never touches the action path.
inline int evaluate_value(const Net &net, Scratch &s, const RawObs &o, bool run_audit, float *value) {
  int status = detail::prepare(net, s, o, run_audit);
  if (status != kOk) return status;
  status = detail::encode(net, s);
  if (status != kOk) return status;
  *value = detail::value_head(net, s);
  return kOk;
}

inline int evaluate_full(const Net &net, Scratch &s, const RawObs &o, bool run_audit, float *value, const float **logits) {
  int status = detail::prepare(net, s, o, run_audit);
  if (status != kOk) return status;
  status = detail::encode(net, s);
  if (status != kOk) return status;
  status = detail::logit_head(net, s);
  if (status != kOk) return status;
  *value = detail::value_head(net, s);
  *logits = s.logits;
  return kOk;
}

// First maximum, as np.argmax does.
inline int greedy(const float *logits, int count) {
  int best = 0;
  for (int i = 1; i < count; i++)
    if (logits[i] > logits[best]) best = i;
  return best;
}
}  // namespace ednet
