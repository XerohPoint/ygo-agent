# Edison forward-model spike

Measured November 4, 2026 at commit 4e3452c. This decides how M2 search copies and
returns to positions. It does not build search.

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
