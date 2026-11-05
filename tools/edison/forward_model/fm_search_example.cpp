// What a search looks like from outside: this file includes only fm_world.h, so it sees no core or
// environment type. It holds the API's contract checks, then one flat Monte Carlo decision. It is an
// example and a test, not a search.
//
// Usage, from a directory holding ./script: fm_search_example ROOT DECK0 DECK1 SEED_PLUS_ENV OPPONENT_MAIN_FILE
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "fm_world.h"

namespace {
int g_failures = 0;
void check(bool ok, const char *what) {
  printf("%s: %s\n", ok ? "pass" : "FAIL", what);
  if (!ok) ++g_failures;
}
uint64_t next_random(uint64_t &x) {
  uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}
}  // namespace

int main(int argc, char **argv) {
  if (argc != 6) return 2;
  fm::ForwardModelConfig config;
  config.root = argv[1];
  fm::init_forward_model(config);
  fm::Referee duel(argv[2], argv[3], std::strtoull(argv[4], nullptr, 10));
  // Both lists are given, since the acting seat decides which is the opponent's.
  std::ifstream lists(argv[5]);
  std::vector<uint32_t> main_of[2];
  for (int seat = 0; seat < 2; ++seat) {
    int count = 0;
    lists >> count;
    main_of[seat].resize(count);
    for (auto &code : main_of[seat]) lists >> code;
  }
  uint64_t x = 85100091;
  for (int i = 0; i < 24 && !duel.done(); ++i) duel.play(static_cast<int>(next_random(x) % duel.option_count()));

  int viewer = duel.to_play();
  check(duel.open_search(1 - viewer) == nullptr, "only the acting seat can open a search");
  auto world = duel.open_search(viewer);
  check(world != nullptr, "the acting seat can open a search");
  fm::DealAssumption assumption;
  assumption.kind = fm::DealAssumption::Kind::kOpponentListKnown;
  assumption.opponent_main = main_of[1 - viewer];

  check(!world->dealt(), "a new world is undealt");
  check(world->step(0) == fm::Status::kNeedsDeal, "an undealt world cannot be stepped");
  check(!world->terminal() && world->winner() == -1, "an undealt world reports no result");
  fm::Observation before;
  check(world->observe(&before) == fm::Status::kOk && before.to_play == viewer, "an undealt world shows the viewer's own decision");
  std::vector<uint8_t> root_cards(before.cards, before.cards + 160 * 41);
  fm::DealAssumption wrong = assumption;
  wrong.opponent_main[0] = wrong.opponent_main[1] == wrong.opponent_main[0] ? wrong.opponent_main[2] : wrong.opponent_main[1];
  wrong.opponent_main[2] = 89631139;
  fm::DealResult refused = world->deal(1, wrong);
  check(!refused.ok && refused.refusal == fm::DealRefusal::kListMismatch, "a list that cannot be true is refused");
  check(!world->dealt(), "a refused deal leaves the world undealt");

  fm::DealResult first = world->deal(1, assumption);
  bool deal_ok = first.ok;
  check(deal_ok, "the true list deals");
  if (!deal_ok) {
    printf("refusal %d at this position; try another seed\n", static_cast<int>(first.refusal));
    return 1;
  }
  check(first.moved > 0, "a deal moves slots");
  check(world->deal(2, assumption).refusal == fm::DealRefusal::kNotAtRoot, "a dealt world cannot be dealt again");
  fm::Observation after;
  world->observe(&after);
  check(std::vector<uint8_t>(after.cards, after.cards + 160 * 41) == root_cards, "dealing does not change what the viewer sees");
  check(world->step(100000) == fm::Status::kBadOption, "an impossible option is refused");

  fm::SearchWorld::Handle mid = world->snapshot();
  std::vector<uint8_t> at_mid(after.cards, after.cards + 160 * 41);
  fm::Status status = world->step(0);
  check(status == fm::Status::kOk || status == fm::Status::kTerminal, "a dealt world steps");
  check(world->restore(mid) == fm::Status::kOk && world->dealt(), "a handle remembers its world was dealt");
  world->observe(&after);
  check(std::vector<uint8_t>(after.cards, after.cards + 160 * 41) == at_mid, "restore returns to the snapshot");
  world->release(mid);
  check(world->restore(mid) == fm::Status::kBadHandle, "a released handle is gone");
  check(world->restore(fm::SearchWorld::kRoot) == fm::Status::kOk && !world->dealt(), "the root is undealt again");

  // One flat Monte Carlo decision, uniform random playouts, a fresh deal per iteration.
  int options = world->option_count();
  std::vector<int> wins(options), games(options);
  for (int iteration = 0; iteration < 200; ++iteration) {
    int option = iteration % options;
    world->restore(fm::SearchWorld::kRoot);
    if (!world->deal(next_random(x), assumption).ok) continue;
    fm::Status s = world->step(option);
    while (s == fm::Status::kOk) {
      fm::Observation obs;
      s = world->observe(&obs);
      if (s == fm::Status::kOk) s = world->step(static_cast<int>(next_random(x) % obs.n_options));
    }
    // Only a duel the rules finished is evidence. Production's decision limit also ends a world, with no
    // winner, and counting it would score it as a loss. Voids and refusals are skipped the same way.
    if (s == fm::Status::kTerminal && !world->truncated()) {
      ++games[option];
      wins[option] += world->winner() == viewer;
    }
  }
  for (int option = 0; option < options; ++option) printf("option %d: %d wins of %d\n", option, wins[option], games[option]);
  const fm::WorldStats stats = world->stats();
  printf("restores %ld, deals %ld, steps %ld, observes %ld\n", stats.restores, stats.deals_ok, stats.steps, stats.observes);

  world.reset();
  int recorded = duel.option_count();
  check(recorded > 0, "closing the world gives back the true decision");
  duel.play(0);
  check(duel.done() || duel.option_count() > 0, "the referee plays on");
  printf("%d failures\n", g_failures);
  return g_failures ? 1 : 0;
}
