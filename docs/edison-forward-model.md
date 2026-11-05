# Edison forward model

## November 5, 2026: the forward model on the certified rules build

Branch `m2-forward-model`, based on `b27c798`. Nothing here is search. The older spike below keeps its own measurements, labelled as old core.

**Result: the forward model runs on today's certified rules, on all 21 gauntlet lists, and a search can use it through a small header that cannot name the true hidden state.** It matches production at every decision of 672 games. A restore is byte-exact. In R4, the leak test, 3,344 of 3,344 pairs of true worlds with one public history dealt the same hidden cards and offered the same options in canonical order, and 3,319 of 3,344 matched in exact option order and every observation byte. The other 25 differ in option order only. Planted leaks and planted missed rollbacks are all caught. A failure inside a sampled world voids that world only.

**What is not resolved:** the deal still assumes the opposing decklist is known, a refusal can leak one bit, 1.9% of positions refuse every deal, and option order is not fully canonical. See [Still open](#still-open).

### The pin

| Item | Value |
| --- | --- |
| Core package | `5fe29f6a...`, `edison_early_trigger = true`, `edison_hardening = "r2"` |
| Core library | `e3adf73d...` |
| Production module compared against | `b8fcd2bc...`, the certified build |
| Header | `ygoenv/ygoenv/ygopro/ygopro.h` at `b27c798`, unmodified |

`build.sh` finds the package by its manifest and checks the library's hash. It refuses anything else, and it refuses more than one match. `test_build_refusal.sh` shows 6 of 6 cases refused: the old core, r1, no early trigger, no package, the right manifest on the wrong library, and two matches. No production source, core, patch, `xmake.lua` or `tests/edison` file changed.

### Corpus

Production's own heuristic in both seats, through the certified module. Each of the 21 registry lists against its next neighbour, in both seats: 42 cells, 16 games each. Seeds **85100000 + cell × 1000 + env, so 85100000-85141015**. Re-collecting gave byte-identical digests (`f2e25b03...`). Mean 181 decisions per game. The API check also uses seed 85150000, with its own uniform random line. Position sampling uses constants from the same block.

### Production equivalence

| 672 games, 121,919 decisions, 21 lists | Differences |
| --- | ---: |
| Acting player | 0 |
| Option count | 0 |
| Winner, win reason | 0 |
| `cards_`, `global_`, `decision_`, `selection_` | 0 |
| Offered `actions_` | 0 |
| `h_actions_` (the history the spike did not compare) | 0 |
| Games with any difference | 0 |
| Core or Lua diagnostics | 0 |

Every field's sha256 is compared at every decision of every game. `mask_` is not compared, as `oppo_info` is off and production never writes it. Whole-game digests agree across two plain runs, a scrambled heap, the arena allocator, and encoder on or off.

**A real port change:** since the label fix, a history row takes its card id from the id `WriteState` stamps on the action. Stepping with only `msg_` stamped, as the spike did, writes different history. `FmEnv::stamp()` now applies the rule production uses for lone options. Against `WriteState` it gave the same id for every option at every decision: 0 differences.

### Snapshot correctness

The check from [Correctness](#correctness) below, on all 21 lists: every option at each branch point, continued by one seeded random policy to the end, against a straight run in a fresh process. Each digest now also carries the root's own full state, so a missed rollback shows before anything moves.

| Check | Result |
| --- | --- |
| Branch points / options / reference decisions | 504 / 1,650 / 258,274 |
| Truncated, failed positions, diagnostics | 0, 0, 0 |
| Arena restore vs straight run: decision chain, deep hash, every observation byte | **0 mismatches** |
| Options in reverse order | **0 mismatches** |
| Garbage above the allocator top | **0 mismatches** |
| Fork vs straight run; replay repeated; replay in a scrambled heap | **0 mismatches** |
| Return to the position and finish the recorded game | **0 mismatches** |
| Everything outside the core (`env_hash`, below) | **0 mismatches** |
| **Whole arena, byte for byte** | **0 bytes differ in 4.28 GB, over 1,650 comparisons** |

**How rollback of state outside the core is proved, not assumed:**

- The observation chain includes history bytes at every decision.
- `env_hash` folds in both history rings and cursors, reveal memory, the Deck binder's ranks, decision context, multi-select and prompt state, the environment's random streams and the message buffer.
- The whole-arena comparison needs no list of what matters. Two children start from one root. One runs an option straight. The other runs other options first, restores, then runs it. Every byte of every object, live or freed, allocator included, must match.
- Statics: three playouts change one thing in the program's 2.7 MB of `.data` and `.bss`, the test bed's own `g_query_buf` scratch. `check_statics.py` maps changed bytes to symbols and fails on any other. Production calls `new duel`/`delete` directly, so the core's static `duel_set` is never touched.

Write-tracked restore was rejected by the spike and is no longer in this check.

**Negative controls.** Each plants a real miss in the rollback, on 126 branch points, 378 options:

| Planted | Arena mismatches of 378 | Decision | Deep | Observation bytes | Other checks |
| --- | ---: | ---: | ---: | ---: | --- |
| History rings not rolled back | 252 | 0 | 252 | 252 | Reverse 378, return 126, bytes 19 |
| History cursors not rolled back | 251 | 0 | 251 | 250 | Reverse 374, return 125, bytes 375 |
| A stale reveal key left behind | 378 | 0 | 378 | 111 | Reverse 378, return 126 |
| A stale Deck binder rank left behind | 378 | 0 | 378 | 0 | Reverse 378, return 126 |
| Core random stream one step on | 378 | 324 | 378 | 325 | Reverse 378, return 85, 44 positions crashed |

My first reveal, binder and random controls changed nothing, because a finished playout usually leaves that state empty and the duel gone. They passed the check. I rewrote them to plant the state every time, and added the root's state to the digest, which is what catches the binder. The byte comparison alone misses the stale key, the rank and the stream, since later play overwrites them.

### Determinizer hardening

`fm_deal.h` replaces the spike's determinizer.

**Deal inputs.** Slot layout, a public memory, an explicit assumed list, a seed. The dealt codes come from `assumption.opponent_main` minus every opposing Main Deck card outside the dealt slots. The pool is not read from hidden cards. Hidden codes are read only to pick which object carries a code and to refuse. Constrained slots draw first, then the rest is shuffled. Nothing is rejected and retried, so the 2 in 1,080 "could not place" failures are gone. On 59 positions with 4,000 deals each, chi-square for a hand slot's code is 1,381.8 on 1,366 degrees of freedom (ratio 1.012), and 1 position is past 3 sigma.

**What a deal assumes, and what it does not re-deal:**

| Object | Treatment | Reported as |
| --- | --- | --- |
| Viewer's hand, set cards | Known, untouched | |
| Viewer's Deck | Re-dealt, from its own multiset | `moved` |
| Opposing Main Deck | **Assumed known**, `DealAssumption::opponent_main`. A list that cannot be true is refused | `kListMismatch` |
| Opposing Deck, hand, face-down zones | Re-dealt from that list, by public slot type | `moved` |
| Cards the viewer knows, such as a searched card shown and still in hand | Pinned | `known_pinned` |
| Opposing Extra Deck | **Not re-dealt.** Sound only under the same assumption | `opponent_extra_not_redealt` |
| Face-down banished | **Not re-dealt.** An unseen opposing one would bias the pool, so the deal is refused | `facedown_banished_not_redealt`, `kFaceDownBanished` |
| Core and environment random streams | Reseeded | |
| Other seat's history rows | Private card ids zeroed | |

**Pinning now fails closed.** The spike pinned hidden cards that carried rule state or that the core held by pointer, and that read hidden state. Now any such card refuses the whole deal, with a counted reason. Rates over 12,096 deals at 2,016 real roots:

| Outcome | Deals | |
| --- | ---: | --- |
| Dealt | 11,862 | 98.1% |
| Refused: the pending operation holds a hidden opposing card | 150 | Live selection 72, current chain 42, pending chains 36 |
| Refused: a hidden card carries rule state | 84 | |
| Positions where every deal was refused | 39 of 2,016 | 1.9% |

A refused position gives a search no sampled world. What a search does then is its decision: this is a limit on coverage. Refusing reads hidden state, so it can leak one bit. R4 counts that below.

**R1-R5 on all 21 lists,** 2,016 positions (every game, three stages), 6 deals each, 0 crashed positions:

| Test | Result |
| --- | --- |
| R1. Reorder both Decks: playouts completed | 12,096 of 12,096. Same seed reproduces all |
| R2. Full re-deal: dealt | 11,862 of 12,096 |
| R2. Playouts completed | 11,862 of 11,862. Same seed reproduces all |
| R2. **Viewer's own observation changed by the deal** | **0 of 11,862**, including 870 own Deck options re-bound |
| R1, R2. Invariant failures, diagnostics, exceptions | 0 |
| R2. Cost per deal | 90 µs (spike: 39 µs) |
| R3. Re-deal vs natural duel, same assignment: board | 1,453 of 1,453 |
| R3. Forward chain, canonical order | **1,453 of 1,453** |
| R3. Winner | 1,453 of 1,453 |
| R3. Exact option order and every viewer observation byte | 1,312 of 1,453 |
| R3. Alternate-history acceptance | 1,672 of 5,825: early 52%, mid 19%, late 16%. 8.8 ms each |
| R4. Pairs (two true worlds, one public history, one deal seed) | 3,344 |
| R4. **Hidden slots equal** | **3,344** |
| R4. **Forward chain, canonical order** | **3,344** |
| R4. Viewer's observation at the root | 3,344 |
| R4. All observation bytes, next 8 viewer decisions | 3,336 |
| R4. Exact option order, viewer's bytes to the end of the game | 3,319 (99.25%) |
| R4. Every seat's bytes to the end of the game | 3,319 |
| R4. One world refused, the other dealt | 0 of 3,344 here. 2 of 776 in a second sample |
| R4. Control, no re-deal: chains equal / viewer bytes equal | 24 / 23 of 1,672 |
| R5. Cards outside the true list: playouts completed | 12,060 of 12,060, 131 µs. 70% of those cards moved in play |

Two worlds count as one public history only if every public zone, life total, card shown to the viewer, viewer's prompt and **every byte of each viewer observation before the root** agree. The second sample used other stages of the same games (504 positions): 776 pairs, 774 equal slots, 774 canonical, 771 exact, and its 2 refusals are one position, a hidden card carrying rule state in one world (`hidden_object_state`). So the refusal bit is real and rare.

**What R4 cannot see.** It compares worlds with the same list and the same opposing Extra Deck, so it says nothing about either assumption. It tests refusal parity only where the first world deals. Accepted worlds lean early, as the rates above show.

**The planted leaks.** 200 pairs each, against 100% equal slots on the clean build:

| Planted | Slots equal | Canonical chain | All bytes | Root bytes |
| --- | ---: | ---: | ---: | ---: |
| Deal keeps the true opposing hand | 42 | 48 | 44 | 200 |
| Deal's seed mixed with the true opposing hand | 42 | 45 | 44 | 200 |
| Deal's pool keeps the true slot order | 0 | 4 | 2 | 200 |

Root bytes stay equal in all three because the viewer's own view contains no hidden card, so that row alone proves nothing. I did not analyse the 42. That both hand leaks leave exactly 42 suggests those positions have an empty opposing hand, which I did not confirm.

**The two undiagnosed differences.** R3 game 57 index 11 and R4 game 28 index 150 are on seeds I may not replay (the 70M block), so **neither was re-run**. The same class of difference did recur on the new corpus, and its cause is found: **the environment's own random stream, which decides the End Phase random discard.**

- The rig's reset does not draw from it as `reset()` does, so natural and swapped worlds held different streams. The R3 chain matched 1,430 of 1,444.
- With one stream given to both worlds, it matches 1,453 of 1,453.
- The old deal reseeded only the core's stream, so two R4 worlds kept different ones too. The deal now reseeds both.

That this explains those two old cases is likely and not verified.

**What R4 found on the way.** Each of these would have been a real leak through prompts:

- **Effect order.** The core lists same-code effects in registration order, so a dealt card kept the place of where it truly was. All hidden slots now re-register in slot order.
- **Effect ids.** Offered effects sort by id, and an id says when a card arrived. A dealt card kept its true arrival. It now takes the slot's (`take_slot_effect_ids`). My first two fixes were wrong: R4 dropped to 95.8% on one, which is how I found that ids are arrival times.

Found by reading, with no failing test first:

- **Public memory.** The spike pinned a "seen" card wherever it was, so a seen card drawn back into a hidden hand stayed pinned there, which is true hidden state. A card now stays tracked only while no hidden event could have moved it, judged from public counts alone, and a card entering a Deck stops being tracked. Forgetting costs a little knowledge. Keeping it would leak the true move.
- **Reveals between decisions.** A message tap now reads `MSG_CONFIRM_CARDS` and `MSG_CONFIRM_DECKTOP` as they pass. This more than doubled what deals know: 216 pinned cards against 96 over the same 2,976 deals.
- **The other seat's history.** That ring holds private ids of the true world, such as what it set. In a sampled world they are zeroed unless the card was public when the row was written: 44,352 ids over 11,862 deals.
- **The Deck binder.** Own Deck options name a real position, which a deal invalidates. They now re-bind by production's own rule, and the viewer's bytes confirm it above.

### Failure inside a sampled world

Each plant fires only while Heavy Storm is in hand during a Summon, and the chosen game never does that on its recorded line. So the failure cannot occur in the true duel, only in sampled worlds that deal Heavy Storm to the opposing hand (`g_diva_frog` vs `g_hero_beat`, seed 85132000 env 0, decision 145). Three runs in fresh processes, 512 seeds each: all seeds in order, then only the clean seeds, then only the voided seeds.

| Plant | Voided as | Voids | Finished | Clean-run digest differences | Voids reproduced alone | True game finishes after |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `error()` in Lua | `kLuaError` | 39 | 473 | 0 of 473 | all | yes |
| A prompt production refuses (opposing Deck) | `kEnvironmentThrow` | 39 | 473 | 0 | all | yes |
| Endless loop in Lua | `kHang` | 39 | 473 | 0 | all | yes |
| Runaway recursion in Lua | `kLuaError` | 39 | 473 | 0 | all | yes |
| Invalid write inside the guard | `kFatalSignal` | 32 | 480 | 0 | all | yes |
| `abort()` inside the guard | `kFatalSignal` | 32 | 480 | 0 | all | yes |
| Production's `Spec not found` line | `kNativeDiagnostic` | 32 | 480 | 0 | all | yes |
| **Control:** history rollback missed, Lua plant | `kLuaError` | 39 | 473 | **473 of 473** | all | yes |

The digest chains every observation byte and the privileged deep hash at every decision of every finished world. So later worlds in a run that voided are identical to a run where nothing ever failed. Segfault and abort are planted through a test hook, as no real core crash is at hand. The hang limit is CPU time, 2 s by default (1 s here), through a timer that costs no system call per step.

**Why this works:** every byte a duel owns is in the arena, so any way out of a core call leaves nothing a restore cannot put back: a return, a C++ throw, or a `siglongjmp` from a signal handler through Lua and the core. The only other state is stdio, pinned to static storage. A failure of the true duel is not a void: the referee throws.

### Search API

`fm_world.h` includes no core or environment type. `fm_search_example.cpp` includes only it. Its 20 contract checks pass, and 8 attempts to name a core type, an environment type, the internals or the test hooks fail to compile, against a control that compiles.

| Need | Call |
| --- | --- |
| Build from a seeded duel | `Referee(deck0, deck1, seed_plus_env)`, `play`, `observe` (acting seat only) |
| Hand a position to search | `Referee::open_search(viewer)`: null unless the viewer is acting |
| Snapshot, restore | `SearchWorld::snapshot`, `restore`, `release`, `kRoot` |
| Deal | `deal(seed, DealAssumption)`, returning `DealResult` with refusal and counts |
| Raw observation for the acting seat | `observe(&Observation)` |
| Step an option | `step(option)` |
| Terminal, winner | `terminal`, `winner`, `win_reason`, `truncated` |
| Void | `voided`, `void_reason`, `last_diagnostic`, `stats` |

**How the true state stays out of reach.**

- A `SearchWorld` has no way back to its `Referee`. The referee refuses to play while a world is open.
- A world starts undealt. In that state it refuses `step` (`kNeedsDeal`), reports no result, and shows only the viewer's own decision, which R2 shows is unchanged by a deal.
- There is no call to read a card, a code or a hidden zone.

This guards against accident, as asked, and it is C++: a determined caller could still `reinterpret_cast`.

One worker per process, as before. A world is not thread safe. Sampled opposing observations come from the sampled world with scrubbed history, so they are not what production would show that seat in a true duel.

### Throughput

Through the search API, on a shuffled mix of real roots, 15 s per cell, Ryzen 7 9800X3D under WSL2. **Another agent was running. Load average was 1.0-1.8 across the run. Treat absolutes as ±30%.**

| Workers | Nodes/s per worker | Nodes/s total | Node, µs | Restore | Deal | Step | Observe |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 3,397 | 3,397 | 294 | 66 | 77 | 34 | 117 |
| 2 | 3,205 | 6,409 | 312 | 69 | 81 | 36 | 125 |
| 4 | 3,134 | 12,536 | 319 | 72 | 82 | 37 | 127 |

A node is restore + deal + one step + encode. Full random playouts with a deal and the encoder at every decision: 54 / 50 / 49 per worker on 1 / 2 / 4 workers, 158-162 decisions each, about 8,000 decisions/s per worker.

The older playout bench, no deal, on the new lists: 198 / 204 / 202 playouts/s per worker without the encoder (old core 348 / 326 / 319), 49 / 49 / 47 with it (old core 60 / 56 / 54). The lists differ, so this is no clean comparison. Stepping with exact history now costs an id lookup per option, which is my likely explanation for most of the no-encoder drop, unmeasured.

### Still open

- **The known-list assumption.** A real opponent's list is not known. R5 shows cards outside the list work mechanically, and the API does not offer it. A wrong list is refused, so a caller passing a guess learns one bit per call.
- **The refusal bit.** A deal refused for a hidden card's rule state tells the caller something true about hidden state. Seen in 2 of 4,120 pairs, one position. A search should treat a refusal as carrying no signal.
- **1.9% of positions refuse every deal,** mostly a prompt or chain that holds an opposing hidden card, such as a target among set cards. Following the slot instead of the object may recover many of these. I did not try it.
- **Option order is not fully canonical.** 25 of 3,344 pairs differ. All 25 first differ only in the order of `actions_` rows, at a prompt offering Special Summon (18) or Activate (7), with `cards_` and history still equal there. Some are copies of one code and some are different cards. Chains are identical once options are sorted. I did not find the cause of what remains. A policy reading list position could read true-world history there. Sort at the search boundary until this is closed.
- **Behaviour.** Deals respect public facts only. They ignore what an opponent's passes imply, and that a card just shuffled in is in the Deck, or that a set Level 7 needs Tributes.
- **Scripts holding hidden cards.** Label objects on another card's effect refuse the deal. Label groups are ignored, as Summon procedures leave them stale. References in Lua tables and the stale `operated_set` are not detected. None showed in R3 or R4.
- **Reveals.** The tap reads a batch's first message, which by source reading is where those two messages always land. Not proven. A Deck slot is pinned only while its reveal key lives, which production clears on any move.

### Not verified

- **Refusal paths never reached on these lists:** face-down banished (0 such cards at 2,016 roots), foreign owner, reversed Deck, prompt binding, no valid assignment. `kListMismatch` is exercised only by the API check. `kOptionOverflow` has no test. All are by reading.
- **Pairs:** 42 of 420 ordered list pairings. Each list is in 4 cells.
- **Playouts** are uniform random in sampled worlds. The heuristic and the network were not run inside one.
- **Other stage mixes.** Positions are 10/50/90% of game length, which is what the sampler draws from.
- **glibc reliance.** The marker check reads `stdout`'s private write pointer. Recovery from `abort()` relies on glibc's recursive abort lock.
- **A fatal signal inside a glibc call** would jump with its lock held. The guard runs in the arena, so none occurred, and none was planted.
- **More than 4 workers, and native Linux.**
- **Not rerun on this build:** `run_costs.sh` and `run_allocator_compare.sh` (fork, replay and memory tables). Candidate D was rerun: 380 of 400 reached idle, board 380, exact action set 25 (7%), 135 (36%) ignoring phase actions, 900 wrongly offered and 2 lost. It still fails.

### Reproduce

Link the git-ignored assets, then run everything. About 30 minutes on 4 threads, and the build does not use xmake:

~~~bash
W=/path/to/worktree
ln -s /home/mada/ygopro-scripts "$W/scripts/script"
cp -p /home/mada/ygo-agent/ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so "$W/ygoenv/ygoenv/ygopro/"
mkdir -p "$W/assets/locale/en"
ln -s /home/mada/ygo-agent/assets/locale/en/cards.cdb "$W/assets/locale/en/cards.cdb"
bash "$W/tools/edison/forward_model/test_build_refusal.sh"
bash "$W/tools/edison/forward_model/run_all.sh"
~~~

Output goes to `build/forward_model/`. `collect_corpus.py` refuses a module other than `b8fcd2bc...` and seeds outside 85100000-85199999. Logs name each refusal, difference and byte. Results of this run are in [results-2026-11-05](../tools/edison/forward_model/results-2026-11-05/).

### Files

Under `tools/edison/forward_model/`:

| File | Purpose |
| --- | --- |
| `fm_world.h`, `fm_world.cpp` | The search API, and the guard around every core call |
| `fm_search_example.cpp`, `run_api.sh` | API contract checks and compile refusals |
| `fm_deal.h` | Determinizer, public memory, re-binding, the planted faults |
| `fm_hidden.h` | R1-R5, uniformity, candidate D |
| `fm_env.h` | Production subclass, stamp, hashes, sha256, message tap |
| `fm_bench.cpp` | Commands: `verify`, `correct`, `redeal`, `uniform`, `void`, `node`, `statics`, `deepdiff`, `construct`, and the older `replay`, `fork`, `forkmem`, `arena`, `throughput`, `fork-conc` |
| `arena.h` | Allocator and snapshots, unchanged |
| `collect_corpus.py` | Production games and digests, replacing `collect_games.py` and `check_observations.py` |
| `check_statics.py` | Maps changed static data to symbols |
| `build.sh`, `test_build_refusal.sh` | The pinned build, and proof it refuses others |
| `run_all.sh`, `run_verify.sh`, `run_correct.sh`, `run_controls.sh`, `run_void.sh`, `run_throughput.sh` | Reproduction |

---

# Spike on the old core

**Everything below this line was measured on November 4, 2026 at commit 4e3452c, on the pre-hardening core `bedb42dd...` and 3 fixture decks.** It is kept for its decision and its record. Its numbers do not describe the current build unless the section above repeats them. Its reproduce steps need that commit, as `collect_games.py` and `check_observations.py` are gone.

This decides how M2 search copies and returns to positions. It does not build search.

## Decision

**Use an in-process arena snapshot (candidate E, added by this spike), one worker
process per thread.** All duel memory, core and Lua alike, is allocated from one
fixed-address mapping, so a snapshot is a memcpy of about 1.9 MB. It needs no core or
Lua patch and no knowledge of the core's structures.

| | Approach | Node cost, p50 | Playouts/s per worker | Correct? | Verdict |
| --- | --- | ---: | ---: | --- | --- |
| E | Arena snapshot | 102–128 µs | 319–348 | 0 mismatches | **Recommended** |
| B | `fork()` | 631–696 µs | 228–245 | 0 mismatches | Fallback, and only from a small process |
| A | Replay from seed | 6.0–7.8 ms | 121–131 | 0 mismatches | Use to build roots, not nodes |
| C | Structural clone | not built | | | Ruled out: harder, and E gives the same result |
| D | Construct from description | 4–6 ms (estimated; not timed) | | **Fails** | Ruled out as a forward model |

Three findings matter beyond that table:

1. **The observation encoder, not the engine, is the bottleneck.** `WriteState` takes
   about 150 µs per decision against 10–17 µs for the engine step. It cuts arena
   throughput by a factor of 5.7 before any network runs.
2. **Hidden state can be re-dealt in place.** A deal takes 39 µs, ran 1078 random
   playouts to completion with no failures, and matched natural duels on all the
   checks below except one unexplained divergence in 119. Small leak channels remain.
3. **Nothing here loads a position from a real external duel.** Replay needs the true
   seed and the opponent's private responses, which only exist in our own simulated
   games. Construction fails. M6 needs its own solution.

**Supervisor rerun.** The code was copied to the main tree and rebuilt with `collect_games.py`,
`build.sh`, `run_verify.sh` and `run_correct.sh`. All correctness counts reproduced: 330 branch
points, 1038 options and 171,524 reference decisions with 0 mismatches, plus the same two logged
differences (`game=57 index=11` and `game=28 index=150`). In that output R3's canonical match is
117 of 119, and 118 of 119 when compared by `cardid`. The table below reports the latter.
Throughput was not rerun.

## Setup

- **Games:** 192 production heuristic-vs-heuristic duels, 32 for each of 6 cross-deck
  pairings of the legal fixture decks. Seeds 70000000 + cell × 1000 + env. Mean 163
  decisions per game, range 46–468. Mean 3.14 offered options per decision; 55.5% of
  decisions offer at most 2 and 1.6% offer at least 10.
- **Positions:** one per game at each stage, 10% (early), 50% (mid) and 90% (late) of
  game length, ±5%. Mean decision index 16, 81 and 146.
- **Test bed:** a native binary that subclasses the unmodified `YGOProEnvImpl` and links
  the production core package (`edison_early_trigger = true`). It reproduces production
  (see Correctness).
- **Machine:** Ryzen 7 9800X3D, **8 physical cores, 16 threads**, under WSL2. Other
  agents were running (load average 4–8), and the same replay measured 60–98k
  decisions/s across repeats. **Treat absolute numbers as ±30%.** Runs used at most 4
  workers.
- Playouts use a uniform random policy unless stated otherwise.

## Results

Latencies are p50 / p90 / p99 in µs, all from one worker.

| Operation | Early | Mid | Late |
| --- | ---: | ---: | ---: |
| **A. Replay** | | | |
| Create duel, load scripts, first prompt | 5707 / 7639 / 12823 | same | same |
| Replay the action prefix | 199 / 450 / 1293 | 1057 / 2236 / 3645 | 1871 / 3829 / 6570 |
| Node total (create + prefix + 1 step + destroy) | 6017 / 7992 / 13311 | 6921 / 9416 / 14913 | 7826 / 11016 / 15936 |
| **B. Fork** (parent RSS 10 MB) | | | |
| Bare fork + exit + wait | 346 / 455 / 718 | 347 / 456 / 771 | 368 / 510 / 781 |
| Node (fork + step + pipe + wait) | 631 / 840 / 1097 | 661 / 908 / 1162 | 696 / 923 / 1142 |
| **E. Arena** | | | |
| Take full snapshot | 62 / 91 / 857 | 66 / 99 / 972 | 67 / 101 / 1025 |
| Restore full snapshot | 67 / 126 / 184 | 76 / 153 / 201 | 80 / 160 / 250 |
| Node (restore + step) | 102 / 177 / 262 | 114 / 240 / 301 | 128 / 248 / 485 |
| Node, write-tracked restore | 343 / 589 / 729 | 481 / 910 / 1319 | 571 / 998 / 1451 |

| Mean cost per playout, one position | Early | Mid | Late |
| --- | ---: | ---: | ---: |
| Random playout length, decisions | 237 | 167 | 74 |
| Arena, including restore | 5.2 ms | 4.2 ms | 2.2 ms |
| Fork, including fork, pipe and wait | 8.3 ms | 7.0 ms | 4.1 ms |

Throughput over a shuffled mix of all stages, 16 playouts per root, including building
each root by replay (5–6 ms; 13 ms with the encoder). Each cell ran for 15 seconds, and
every method averaged about 160 decisions per playout. Load average was 1.6–3.5.

| Method | Workers | Playouts/s, total | Playouts/s per worker | Decisions/s, total |
| --- | ---: | ---: | ---: | ---: |
| Arena | 1 / 2 / 4 | 348 / 652 / 1275 | 348 / 326 / 319 | 55.9k / 105.6k / 206.7k |
| Arena with production encoder | 1 / 2 / 4 | 60 / 112 / 215 | 60 / 56 / 54 | 9.8k / 18.5k / 35.3k |
| Fork, separate parents | 1 / 2 / 4 | 245 / 477 / 912 | 245 / 239 / 228 | 39.1k / 76.2k / 145.7k |
| Fork, 1 parent, N children | 1 / 2 / 4 | 235 / 427 / 674 | 235 / 214 / 169 | 37.6k / 68.0k / 108.4k |
| Replay every playout | 1 / 2 / 4 | 131 / 248 / 485 | 131 / 124 / 121 | 21.0k / 40.1k / 78.0k |

So one arena worker simulates about 52–56k decisions/s and 319–348 full random playouts/s.
With the encoder that falls to about 9–10k decisions/s and 54–60 playouts/s. Fork loses
more with one parent than with separate parents, so each fork worker should own its parent.

An earlier throughput run is **discarded**. It walked positions in stage order, so short
runs stayed inside one stage (99–238 decisions per playout), and it ran under heavier load.
It gave arena 303 / 291 / 223 per worker. Its output is still in `costs.jsonl` if
that file has not been regenerated.

| Memory per snapshot | Early | Mid | Late |
| --- | ---: | ---: | ---: |
| Arena, full copy | 1.8 MB | 1.9 MB | 1.9 MB (max 2.2 MB) |
| Arena, pages dirtied by one step, mean | 40 (160 kB) | 43 (171 kB) | 51 (204 kB) |
| Arena, pages dirtied by a playout, mean | 400 of about 450 | 427 of about 480 | 425 of about 480 |
| Fork, private dirty after one step, mean | 235 kB | 272 kB | 315 kB |
| Fork, private dirty after a playout, mean | 1973 kB | 2010 kB | 1955 kB |
| Fork, page tables per live child | 68 kB | 68 kB | 68 kB |

For 200 stopped children, private dirty was 91, 145 and 314 kB per child by stage.
`MemAvailable` deltas were dominated by other agents and are not usable.

### Findings per candidate

**A. Replay is correct, and duel creation dominates its cost.** About 6 ms of the node
is creating the duel (Lua state, scripts, cards), and the prefix adds about 17 µs per
decision. It stays the simplest way to build a root, and restoring a snapshot taken
after creation would remove that 6 ms (derived, not measured).

**B. Fork is correct, but its cost grows with the parent's memory.** A bare fork took
0.35 ms at 10 MB RSS, 5.7 ms at 215 MB and 35–42 ms at 1 GB. It cannot fork from a
Python or JAX process and would need a small dedicated server. Fork, copy-on-write faults
and the pipe add about 2–3 ms per playout over the arena. Its advantages are that it is trivially
correct and it isolates crashes.

**E. Arena restore is cheap because the state is small.** A full copy is 67–80 µs. It
works because every pointer in the snapshot stays valid at the fixed address, and the
allocator's own state rolls back too, so later allocations land at identical addresses.
Constraints:

- One active duel per process, since the address is fixed. Hence one process per thread.
- Snapshot only between `process()` calls, when no Lua call is on the C stack. Every
  decision point qualifies.
- Malloc interposition suits a dedicated executable. It cannot go into the production
  Python module without `LD_PRELOAD` or allocator hooks in the core and Lua.

**Write-tracked restore failed and is rejected.** Tracking dirty pages with `mprotect`
and `SIGSEGV` was 3–4× *slower* than copying everything, because one step dirties 40–51
pages and each fault costs several µs here. It was correct, just slow, and a playout
dirties nearly every page anyway. Delta snapshots could still save memory for shallow
tree nodes (about 160–200 kB against 1.9 MB). That is untested.

**C. Structural clone: ruled out by source reading, not prototyped.** It would need:

- a Lua state clone, which Lua 5.5 does not offer (`lua_dump` covers only functions,
  without upvalues), including suspended coroutines, registry function references and
  userdata;
- deep copies with pointer fixups for cards, effects and groups, including `multimap`
  iterators stored in `effect_indexer`;
- type knowledge for the untyped `void*` fields `processor_unit::ptr1`–`ptr4`, with 61
  uses across `processor.cpp` and `operations.cpp` and 114 `add_process` sites in
  `processor.cpp`.

A missed field would diverge silently. My judgment is weeks of work plus permanent
maintenance, for the same state E copies in 70 µs. Its only advantage is several live
duels in one address space, which sequential per-worker search does not need.

**D. Construction fails as a forward model.** I rebuilt 400 sampled real idle prompts
through `new_card` and `set_player_info`, as the fixtures do. Optional chain windows
were declined on the way to the prompt, and seats were swapped so the acting player has
the first turn.

| Of 374 positions that reached an idle prompt | Count |
| --- | ---: |
| Board identical | 374 (100%) |
| All monster ATK identical | 365 (98%) |
| Offered actions exactly equal | 30 (8%) |
| Equal, ignoring phase actions | 171 (46%) |
| Same, in the easy subset: no summon used, no equip/target/counter relations | 157 of 186 (84%) |

The other 26 of the 400 sampled never reached an idle prompt.

The missing state is what the API cannot express. The rebuilt positions wrongly offered
712 actions: 228 repositions, 225 summons, 225 monster sets and 34 activations. They
lost 6 real activations. Beyond that:

- **Can inject:** cards, location, sequence, position, LP.
- **Cannot inject through the API:** turn number and phase (Turn 1 has no Battle Phase),
  normal-summon and reposition use, set-turn status, once-per-turn counters, equip and
  target relations, ongoing effects with targets, a chain in progress, and any non-idle
  prompt.

Writing these fields directly is possible but amounts to rebuilding C, and this does not
test it.

## Correctness

The check runs **every option** at each branch point. Each is continued by the same
seeded random policy to the end of the game, then compared with a straight run in a
fresh process with no snapshots. The comparison uses a hash chain over each decision
plus a privileged deep hash: all cards with full query data, deck order, future RNG
output and processor counters.

| Check | Result |
| --- | --- |
| Branch points / options / reference decisions | 330 / 1038 / 171,524 |
| Playouts truncated by the 3000-decision cap | 0 |
| Fork vs straight run | **0 mismatches** |
| Arena, full restore vs straight run | **0 mismatches** |
| Arena, options in reverse order | **0 mismatches** |
| Arena, write-tracked restore | **0 mismatches** |
| Arena, different garbage above allocator top (uninitialised-read probe) | **0 mismatches** |
| Return to position, finish real game as recorded, same result | **0 mismatches** |
| Replay repeated | **0 mismatches** |
| Replay in a deliberately scrambled heap | **0 mismatches** |
| Core/Lua diagnostics | 0 |

**The test bed is the production environment:**

- All 192 games reproduce the production trace's acting player, option count, winner and
  win reason at every decision (31,275 decisions).
- Native observation bytes match the production trace's sha256 on 5520 of 5520 decisions.
  This covers only the 32 games of cell 0.
- Whole-game digests are identical across separate processes, a scrambled heap, the
  arena allocator, and encoder on or off. Deep hashing does not change decisions.

**A caveat for replay only.** The core has pointer-keyed ordered and unordered containers
(`quick_f_chain`, `delayed_quick`, `reseted_effects`, `pheff`), and six script files use
`pairs()`, whose order can depend on addresses or Lua's per-state seed. None of this
caused a divergence on these decks, but other cards might. Snapshots preserve addresses
and are immune.

## Hidden state

Search must never read true hidden state, so a sampled world must depend only on public
inputs and a seed. The prototype determinizer (`fm_hidden.h`) re-deals the viewer's deck
order and the opponent's deck, hidden hand and set cards. It then reseeds the core RNG,
because the true stream encodes future shuffles and coin flips.

The core mechanism is a **slot permutation**. A slot keeps its rule state (how it got
there, set-turn status, field ids, position). Only the card object occupying it changes,
with effects cancelled and re-applied around the move. Monster and spell/trap set zones
keep their public type.

| Test | Result |
| --- | --- |
| R1. Reorder both decks only: playouts completed | 1080 of 1080 |
| R2. Full re-deal: deals that succeeded | 1078 of 1080; 2 could not satisfy set-zone types in 500 tries |
| R2. Playouts completed | 1078 of 1078 |
| R1 and R2. Invariant failures, diagnostics, exceptions, crashes | 0 |
| R1 and R2. Same seed reproduces the same playout | all |
| R2. Cost per deal | 39 µs |
| R3. Re-deal vs natural duel, same assignment: board | 119 of 119 |
| R3. Forward decision chain, canonical prompt order | 118 of 119 |
| R3. Winner | 119 of 119 |
| R3. Exact prompt order | 101 of 119 |
| R4. Two true worlds, same public history, same deal seed: hidden slots equal | 168 of 168 |
| R4. Forward chain equal, canonical order | 167 of 168 |
| R4. Forward chain equal, exact prompt order | 157 of 168 |
| R4. Control, no re-deal: forward chains equal | 2 of 168 |
| R5. Cards outside the true decklist: playouts completed | 1080 of 1080, 0 failures, 85 µs per deal |
| R5. Materialised cards that moved during play | 2011 of 3240 (62%) |

**R3 is the oracle.** It builds the same assignment as a natural duel by rigging the
initial deck and replaying history. The re-deal matches it, including on rule state the
slot swap had to reconstruct. One sample diverges at decision 292 of 315 with the same
length and winner (game 57, index 11). **That divergence is unexplained.**

**R4 tests for leaks directly.** It found one: my first determinizer pinned cards listed
in `core.select_cards`, which stays populated after a prompt ends, so the deal depended on
the opponent's earlier private candidates. Slot equality was 100/108 before that fix and
is now 168/168. Three residuals remain:

- **Prompt order:** in 11 of 168 pairs the exact order differs. List order follows
  `cardid` and similar internal ordering, so a policy reading list position could pick up
  true-world information. Sort prompts canonically at the search boundary.
- **One canonical difference** (game 28, index 150), in a pair whose physical layouts had
  already diverged. Not diagnosed.
- **Pinning reads hidden state:** a hidden object carrying rule state (relations,
  counters, extra effects) is pinned, and that check reads it. It happened 6 times in
  68,948 moves. Production code should fail closed here rather than pin.

**Alternate-history replay is not a usable sampler.** Even with rigged decks, acceptance
was 168 of 540 (31%): early 47%, mid 25%, late 21%. Replaying raw indices survived 103 of
540 (19%). Each accepted sample costs about 8 ms. It also has two deeper problems:

- It uses the opponent's private responses, which a real engine does not have, so these
  rates are upper bounds.
- 338 of 372 rejections came from opponent-side or sequence prompt differences, which are
  not public contradictions. These rates therefore measure the limits of this replay, not
  how many deals are impossible.

A legal version must be driven by public events. That was not built.

### What re-dealing cannot do yet

- **Decklist knowledge.** R2 deals the true multiset, which is fair only for these known
  fixtures. An unknown opponent list needs materialisation. R5 shows that works
  mechanically, but script `global_check` trackers would start mid-duel without history,
  and the replaced object stays allocated at location 0.
- **Consistency with the opponent's behaviour.** Deals respect public facts only. They do
  not reason that an opponent who passed probably lacks a response.
- **Reveal memory.** Public tracking scans at decisions and uses production's known-imperfect
  reveal memory. A card public only between decisions could be missed.
- **Lua references.** Scripts holding hidden cards in `LabelObject` or Lua tables are not
  detected. Moved cards in a `card_set` could also break its ordering when `cardid` is
  transferred. Neither surfaced on these decks.
- **Not re-dealt:** the opponent's Extra Deck, and face-down banished cards.

## How many playouts per decision

**The 16-thread figures are extrapolated.** Only 1, 2 and 4 workers were measured, and the
machine has 8 physical cores. The range runs from 8 × to 16 × the 4-worker rate, assuming
SMT adds between nothing and a full core. Network inference is excluded, as requested.

| Budget | Random playouts, no encoder | Playouts with production encoder |
| ---: | ---: | ---: |
| Rate | 2.6k–5.1k per second | 430–860 per second |
| 1 s | 2.6k–5.1k | 430–860 |
| 10 s | 26k–51k | 4.3k–8.6k |
| 60 s | 150k–310k | 26k–52k |

Spread over the mean 3.14 options, the sampling error of a win rate near 50% (0.5/√n) is:

| Budget | No encoder | With encoder |
| ---: | ---: | ---: |
| 1 s | 1.2–1.8 pp | 3–4 pp |
| 10 s | 0.4–0.6 pp | 1.0–1.4 pp |
| 60 s | about 0.2 pp | 0.4–0.6 pp |

**Is that enough?** Statistically yes for root ranking: about 10 seconds resolves
differences of a few points even with the encoder, and pairing worlds across options
should help further. It is not enough for a deep tree at 1 second. **Whether the estimates
mean anything is untested:** uniform random playouts may be a poor evaluator of Edison
positions, and this spike measured cost, not playing strength.

Truncated rollouts look like the better use of the budget. Restore + step + encode is
about 280 µs, or roughly 28k–57k encoded nodes per second across the machine (derived).
Speeding up the encoder is the highest-value engineering after the transport.

## Search recommendation

**Start with PIMC, with a fresh deal per iteration, then add a learned belief. Defer full
ISMCTS.**

- **PIMC first.** A deal plus restore is about 120 µs against 2–5 ms per random playout, so
  per-iteration sampling is affordable and needs no world reuse. It is the simplest thing
  that can be tested against the M2 exit criterion.
- **Keep the path to ISMCTS open.** Single-observer ISMCTS uses this same transport and
  deal step, so nothing here locks it out. Its extra machinery is information-set keying,
  which is hard with 3.14 mean options but about 160 decisions per game and many multi-step
  prompts. I would add it only if PIMC's known weaknesses appear in measured losses, namely
  strategy fusion and undervaluing information-gathering or bluffing. This is reasoning from
  the literature, not a measurement here.
- **Learned belief next.** Uniform deals ignore what the opponent's actions imply, and the
  alternate-history result shows that filtering by replay is too costly and illegal as
  built. A belief model over the public stream, used to weight or generate deals, is the
  practical route. It needs the M1 network, so it comes later.

## Not verified

- Scaling beyond 4 workers; contention on the machine could be better or worse than the
  range above.
- Any deck other than the 3 fixtures (67 card identities). The correctness and hidden-state
  results apply to these only.
- Playouts driven by the heuristic or a network. The heuristic is Python and was not run in
  playouts; the encoder was, the network was not.
- Multi-level tree search with nested snapshots, and moving a snapshot between processes.
  That needs identical code addresses, so workers must fork from one parent.
- Whether the sampled deal distribution is uniform. Rejection on set-zone types may bias it.
- Full certification was not rerun, because no runtime source changed.
- Native Linux. Fork and page-fault costs here are WSL2's and may differ.

## Reproduce

Set up the git-ignored files in a checkout of 4e3452c, reusing the main checkout's assets:

~~~bash
W=/path/to/worktree
ln -s /home/mada/ygopro-scripts "$W/scripts/script"
cp -p /home/mada/ygo-agent/ygoenv/ygoenv/ygopro/ygopro_ygoenv*.so "$W/ygoenv/ygoenv/ygopro/"
mkdir -p "$W/assets/locale/en"
ln -s /home/mada/ygo-agent/assets/locale/en/cards.cdb "$W/assets/locale/en/cards.cdb"
~~~

Then run, in order. Output goes to `build/forward_model/`, and the build does not use xmake:

~~~bash
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 /home/mada/ygo-agent/.venv/bin/python \
  "$W/tools/edison/forward_model/collect_games.py"     # 192 games, about 10 s
bash "$W/tools/edison/forward_model/build.sh"          # native test bed, g++ directly
bash "$W/tools/edison/forward_model/run_verify.sh"     # trace and digest checks
bash "$W/tools/edison/forward_model/run_costs.sh"      # latency and memory; about 2 min
bash "$W/tools/edison/forward_model/run_throughput.sh" # throughput; about 4 min
bash "$W/tools/edison/forward_model/run_correct.sh"    # correctness, R1-R5, construction
bash "$W/tools/edison/forward_model/run_allocator_compare.sh"  # replay noise check
~~~

Results: `costs.jsonl`, `costs-encode.jsonl`, `throughput.jsonl`, `correct.jsonl`,
`results.jsonl`.
`redeal.stderr` lists any divergence, and it holds the two lines cited above.

## Files

Under `tools/edison/forward_model/`:

| File | Purpose |
| --- | --- |
| `arena.h` | Fixed-address allocator, malloc interposer, snapshots, write tracking |
| `fm_env.h` | Production environment subclass, hashes, game loading |
| `fm_bench.cpp` | Commands: `verify`, `obsdump`, `replay`, `fork`, `forkmem`, `arena`, `throughput`, `fork-conc`, `correct` |
| `fm_hidden.h` | Determinizer, tests R1–R5 (`redeal`), candidate D (`construct`) |
| `collect_games.py` | Runs production duels, exports traces |
| `check_observations.py` | Compares native observations with production sha256 |
| `build.sh`, `run_*.sh` | Build and reproduction scripts |

No game rule, production source, core package or lock changed. The test bed reaches
production's private helpers with `#define private protected`, confined to `fm_env.h`, and
the malloc interposer exists only in the `fm_bench` executable. Production's `step()` reads
`msg_` values that `WriteState` stamps onto legal actions, so code that steps without
encoding must stamp them first, as `FmEnv::act` does.
