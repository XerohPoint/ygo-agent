// Implementation of fm_world.h. See that header and docs/edison-forward-model.md.
//
// How failure inside a sampled world stays inside it. Everything a duel touches is in the arena, and a
// restore replaces all of it, allocator included. So any way out of a core call is fine, however much
// it left half done: return, C++ throw, or a signal handler's siglongjmp out of the core or Lua. Only
// process-wide state could survive a restore, and there is little: stdio (pinned to static storage and
// checked for markers, see Guard), the signal state below, and the core's static function pointers.
#include "fm_world.h"

#include <execinfo.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>

#include "fm_deal.h"

extern "C" char __executable_start;

namespace fm {

static_assert(int(DealStatus::kNoValidAssignment) == int(DealRefusal::kNoValidAssignment));
static_assert(int(DealStatus::kCount) == int(DealRefusal::kCountFromDeal));

namespace {
sigjmp_buf g_guard_jump;
volatile sig_atomic_t g_guard_armed = 0;
volatile sig_atomic_t g_guard_signal = 0;
volatile sig_atomic_t g_guard_epoch = 0;
volatile sig_atomic_t g_watch_epoch = -1;
volatile sig_atomic_t g_watch_ticks = 0;
int g_watch_limit_ticks = 8;
constexpr int kWatchIntervalMs = 250;
bool g_signals_installed = false;
ForwardModelConfig g_config;
bool g_initialised = false;
bool g_referee_alive = false;

void on_guard_signal(int sig, siginfo_t *info, void *context) {
  if (sig == SIGVTALRM) {
    if (!g_guard_armed || !g_active) return;
    if (g_watch_epoch != g_guard_epoch) {
      g_watch_epoch = g_guard_epoch;
      g_watch_ticks = 0;
      return;
    }
    if (++g_watch_ticks < g_watch_limit_ticks) return;
  }
  if (g_guard_armed) {
    g_guard_signal = sig;
    g_guard_armed = 0;
    siglongjmp(g_guard_jump, 1);
  }
  // Outside any guard this is a bug in the forward model or the test bed. Say where, for addr2line.
  auto *registers = static_cast<ucontext_t *>(context);
  char line[160];
  int length = snprintf(line, sizeof line, "fatal signal %d at address %p, code offset 0x%zx, frames:\n", sig, info->si_addr,
                        reinterpret_cast<char *>(registers->uc_mcontext.gregs[REG_RIP]) - &__executable_start);
  (void)!write(2, line, length);
  void *frames[48];
  int count = backtrace(frames, 48);
  for (int i = 0; i < count; ++i) {
    length = snprintf(line, sizeof line, "0x%zx\n", static_cast<char *>(frames[i]) - &__executable_start);
    (void)!write(2, line, length);
  }
  signal(sig, SIG_DFL);
  raise(sig);
}

void install_signals() {
  if (g_signals_installed) return;
  g_signals_installed = true;
  static char alternate[1 << 16];
  stack_t stack{};
  stack.ss_sp = alternate;
  stack.ss_size = sizeof alternate;
  sigaltstack(&stack, nullptr);
  void *preload[2];
  backtrace(preload, 2);  // Its first call loads a library, which must not happen inside a handler.
  struct sigaction sa {};
  sa.sa_sigaction = on_guard_signal;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;  // Not blocked in the handler, so a jump needs no mask restore.
  sigemptyset(&sa.sa_mask);
  for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGVTALRM}) sigaction(sig, &sa, nullptr);
}

// CPU time only, so a loaded machine does not void healthy worlds, and no system call on the hot path:
// the handler compares the guard's epoch between ticks. A timer does not survive fork, so each duel arms it.
void arm_watchdog() {
  itimerval timer{};
  timer.it_interval.tv_usec = kWatchIntervalMs * 1000;
  timer.it_value = timer.it_interval;
  setitimer(ITIMER_VIRTUAL, &timer, nullptr);
}

char g_last_message[320] = {0};

// Runs a call on the duel. On any failure returns why. The duel's memory is then whatever the failure
// left, and only a restore makes it usable.
template <typename F>
VoidReason guarded(F &&call) {
  char *stdout_mark = stdout->_IO_write_ptr;
  long diagnostics = g_diagnostics;
  VoidReason reason = VoidReason::kNone;
  ++g_guard_epoch;
  if (sigsetjmp(g_guard_jump, 0)) {
    g_active = false;
    snprintf(g_last_message, sizeof g_last_message, "signal %d", int(g_guard_signal));
    return g_guard_signal == SIGVTALRM ? VoidReason::kHang : VoidReason::kFatalSignal;
  }
  g_guard_armed = 1;
  try {
    Scope arena(true);
    call();
  } catch (const std::exception &ex) {
    snprintf(g_last_message, sizeof g_last_message, "%s", ex.what());
    reason = VoidReason::kEnvironmentThrow;
  } catch (...) {
    snprintf(g_last_message, sizeof g_last_message, "unknown exception");
    reason = VoidReason::kEnvironmentThrow;
  }
  g_guard_armed = 0;
  if (reason != VoidReason::kNone) return reason;
  if (g_diagnostics != diagnostics) {
    snprintf(g_last_message, sizeof g_last_message, "%s", g_last_diagnostic);
    return VoidReason::kLuaError;
  }
  // Production prints these markers and carries on with a blank row. Its workers fail closed by
  // reading their output. Here the text is still in the static stdio buffer: read it and drop it.
  if (stdout->_IO_write_ptr != stdout_mark) {
    size_t n = std::min<size_t>(stdout->_IO_write_ptr - stdout_mark, sizeof g_last_message - 1);
    std::memcpy(g_last_message, stdout_mark, n);
    g_last_message[n] = 0;
    stdout->_IO_write_ptr = stdout_mark;
    return VoidReason::kNativeDiagnostic;
  }
  return VoidReason::kNone;
}

void fill_observation(FmEnv *e, Observation *out) {
  auto f = e->obs_fields();
  out->to_play = e->to_play();
  out->n_options = static_cast<int>(e->actions().size());
  out->cards = static_cast<const uint8_t *>(f.data[0]);
  out->global = static_cast<const uint8_t *>(f.data[1]);
  out->decision = static_cast<const int32_t *>(f.data[2]);
  out->selection = static_cast<const uint8_t *>(f.data[3]);
  out->actions = static_cast<const uint8_t *>(f.data[4]);
  out->history = static_cast<const uint8_t *>(f.data[5]);
}
}  // namespace

void init_forward_model(const ForwardModelConfig &config) {
  if (g_initialised) throw std::logic_error("init_forward_model twice");
  g_initialised = true;
  g_config = config;
  g_watch_limit_ticks = std::max(2, config.step_cpu_limit_ms / kWatchIntervalMs);
  init_assets(config.root, config.registry);
  install_signals();
}

struct Referee::Impl {
  FmEnv *env = nullptr;
  PublicMemory memory;
  WorldBaseline baseline;
  TapContext tap;
  bool search_open = false;
  bool encoded = false;

  // A failure of the true duel is not a void: nothing is sampled, so it stops the process's duel.
  void call(const std::function<void()> &work) {
    g_message_tap = on_core_messages;
    g_message_tap_context = &tap;
    VoidReason reason = guarded(work);
    g_message_tap = nullptr;
    if (reason != VoidReason::kNone) throw std::runtime_error(std::string("the true duel failed: ") + g_last_message);
  }
};

Referee::Referee(const std::string &deck0, const std::string &deck1, uint64_t seed_plus_env) : impl_(new Impl) {
  if (!g_initialised) throw std::logic_error("init_forward_model first");
  if (g_referee_alive) throw std::logic_error("one Referee per process: the arena has a fixed address");
  g_referee_alive = true;
  g_use_arena = true;
  arm_watchdog();
  arena_clear();
  (void)spec_for_decks(deck0, deck1, static_cast<int>(seed_plus_env));
  {
    Scope arena(true);
    void *mem = malloc(sizeof(FmEnv));
    impl_->env = new (mem) FmEnv(spec_for_decks(deck0, deck1, static_cast<int>(seed_plus_env)), env_seed_of(seed_plus_env));
  }
  impl_->tap = {impl_->env, &impl_->memory};
  impl_->call([&] { impl_->env->reset(); });
  capture_baseline(impl_->env, impl_->baseline);
  if (!impl_->env->done()) note_public(impl_->env, impl_->memory);
}

Referee::~Referee() {
  g_referee_alive = false;
  g_use_arena = false;
}

bool Referee::done() const { return impl_->env->done(); }
int Referee::to_play() const { return impl_->env->to_play(); }
int Referee::option_count() const { return static_cast<int>(impl_->env->actions().size()); }
int Referee::winner() const { return impl_->env->reported_winner(); }
int Referee::win_reason() const { return impl_->env->reported_win_reason(); }

Observation Referee::observe() {
  if (impl_->search_open) throw std::logic_error("close the search world first");
  Observation out;
  impl_->call([&] { impl_->env->encode(); });
  impl_->encoded = true;
  fill_observation(impl_->env, &out);
  return out;
}

void Referee::play(int option) {
  if (impl_->search_open) throw std::logic_error("close the search world first");
  FmEnv *e = impl_->env;
  if (option < 0 || option >= option_count()) throw std::out_of_range("no such option");
  RowMarker marker = before_true_step(e, impl_->memory, option);
  bool encoded = impl_->encoded;
  impl_->call([&] {
    if (!encoded) e->stamp();
    e->step(option);
  });
  impl_->encoded = false;
  after_true_step(e, impl_->memory, marker);
  if (!e->done()) note_public(e, impl_->memory);
}

struct SearchWorld::Impl {
  Referee::Impl *duel = nullptr;
  int viewer = 0;
  bool dealt = false, voided = false, encoded = false;
  VoidReason void_reason = VoidReason::kNone;
  struct Saved {
    Snapshot memory;
    bool in_use = false, dealt = false;
  };
  std::vector<std::unique_ptr<Saved>> saved;
  WorldStats stats;
  std::vector<uint32_t> cached_list;
  AssumedList assumed;

  Status fail(VoidReason reason) {
    voided = true;
    void_reason = reason;
    ++stats.voids[static_cast<int>(reason)];
    return Status::kVoid;
  }
  FmEnv *env() const { return duel->env; }
};

SearchWorld::SearchWorld(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<SearchWorld> Referee::open_search(int viewer) {
  if (impl_->search_open) throw std::logic_error("a search world is already open");
  FmEnv *e = impl_->env;
  if (e->done() || e->to_play() != viewer) return nullptr;
  auto world = std::make_unique<SearchWorld::Impl>();
  world->duel = impl_.get();
  world->viewer = viewer;
  world->saved.push_back(std::make_unique<SearchWorld::Impl::Saved>());
  world->saved[0]->in_use = true;
  snapshot_take(world->saved[0]->memory);
  impl_->search_open = true;
  return std::unique_ptr<SearchWorld>(new SearchWorld(std::move(world)));
}

SearchWorld::~SearchWorld() {
  auto &root = impl_->saved[0]->memory;
  restore_world(impl_->env(), root);
  for (auto &saved : impl_->saved) {
    Scope off(false);
    __libc_free(saved->memory.data);
    saved->memory.data = nullptr;
  }
  impl_->duel->search_open = false;
  impl_->duel->encoded = false;
}

int SearchWorld::viewer() const { return impl_->viewer; }
bool SearchWorld::dealt() const { return impl_->dealt; }
bool SearchWorld::voided() const { return impl_->voided; }
VoidReason SearchWorld::void_reason() const { return impl_->void_reason; }
const char *SearchWorld::last_diagnostic() const { return g_last_message; }
const WorldStats &SearchWorld::stats() const { return impl_->stats; }

SearchWorld::Handle SearchWorld::snapshot() {
  ++impl_->stats.snapshots;
  size_t slot = 0;
  while (slot < impl_->saved.size() && impl_->saved[slot]->in_use) ++slot;
  if (slot == impl_->saved.size()) impl_->saved.push_back(std::make_unique<Impl::Saved>());
  auto &saved = *impl_->saved[slot];
  saved.in_use = true;
  saved.dealt = impl_->dealt;
  if (impl_->voided) {
    saved.in_use = false;
    return -1;  // A voided world is not worth keeping: its memory is whatever the failure left.
  }
  snapshot_take(saved.memory);
  return static_cast<Handle>(slot);
}

Status SearchWorld::restore(Handle handle) {
  if (handle < 0 || handle >= static_cast<Handle>(impl_->saved.size()) || !impl_->saved[handle]->in_use) return Status::kBadHandle;
  auto &saved = *impl_->saved[handle];
  ++impl_->stats.restores;
  restore_world(impl_->env(), saved.memory);
  impl_->dealt = saved.dealt;
  impl_->voided = false;
  impl_->void_reason = VoidReason::kNone;
  impl_->encoded = false;
  return Status::kOk;
}

void SearchWorld::release(Handle handle) {
  if (handle > kRoot && handle < static_cast<Handle>(impl_->saved.size())) impl_->saved[handle]->in_use = false;
}

DealResult SearchWorld::deal(uint64_t seed, const DealAssumption &assumption) {
  DealResult out;
  if (impl_->dealt || impl_->voided) {
    out.refusal = DealRefusal::kNotAtRoot;
    ++impl_->stats.deals_refused[static_cast<int>(out.refusal)];
    return out;
  }
  impl_->assumed.opponent_main = assumption.opponent_main;
  DealReport report;
  VoidReason reason = guarded([&] {
    report = determinize(impl_->env(), impl_->viewer, seed, impl_->duel->memory, impl_->duel->baseline, impl_->assumed);
  });
  if (reason != VoidReason::kNone) {
    impl_->fail(reason);
    out.refusal = DealRefusal::kNotAtRoot;
    return out;
  }
  out.ok = report.ok();
  out.moved = report.moved;
  out.known_pinned = report.known_pinned;
  out.opponent_extra_not_redealt = report.opponent_extra_not_redealt;
  out.facedown_banished_not_redealt = report.facedown_banished_not_redealt;
  if (out.ok) {
    out.refusal = DealRefusal::kNone;
    impl_->dealt = true;
    impl_->encoded = false;
    ++impl_->stats.deals_ok;
  } else {
    out.refusal = static_cast<DealRefusal>(static_cast<int>(report.status));
    ++impl_->stats.deals_refused[static_cast<int>(out.refusal)];
  }
  return out;
}

int SearchWorld::acting_seat() const { return impl_->voided ? -1 : impl_->env()->to_play(); }
int SearchWorld::option_count() const {
  return impl_->voided ? 0 : static_cast<int>(impl_->env()->actions().size());
}

Status SearchWorld::observe(Observation *out) {
  if (impl_->voided) return Status::kVoid;
  FmEnv *e = impl_->env();
  if (e->done()) return Status::kTerminal;
  // Before a deal this is the viewer's own decision: open_search() and restore() guarantee that.
  ++impl_->stats.observes;
  if (e->actions().size() > 256) return impl_->fail(VoidReason::kOptionOverflow);
  VoidReason reason = guarded([&] { e->encode(); });
  if (reason != VoidReason::kNone) return impl_->fail(reason);
  impl_->encoded = true;
  fill_observation(e, out);
  return Status::kOk;
}

Status SearchWorld::step(int option) {
  if (impl_->voided) return Status::kVoid;
  if (!impl_->dealt) return Status::kNeedsDeal;
  FmEnv *e = impl_->env();
  if (e->done()) return Status::kTerminal;
  if (option < 0 || option >= static_cast<int>(e->actions().size())) return Status::kBadOption;
  if (e->actions().size() > 256) return impl_->fail(VoidReason::kOptionOverflow);
  ++impl_->stats.steps;
  bool encoded = impl_->encoded;
  VoidReason reason = guarded([&] {
    if (!encoded) e->stamp();
    e->step(option);
  });
  impl_->encoded = false;
  if (reason != VoidReason::kNone) return impl_->fail(reason);
  return e->done() ? Status::kTerminal : Status::kOk;
}

bool SearchWorld::terminal() const { return impl_->dealt && !impl_->voided && impl_->env()->done(); }
int SearchWorld::winner() const { return terminal() ? impl_->env()->reported_winner() : -1; }
int SearchWorld::win_reason() const { return terminal() ? impl_->env()->reported_win_reason() : 0; }
bool SearchWorld::truncated() const { return terminal() && impl_->env()->truncated(); }

#ifdef FM_TEST_HOOKS
// Test bed only: privileged views, and crashes planted inside the guard.
struct TestAccess {
  static FmEnv *env(SearchWorld &world) { return world.impl_->env(); }
  static Status fault(SearchWorld &world, const std::function<void()> &call) {
    VoidReason reason = guarded(call);
    return reason == VoidReason::kNone ? Status::kOk : world.impl_->fail(reason);
  }
};
#endif
}  // namespace fm
