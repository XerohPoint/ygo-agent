// fm_world.h: the forward model's search-facing interface. This header includes no core or environment
// type, and a search includes only this, so a search cannot name the core, the environment or a card
// object. It holds no search.
//
// Two classes keep the true duel out of a search's reach:
//
//   Referee      The real game. It knows the truth, because something has to play it. It gives out only
//                the acting seat's own observation, and it hands a search a SearchWorld.
//   SearchWorld  What a search gets. Its worlds begin as the referee's position, but nothing about the
//                opponent's hidden cards can be read or stepped until deal() has replaced them.
//
// Typical use, inside one worker process (the arena has a fixed address, so one duel per process):
//
//   fm::init_forward_model({root});            // cwd must hold ./script
//   fm::Referee duel("g_lightsworn", "blackwing", 85100000);
//   while (!duel.done()) {
//     auto world = duel.open_search(duel.to_play());       // refuses any seat but the acting one
//     for (uint64_t i = 0; i < iterations; ++i) {
//       world->restore(fm::SearchWorld::kRoot);
//       if (!world->deal(seed + i, assumption).ok) continue;      // refused: counted, no world
//       fm::Status s = fm::Status::kOk;
//       while (s == fm::Status::kOk) {
//         fm::Observation obs;
//         if (world->observe(&obs) != fm::Status::kOk) break;     // voided: counted, restore next
//         s = world->step(choose(obs));
//       }
//       if (s == fm::Status::kTerminal) record(world->winner());
//     }
//     world.reset();                                          // the true position is back
//     duel.play(chosen);
//   }
//
// Calls that do not return kOk leave the world as stated on Status. Errors in a sampled world (a Lua
// error, a prompt production refuses to show, a crash, a hang) void that world only: the process lives,
// the next restore is exact, and the void is counted.
//
// Not thread safe, and one Referee at a time per process. Workers are processes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fm {

struct ForwardModelConfig {
  std::string root;           // the checkout: assets, code list, build/forward_model/corpus/registry.txt
  std::string registry;       // optional other "name path" list of decks
  int step_cpu_limit_ms = 2000;  // CPU time one core call may use before its world voids
};
// Once per process. The working directory must hold ./script.
void init_forward_model(const ForwardModelConfig &config);

enum class Status : int {
  kOk = 0,      // the call did what it says. For step: the next decision is ready
  kTerminal,    // the duel in this world has ended; read winner()
  kVoid,        // this world failed and was voided. Only restore() is useful now
  kNeedsDeal,   // this world still holds the true hidden cards, so it cannot be stepped. Deal first
  kBadOption,   // the option index is out of range. The world did not change
  kBadHandle,   // no such snapshot
};

enum class VoidReason : int {
  kNone = 0,
  kLuaError,          // the core reported a Lua or script error
  kEnvironmentThrow,  // production threw: a prompt it refuses to show, or a broken contract
  kNativeDiagnostic,  // production printed a marker and kept going, such as "Spec not found"
  kOptionOverflow,    // more options than the observation can hold
  kFatalSignal,       // SIGSEGV, SIGBUS, SIGFPE, SIGILL or SIGABRT inside the call
  kHang,              // one call used more CPU time than step_cpu_limit_ms
  kCount
};

enum class DealRefusal : int {
  kNone = 0,
  kNotViewersDecision,  // the viewer is not the acting seat
  kListMismatch,        // the assumed list cannot be the one in play
  kHiddenObjectState,   // a hidden card carries rule state a slot swap would not transfer
  kCoreReference,       // the pending operation holds a hidden card by pointer
  kFaceDownBanished,    // an unseen opposing face-down banished card would bias the deal
  kForeignOwner,        // a hidden slot holds a card owned by the other player
  kReversedDeck,        // a face-up card inside a Deck
  kPromptBinding,       // an own Deck option that cannot be re-bound
  kNoValidAssignment,   // the assumed cards cannot fill the public slot types
  kCountFromDeal,       // (end of the reasons shared with the determinizer)
  kNotAtRoot,           // deal only from an undealt position: restore a root first
  kCount
};

// What a deal assumes is known about the opponent. Nothing else about their hidden cards is an input.
struct DealAssumption {
  enum class Kind {
    // The caller asserts the opponent's Main Deck is exactly this list. If it cannot be, the deal is
    // refused. Their Extra Deck is left as it is, so the same assertion must hold for it.
    kOpponentListKnown,
  };
  Kind kind = Kind::kOpponentListKnown;
  std::vector<uint32_t> opponent_main;
};

struct DealResult {
  bool ok = false;
  DealRefusal refusal = DealRefusal::kNone;
  int moved = 0;                          // hidden slots re-dealt, including the viewer's Deck
  int known_pinned = 0;                   // hidden-zone cards the viewer knows, left in place
  int opponent_extra_not_redealt = 0;     // opposing face-down Extra Deck cards, untouched
  int facedown_banished_not_redealt = 0;  // face-down banished cards, untouched
};

// Raw observation bytes for the acting seat, as production writes them for that seat.
// Pointers stay valid until the next call on the world or referee.
struct Observation {
  int to_play = 0;
  int n_options = 0;
  const uint8_t *cards = nullptr;      // 160 rows of 41
  const uint8_t *global = nullptr;     // 23
  const int32_t *decision = nullptr;   // 12
  const uint8_t *selection = nullptr;  // 160 rows of 3
  const uint8_t *actions = nullptr;    // n_options rows of 12
  const uint8_t *history = nullptr;    // 16 rows of 14
};

struct WorldStats {
  long restores = 0, snapshots = 0, steps = 0, observes = 0, deals_ok = 0;
  long deals_refused[static_cast<int>(DealRefusal::kCount)] = {};
  long voids[static_cast<int>(VoidReason::kCount)] = {};
};

class SearchWorld {
 public:
  using Handle = int;
  static constexpr Handle kRoot = 0;  // the referee's position, undealt

  ~SearchWorld();  // Puts the true position back for the referee.
  SearchWorld(const SearchWorld &) = delete;
  SearchWorld &operator=(const SearchWorld &) = delete;

  int viewer() const;
  bool dealt() const;   // false means the world still holds the true hidden cards
  bool voided() const;
  VoidReason void_reason() const;
  const char *last_diagnostic() const;

  // A full copy of the duel. A handle remembers whether its world had been dealt. restore() also clears
  // a void, and is exact whatever happened since.
  Handle snapshot();
  Status restore(Handle handle);
  void release(Handle handle);

  // Re-deal the hidden cards from public inputs and the seed, then reseed the future. Allowed only from
  // an undealt, unvoided root at the viewer's decision. Same public history, same seed, same assumption:
  // same world. Refused deals change nothing.
  DealResult deal(uint64_t seed, const DealAssumption &assumption);

  // The public shape of this decision. Before a deal this is the viewer's own decision.
  int acting_seat() const;
  int option_count() const;

  Status observe(Observation *out);
  Status step(int option);

  // Only a dealt world answers. An undealt world is never terminal here.
  bool terminal() const;
  int winner() const;      // 0, 1, 2 for a draw; -1 unless terminal, or on truncation
  int win_reason() const;  // production's info:win_reason
  bool truncated() const;  // ended by production's decision limit, not by the rules

  const WorldStats &stats() const;

 private:
  friend class Referee;
#ifdef FM_TEST_HOOKS
  friend struct TestAccess;  // The test bed's privileged views. Never defined in a search build.
#endif
  struct Impl;
  explicit SearchWorld(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

class Referee {
 public:
  // A seeded duel, played exactly as production plays it: seed_plus_env is the vector environment's
  // seed plus the environment's index.
  Referee(const std::string &deck0, const std::string &deck1, uint64_t seed_plus_env);
  ~Referee();
  Referee(const Referee &) = delete;
  Referee &operator=(const Referee &) = delete;

  bool done() const;
  int to_play() const;
  int option_count() const;
  int winner() const;
  int win_reason() const;
  Observation observe();       // the acting seat's own view of the true duel
  void play(int option);

  // Null unless viewer is the acting seat. While the world is open this referee refuses play().
  std::unique_ptr<SearchWorld> open_search(int viewer);

 private:
  friend class SearchWorld;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fm
