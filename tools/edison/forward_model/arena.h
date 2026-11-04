// Transparent duel arena: a spike, not production code.
//
// While `fm::g_active` is set, every malloc-family allocation made by this
// process (operator new, std:: containers, Lua's l_alloc, the core) is served
// from one fixed-address mapping. That makes the complete state of a duel, core
// objects and Lua heap alike, a single contiguous byte range containing only
// pointers into itself, static data, or code. Copying those bytes out and back
// at the same address is therefore a full snapshot/restore without any
// knowledge of the core's structures and without patching the core or Lua.
//
// The allocator keeps all its metadata inside the arena, so restore rolls the
// allocator back too. Allocation addresses after a restore are exactly those of
// the original run, so pointer-ordered containers cannot diverge.
#pragma once
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
void *__libc_malloc(size_t);
void __libc_free(void *);
void *__libc_calloc(size_t, size_t);
void *__libc_realloc(void *, size_t);
void *__libc_memalign(size_t, size_t);
}

namespace fm {
constexpr uintptr_t kArenaBase = 0x100000000000ULL;
constexpr size_t kArenaCap = size_t(1) << 31;
constexpr size_t kPage = 4096;

struct ArenaHeader {
  uintptr_t top;
  void *free_small[66];
  void *free_large[34];
  uint64_t live_blocks;
};
struct BlockHeader {
  uint64_t cls;
  uint64_t size;
};
static_assert(sizeof(BlockHeader) == 16);

inline bool g_active = false;
inline bool g_mapped = false;

inline ArenaHeader *header() { return reinterpret_cast<ArenaHeader *>(kArenaBase); }
inline bool in_arena(const void *p) {
  return reinterpret_cast<uintptr_t>(p) - kArenaBase < kArenaCap;
}

inline void arena_map() {
  void *p = mmap(reinterpret_cast<void *>(kArenaBase), kArenaCap,
                 PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
  if (p != reinterpret_cast<void *>(kArenaBase)) {
    perror("arena mmap");
    abort();
  }
  g_mapped = true;
}

// Discard everything: the next allocation starts from clean zero pages.
inline void arena_clear() {
  if (!g_mapped) arena_map();
  madvise(reinterpret_cast<void *>(kArenaBase), kArenaCap, MADV_DONTNEED);
  header()->top = kArenaBase + ((sizeof(ArenaHeader) + 15) & ~size_t(15));
}

inline size_t arena_used() { return header()->top - kArenaBase; }
inline size_t arena_used_pages() { return (arena_used() + kPage - 1) / kPage; }

inline void *arena_alloc(size_t n) {
  if (n == 0) n = 1;
  ArenaHeader *h = header();
  void **head;
  uint64_t cls;
  size_t size;
  if (n <= 1024) {
    cls = (n + 15) / 16;
    size = cls * 16;
    head = &h->free_small[cls];
  } else {
    int k = 64 - __builtin_clzll(n - 1);
    cls = 1000 + k;
    size = size_t(1) << k;
    head = &h->free_large[k];
  }
  ++h->live_blocks;
  if (*head) {
    void *p = *head;
    *head = *reinterpret_cast<void **>(p);
    return p;
  }
  uintptr_t b = h->top;
  if (b + sizeof(BlockHeader) + size > kArenaBase + kArenaCap) {
    static const char msg[] = "fm arena exhausted\n";
    (void)!write(2, msg, sizeof(msg) - 1);
    abort();
  }
  h->top = b + sizeof(BlockHeader) + size;
  auto *bh = reinterpret_cast<BlockHeader *>(b);
  bh->cls = cls;
  bh->size = size;
  return reinterpret_cast<void *>(b + sizeof(BlockHeader));
}

inline BlockHeader *block_of(void *p) {
  return reinterpret_cast<BlockHeader *>(reinterpret_cast<uintptr_t>(p) - sizeof(BlockHeader));
}

inline void arena_free(void *p) {
  ArenaHeader *h = header();
  BlockHeader *b = block_of(p);
  void **head = b->cls >= 1000 ? &h->free_large[b->cls - 1000] : &h->free_small[b->cls];
  *reinterpret_cast<void **>(p) = *head;
  *head = p;
  --h->live_blocks;
}

// ---------------------------------------------------------------------------
// Full copy snapshots
struct Snapshot {
  uint8_t *data = nullptr;
  size_t size = 0;
  size_t capacity = 0;
};

inline void snapshot_take(Snapshot &s) {
  size_t n = arena_used_pages() * kPage;
  if (n > s.capacity) {
    __libc_free(s.data);
    s.data = static_cast<uint8_t *>(__libc_malloc(n));
    s.capacity = n;
  }
  std::memcpy(s.data, reinterpret_cast<void *>(kArenaBase), n);
  s.size = n;
}

inline void snapshot_restore(const Snapshot &s) {
  std::memcpy(reinterpret_cast<void *>(kArenaBase), s.data, s.size);
}

// Fill memory above the allocator's top. A correctness probe: if any code read
// uninitialised allocations, restored runs would diverge under different fills.
inline void arena_scrub(uint8_t value, size_t bytes) {
  std::memset(reinterpret_cast<void *>(arena_used_pages() * kPage + kArenaBase), value, bytes);
}

// ---------------------------------------------------------------------------
// Write-tracked restore: protect the snapshot range, record first-touch pages in
// a SIGSEGV handler, and copy back only those pages.
inline bool g_tracking = false;
constexpr size_t kMaxDirty = 1 << 19;
inline uint32_t g_dirty[kMaxDirty];
inline size_t g_dirty_count = 0;
inline size_t g_tracked_len = 0;

inline void on_segv(int sig, siginfo_t *si, void *) {
  auto addr = reinterpret_cast<uintptr_t>(si->si_addr);
  if (g_tracking && addr >= kArenaBase && addr < kArenaBase + g_tracked_len &&
      g_dirty_count < kMaxDirty) {
    uintptr_t page = addr & ~(kPage - 1);
    g_dirty[g_dirty_count++] = static_cast<uint32_t>((page - kArenaBase) / kPage);
    mprotect(reinterpret_cast<void *>(page), kPage, PROT_READ | PROT_WRITE);
    return;
  }
  signal(sig, SIG_DFL);
}

inline void track_install() {
  struct sigaction sa {};
  sa.sa_sigaction = on_segv;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSEGV, &sa, nullptr);
}

inline void track_begin(const Snapshot &s) {
  g_tracked_len = s.size;
  g_dirty_count = 0;
  g_tracking = true;
  mprotect(reinterpret_cast<void *>(kArenaBase), s.size, PROT_READ);
}

inline void track_end() {
  mprotect(reinterpret_cast<void *>(kArenaBase), g_tracked_len, PROT_READ | PROT_WRITE);
  g_tracking = false;
  g_dirty_count = 0;
}

inline void track_restore(const Snapshot &s) {
  std::sort(g_dirty, g_dirty + g_dirty_count);
  for (size_t i = 0; i < g_dirty_count;) {
    size_t j = i + 1;
    while (j < g_dirty_count && g_dirty[j] == g_dirty[j - 1] + 1) ++j;
    size_t offset = size_t(g_dirty[i]) * kPage, len = (j - i) * kPage;
    std::memcpy(reinterpret_cast<void *>(kArenaBase + offset), s.data + offset, len);
    mprotect(reinterpret_cast<void *>(kArenaBase + offset), len, PROT_READ);
    i = j;
  }
  g_dirty_count = 0;
}

struct Scope {
  bool saved;
  explicit Scope(bool active) : saved(g_active) { g_active = active; }
  ~Scope() { g_active = saved; }
};
}  // namespace fm

// Interposed in this executable only. With the arena inactive these are glibc.
extern "C" {
void *malloc(size_t n) {
  return fm::g_active ? fm::arena_alloc(n) : __libc_malloc(n);
}
void free(void *p) {
  if (fm::in_arena(p))
    fm::arena_free(p);
  else
    __libc_free(p);
}
void *calloc(size_t a, size_t b) {
  if (!fm::g_active) return __libc_calloc(a, b);
  void *p = fm::arena_alloc(a * b);
  std::memset(p, 0, a * b);
  return p;
}
void *realloc(void *p, size_t n) {
  if (p == nullptr) return malloc(n);
  if (!fm::in_arena(p) && !fm::g_active) return __libc_realloc(p, n);
  if (fm::in_arena(p)) {
    fm::BlockHeader *b = fm::block_of(p);
    if (n != 0 && n <= b->size) return p;
    if (n == 0) {
      fm::arena_free(p);
      return nullptr;
    }
    void *q = fm::g_active ? fm::arena_alloc(n) : __libc_malloc(n);
    std::memcpy(q, p, b->size);
    fm::arena_free(p);
    return q;
  }
  static const char msg[] = "fm: realloc of a heap block while arena active\n";
  (void)!write(2, msg, sizeof(msg) - 1);
  abort();
}
void *memalign(size_t align, size_t n) {
  if (!fm::g_active) return __libc_memalign(align, n);
  if (align <= 16) return fm::arena_alloc(n);
  static const char msg[] = "fm: over-aligned allocation while arena active\n";
  (void)!write(2, msg, sizeof(msg) - 1);
  abort();
}
void *aligned_alloc(size_t align, size_t n) { return memalign(align, n); }
int posix_memalign(void **out, size_t align, size_t n) {
  *out = memalign(align, n);
  return 0;
}
void *valloc(size_t n) { return memalign(4096, n); }
void *pvalloc(size_t n) { return memalign(4096, (n + 4095) & ~size_t(4095)); }
}
