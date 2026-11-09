Sol's independent recount of the M2 final test, copied from its own worktree at commit 0123ea7 on 2026-11-09. Links to `recount/` point into that worktree and are not committed; the small summaries beside this file are copies of some of them.

Independent M2 recount, 9 November 2026. Worktree `sol-m2-recount`, detached at `0123ea73fa1efa10d8017bb4a04555e870da5d3b`.

**Final status: COMPLETE. I accept PASS under the frozen M2 protocol.** The 30 search and 30 plain games reproduced at both worker counts; the full 64-game ranker gate reproduced; opening hands and actual closing boards were inspected; and the optional fresh-seed calibration test completed. The sole saved-gate numerical difference is 1.11e-16 accumulation rounding, which disappears with the original arithmetic.

The initial database-blocked report is preserved below as audit history. Its pending statements are superseded by the appended execution results and final acceptance.

The missing input is `assets/locale/en/cards.cdb`, also absent from `build/`. Its required SHA-256 is `4c5de5831fade7601d55092dec594d5aab220b4321bf9dddbc3707a45df4000d`. I asked for a path outside other worktrees or permission to copy only this input read-only from `m2-search`. I have not accessed another worktree. All new code and evidence are under `recount/`; this report is the requested exception. Tracked files remain unchanged.

I classify findings **REACHABLE** when observed in records or applicable active source paths, **TEST** when the proposed effect/check remains unverified, and **PLANTED-ONLY** when evidence depends on an intentionally broken build. A reachable mechanism does not establish its contribution to the gain.

**REACHABLE: independent scoring reproduces the result.** I read the two requested documents first, then wrote and ran my scorer before opening `m2-final-result.json`, `m2-final-analyze.log` or `m2-cross-summary.json`. It neither imports nor copies `m2_final.py` or `search_common.py`. The preserved pre-summary implementation is [independent-blind.py](recount/independent-blind.py). [independent.py](recount/independent.py) includes a later correction to the supplementary calibration conversion; scores, intervals and integrity checks were unaffected. Initial outputs are preserved with `blind-` prefixes.

From the deck inventory I independently generated the 21 sorted names, each list's next four cyclic neighbours, then all reversed pairs: 168 ordered blocks. Each block has three nets and two search seats, or three plain games. Every expected file exists: 1,008 search and 504 plain. No extras or exclusions. File names, ids, full job lines, seeds, nets, physical seats/decks, budget 64, depth 48, four candidates, margin 1.0, halving, search seeds and mode match the generated design. Every record names frozen binary id `d35e8512ba2cce6f`. Trace lengths and move-index bounds check out.

My scorer computes search minus paired plain score, averages within each block, then averages blocks. Its percentile intervals use 20,000 whole-block resamples with `numpy.default_rng(87000000)`.

| Subset | Search score | Paired plain | D, points | 95% block interval for D |
| --- | ---: | ---: | ---: | --- |
| Overall | 80.258% | 50.000% | 30.258 | [27.778, 32.639] |
| n1702 | 78.571% | 50.000% | 28.571 | [24.702, 32.440] |
| n1703 | 84.226% | 50.000% | 34.226 | [30.655, 37.798] |
| n1704 | 77.976% | 50.000% | 27.976 | [23.810, 31.845] |
| Seat 0 | 84.722% | 53.968% | 30.754 | [25.595, 35.913] |
| Seat 1 | 75.794% | 46.032% | 29.762 | [24.405, 34.921] |

There are 809 search wins of 1,008, no draws. Rules 1–5 pass on the independently reconstructed data. The conservative worst-case lower bound is also 27.778 points because there are no exclusions.

My blind scorer initially drew search, plain and D intervals; the experiment draws D then search. Initial differences (blind versus saved) were overall D upper 32.7381/32.6389 points; overall score upper 82.6389/82.7381%; n1704 D upper 32.1429/31.8452 points; n1704 score upper 81.8452/82.1429%; seat 1 D upper 34.925595/34.920635 points; seat 1 score lower 71.423611/71.428571% and upper 80.158730/79.960317%. Seat 0 score upper also differed by 1.11e-16. **Matching the protocol's statistic order makes every saved bootstrap number match exactly.** These were draw-order differences, not different outcomes. The full list is [bootstrap-differences.json](recount/bootstrap-differences.json).

Every saved per-seat, per-net, per-search-deck score, headline counter, CPU average and calibration row also matches. [final-comparison.json](recount/final-comparison.json) has empty numerical/reporting difference lists. This claim excludes the not-yet-executed ranker numbers.

**REACHABLE: pairing and seat accounting.** All 1,008/1,008 search traces match their paired plain game before the first logged search departure: moves, acting seats and option counts. The cross run matches on 336/336. There are no prefix exceptions. Logged departures equal game departure counters. Every one of the 69,427 final and 24,037 cross search keys equals that seat's own decision count, including forced steps; none uses the opponent's private count.

Reading every plain game from both seats gives exactly 50%, as required. A single seat's baseline is not forced to 50%: final seat 0 is 53.968%, seat 1 is 46.032%. The recorded first actor is seat 0 in every game. Search is balanced across physical seats, and both gain substantially. The native constructor receives the job's `deck0, deck1, seed`; search-seat selection does not swap decks.

**TEST: opening hands are not recorded in the raw files.** Source inspection shows identical decks and seed enter the same reset before search is opened, so I found no path that changes the initial deal or first player for search. Matching prefixes supports this but cannot establish hand equality in games departing immediately. A separate privileged replay inspector has been compiled to record both hands and verify whole-game CRCs. It has not run without the database. I do not count source reading as the requested empirical hand check.

The fixed neighbour design covers 168 of 420 distinct ordered list pairs, with equal list/net/seat weights. Sorted labels can cluster archetypes. The gain is meaningful on this fixed design, not a universal matchup claim. No outcome-based final-job selection or flattering pairing imbalance was found.

**REACHABLE: available provenance checks pass.** HEAD is the manifest-only commit directly on named parent `919108a5a3898c578162fbd039c2afd183c164fa`. All 12 frozen Python source hashes match. Binary SHA-256 is `167be8aa8f9a4767789c3e81dc719051dd41c32afc3678d9cf6172d307a5548f`; its executed `id` command agrees with the records. All three nets, codes, 12,354 top-level Lua scripts and the combined deck-list digest match. The two absent filtered lists were regenerated inside `recount/decks/`, independently of the experiment's Python imports; the combined list hash matches exactly.

Regenerated final jobs hash to `024aacaed3e00d5ae24b5b2bbe0d51a824bd8595f36dbf08e20dab7d95a88675`, exactly the manifest. Both final and both cross `.inputs` sidecars match. Each directory has one matched start/end attempt pair, the independently recomputed input/job identity, zero return code, expected started count and zero worker failures. No interruptions, deaths, root refusals, net errors, option mismatches or overruns are recorded. The database hash alone is unverified because the file is missing. Hashes validate copied evidence, not its honest generation; reruns are still necessary.

Every original registry deck path points through `/home/mada/ygo-agent/.claude/worktrees/m2-search/`, including the two generated lists. The job lines name lists symbolically and depend on those paths indirectly. My runner uses a local registry and local hash-matching lists. A recount-local runtime root contains code/database paths; its script symlink reads `/home/mada/ygopro-scripts`, whose digest matches. No original-worktree writes are needed. See [hashes.json](recount/hashes.json).

**TEST: required frozen-job reruns remain outstanding.** [runtime.py](recount/runtime.py) prepares an independent 30-search/30-plain draw using `random.Random(86609117)` over sorted filenames, and reruns the same sample with one worker and four. It compares every field except inherently variable CPU timings, including CRC, moves, winner, counters, search logs, seats and option counts. **Zero complete frozen games have actually been rerun.** Setup stopped before workers started because of the missing database. Only the frozen binary's `id` command executed.

**REACHABLE: the closing selection makes 63/63 plausible.** I regenerated the protocol's 64-game draw and two middle positions per game, in its exact job order with `random.Random(87000000)`. Fifty-one drawn games were won by search. In **all 64**, the winner's closing real choice is the game's final recorded decision, with no subsequent recorded choice before the game ends. Fifty of the 51 search-winner closings already have an original budget-64 log estimate of at least 80%. See [gate-design.json](recount/gate-design.json) for every id and position.

The winner/last-choice selection explicitly reads the outcome, as the protocol requires. The ranker receives only decks, seed, prefix moves and net, not the winner or later moves. No extra outcome-based game filter was found. These are positions immediately before the actual win, not an unbiased late-game calibration sample. A stronger winning line, different cohort and budget 256 versus the earlier table's budget 64 all weaken the 58/63 versus 63/63 comparison. Under hypothetical independent fixed-probability trials, 63/63 has probability about 36.5% at p=62/63, or 0.55% at p=58/63; those common-population/independence assumptions are not established here.

**TEST: ranker execution and actual card positions remain unchecked.** The independent runner reconstructs all 64 closing plus 128 middle rankings, recomputes means/errors from raw samples, runs every compared option Alone, follows best/worst branches through forced steps, and repeats rankings/branches. It additionally checks ten games from my reproduction draw. None has executed without the database. The saved 63 high/63 top, middle counts 73/48 and gap 0.38242891053945044, 34 branches/33 agreeing, eight abstentions, 668 Alone options, zero differing options and exact repeats remain unverified execution claims. I have inspected their raw decision locations, not replayed card/board positions. Rule 7 cannot be certified from source alone.

**REACHABLE: terminal accounting does not explain the gain.** Native reported reason 1 means ordinary LP exhaustion, -1 deck-out. A decision-limit ending becomes VOID and is excluded, not awarded a win. No true decision-limit endings, draws or other win reasons appear in either raw run.

| Final-run population | Games | LP endings | Deck-outs | Mean decisions | Median | Min–max | 95th percentile |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| Search wins | 809 | 802 | 7 | 124.40 | 111 | 30–548 | 244.6 |
| Plain opponent wins in search games | 199 | 197 | 2 | 146.14 | 124 | 28–618 | 318.0 |
| Plain baseline games, one outcome each | 504 | 494 | 10 | 147.63 | 125.5 | 32–666 | 301.4 |

Both-seat baseline accounting yields 494 LP wins and ten deck-out wins, each matched by a loss. Excluding deck-out search games leaves search scoring 80.280%, essentially unchanged. These lengths count decisions, not turns.

**REACHABLE: refusal causes differ.** I reproduce 5,482/69,427 fallbacks (7.896%), 43,856 refusals = 5,482 × 8. The vector is 43,624 `kCoreReference` refusals (5,453 positions), 224 `kHiddenObjectState` (28 positions in three games), and eight `kForeignOwner` (one position). Core references occur in 790 games. Object-state games are `g41_n1702_1`, `g75_n1704_1`, `g91_n1704_1`; the foreign-owner game is `g150_n1703_1`.

Core references are pending selections, chains, processors or running scripts holding hidden card objects that a slot swap cannot safely retarget; this includes an unseen own-Deck object. Object-state means nontransferable hidden-object rule state such as equips, counters, relations or added effects. Foreign-owner means a hidden slot contains the other player's owned card. These checks precede sampling; changing the seed cannot repair them. The raw vector cannot identify each exact pointer category/card state. The compiled inspector is prepared to collect those details, not yet executed.

**TEST: no supported direction for the gain without refusals.** Bypassing guards would create invalid sampled states, not measure better legal search. A corrected sampler could improve decisions or introduce harmful departures. Hidden-state-dependent refusal also makes search availability selective. Neither counts nor conditional outcomes identify whether removing refusals would help. I have not assumed it would.

**REACHABLE: guard fires are step events, not failed rollouts.** The 2,665,281 events across 4,017,620 rollouts are not a 66.3% failure rate or a distinct-rollout count. The counter increments at a rollout network decision when that guard has any tried option at the observation key, possibly repeatedly in one rollout and even without changing the otherwise best option. This is about 1.66% of 161,032,305 rollout net calls. Tried options are excluded until all have been tried; the true policy uses the same general mechanism. True-game guard events total 1,927 in search games and 1,173 in plain games. Voids, truncations, net errors and step overruns are separately zero. The size warrants attention to loops/horizon effects, but is not an integrity failure itself.

**TEST: privacy coverage is bounded.** The documented search privacy stage uses budget 48/depth 6, fixed keys and a bounded rigged corpus, not the complete gated policy at budget 64/depth 48. My full key recount addresses the actual play-loop key choice. It does not establish equality across every hidden timeline or load order. `scrub_other_seat_history` zeros private card ids but preserves other history features/cursors. R4W's documented rig rejects worlds requiring different non-decline private actions. A natural equal-view pair with such different private choices and different later search answers is a missing test; I have not demonstrated it, so this is not a proven M2 leak.

Lua-table references and some stale groups also remain untracked. The documented Return from the Different Dimension load-order channel applies to `g_diva_hero`, `g_vayu_turbo`, `g_vayu_turbo_ddev08`: search can know which public monster the engine prompts first, information a player would not ordinarily know. This is not evidence of reading a hidden card. Excluding every game involving those lists leaves 732 games/122 blocks: search 79.645%, plain 50%, D 29.645 points. It cannot explain the whole gain. This exclusion is descriptive and post hoc.

Rollouts let each sampled seat see its sampled own hand, use the search's net for both sides and assume the opposing list is known. I found no direct true-opponent-hand read in `pimc.h`. That interface boundary does not prove privacy of the determinizer, refusal availability or retained engine state. Cross-net success weakens an exact-own-net-only exploit explanation, but all three nets share architecture/training. No human or other-family opponent was tested.

**TEST: engine legality remains bounded.** Every move index is offered by the engine and diagnostics are clean; that is not proof of historical legality. I checked current `known_failures.json`, rather than treating the stale conformance matrix's since-fixed failures as live. None of these lists contains both Dandylion and Monster Reincarnation, or contains Necroface, the named unlisted Unions, Trap Monsters, Fake Trap, Major Riot or Pole Position. Thus the current documented returned-trigger/Union/Trap Monster/loop cases have no direct listed-card route here. I did not rerun conformance or manually adjudicate every played line. Unknown quirks and network-specific responses remain hypotheses, not demonstrated causes.

**REACHABLE: calibration treats abstentions as estimates.** `search_study.calibration` filters on candidate count, not completed deals/fallback. A fully refused search has zero means; `(mean+1)/2` inserts a fictitious 50% estimate. The 50–60% bin contains 5,485 zero/zero rows: the 5,482 fallbacks plus three additional real neutral cases. Three games have one extra zero row above their fallback count; I do not call those extra fallbacks.

| Final middle bin | Decisions | Mean estimate | Actual game score |
| --- | ---: | ---: | ---: |
| Saved calculation, reproduced | 11,689 | 52.693% | 80.195% |
| All zero rows, including three real cases | 5,485 | 50.000% | 77.284% |
| Conservatively remove all zero/zero rows | 6,204 | 55.074% | 82.769% |
| Filtered, equal weight per game | 751 games | 55.283% | 82.690% |

The quoted row is contaminated, but underestimation persists and game-length weighting alone does not explain it. Future network play in rollouts versus continued actual search is plausible, not proven by this table. Value-head error, uniform-deal beliefs, retained private history, chosen-option selection and truncation can also matter.

**TEST: a causal calibration experiment is prepared, not run.** It uses 60 new search games on 86610000–86610059 (search RNG seeds 86620000–86620059), with deck/net/seat templates drawn without consulting locked outcomes. One genuine middle-bin decision per eligible fresh game is selected without conditioning on its winner. After fixing its actual chosen move, both seats continue as the plain net; this is compared to continued search from the same fresh game. Such a test can attribute part of the gap to continued search, within its limited sample. It is descriptive, separate from the locked score. No new experiment has used an 87M game seed, and none of these fresh games has run yet.

**REACHABLE: cross recount matches.** I regenerated 336 search/336 plain jobs on `87100000 + block` with the pilot's rotating search net, next-net opponent and correct net-to-physical-seat mapping in plain games. Every file, job, build id and attempt identity matches; no voids, draws or exclusions. Search wins 267/336 (79.464%), paired plain 170/336 (50.595%), D 28.869 points. Both-seat plain scoring is still 50%; its selected comparison seats need not be.

Every saved cross-summary field matches, including its Wilson interval 74.82–83.44%. Whole-block resampling with the main protocol's RNG seed/order gives D [22.619, 35.119] points and score [75.298, 83.631]%. Wilson versus block endpoints are method differences, not inconsistent outcomes. The duel block is 87100000; this explicit bootstrap retains the protocol's 87000000 RNG seed. Evidence: [cross-summary-check.json](recount/cross-summary-check.json), [cross-comparison.json](recount/cross-comparison.json).

Cross totals: 24,037 searches; 3,810 departures; 1,915 fallbacks; 15,320 refusals (15,304 core reference, 16 object state); 1,389,962 rollouts; 929,703 guard events; zero failed/truncated worlds or net errors. Search wins have 263 LP endings/four deck-outs; its 69 losses all end by LP. Plain baseline endings are 331 LP/five deck-outs. Mean/median decisions: 132.50/116 for search wins, 142.04/124 for losses, 146.54/134.5 for plain games. Removing all 1,917 zero rows from cross's middle bin leaves 2,505 decisions, 55.077% estimated versus 78.363% won.

**PLANTED-ONLY: historical mutation results were not rerun.** The documented hand/seed/order leaks, wrong-option rankers, sign flips and failure-isolation controls concern deliberately broken builds. I do not count their historical results as independent evidence from this audit or allege those faults in the frozen executable.

**Executed versus outstanding.** Completed: recrawls of all 2,184 records; independent job design, scoring and block bootstraps; protocol-order comparison; metadata/log checks; every saved non-ranker field comparison; source review; list reconstruction; available hashes; prefix/seed-key, reason/length, refusal, guard, calibration and deck-exclusion analyses; compilation of a separate replay inspector. Outstanding because the database is absent: all frozen-game reruns, empirical hand comparisons, ranker/Alone/Branches and causal calibration follow-up. The inspector is not a replacement frozen executable.

No game/search worker has run, so none of the requested two-hour worker allowance has been consumed. Linux analysis and the one-process inspector compilation ran under nice 19; the Windows bootstrap was analysis only. The prepared runner executes sequential phases with one or four workers and applies `nice -n 19` to every native invocation. A full frozen replay is intentionally omitted for load: the original final search records alone total about 16 CPU-hours, exceeding two hours at four workers. Broad new opponent families, a fresh privacy corpus and conformance rebuilds are also omitted under the load/fixture budget. No omission is described as a passed check.

With a matching database at `recount/cards.cdb`, the prepared Linux command is `nice -n 19 python3 recount/runtime.py`; the separate recorded-trace inspection command is `nice -n 19 python3 recount/inspect_records.py`. Neither is validated end to end yet because setup correctly refuses the missing input. Actual execution, timings and any skipped phases must be appended before an independent PASS decision.

**Acceptance:** the raw statistics and copied evidence are consistent with PASS. I do not yet accept PASS as independently reproduced: the requested minimum frozen-game and ranker checks have not run. The calibration issue warrants a reporting correction; it does not change the paired game gain.


Continuation after the database was supplied (9 November 2026): I independently verified the read-only copy as SHA-256 `4c5de5831fade7601d55092dec594d5aab220b4321bf9dddbc3707a45df4000d`. All manifest input hashes now match. Frozen-job reruns began at 21:47:26 UTC, with the shared two-hour deadline at 23:47:26 UTC. The results below supersede the earlier missing-database status as each phase completes.


Execution update 2026-11-09T22:14:06.756945+00:00 ? frozen reruns.

**REACHABLE: frozen-job reproduction.** My independently drawn 30 search and 30 plain games were each rerun at one and four workers. Every non-CPU field was compared, including the exact field set, CRC, moves, winner, search/engine counters, logs, seats and counts.

| Kind | Workers | Games | Wall seconds | Non-CPU differences | Exit code |
| --- | ---: | ---: | ---: | ---: | ---: |
| search | 1 | 30 | 1247.663 | 0 | 0 |
| plain | 1 | 30 | 1.670 | 0 | 0 |
| search | 4 | 30 | 344.855 | 0 | 0 |
| plain | 4 | 30 | 0.497 | 0 | 0 |

Evidence: [rerun-sample.json](recount/rerun-sample.json), [rerun-results.json](recount/rerun-results.json), exact jobs and native logs under `recount/rerun-*`. CPU times were deliberately excluded; no other fields were ignored. The four-worker outputs are separately compared to the frozen originals, so they also verify scheduling independence against the one-worker outputs.


Execution update 2026-11-09T22:23:28.087876+00:00 ? ranker.

**REACHABLE: independent ranker re-execution.** First I checked ten games from my own reproduction draw in full, then all 64 games in the original protocol draw. Root rankings use budget 256/depth 48, original prefixes, the acting net and the prescribed default search seed. Means/errors were independently reconstructed from samples, every compared option was run Alone, eligible best/worst branches were followed through forced steps, and root/branch calls repeated.

| Measure | Independent ten-game sample | Full 64-game gate | Saved full gate |
| --- | ---: | ---: | ---: |
| closing | 10 | 63 | 63 |
| closing_high | 9 | 63 | 63 |
| closing_top | 10 | 63 | 63 |
| middle | [11, 9] | [73, 48] | [73, 48] |
| middle_gap | 0.10954245835186016 | 0.38242891053945033 | 0.38242891053945044 |
| numbers_broken | 0 | 0 | 0 |
| branches | 3 | 34 | 34 |
| branches_agree | 3 | 33 | 33 |
| abstained | 0 | 8 | 8 |
| options_alone | 122 | 668 | 668 |
| options_differ | 0 | 0 | 0 |
| ranker_failures | 0 | 0 | 0 |
| repeats_exactly | True | True | True |

Every difference from the saved gate, including floating-point differences: `[{"field": "middle_gap", "saved": 0.38242891053945044, "recount": 0.38242891053945033, "absolute_difference": 1.1102230246251565e-16}]`. The ten-game sample is a separate draw and its aggregate counts are not expected to equal the saved 64-game counts. Its middle coverage and separation are below the full gate thresholds; I use it to check re-execution consistency. Acceptance uses the separately re-executed, prespecified 64-game cohort. Evidence: [sample-ranker-summary.json](recount/sample-ranker-summary.json), [gate-summary.json](recount/gate-summary.json), [gate-saved-differences.json](recount/gate-saved-differences.json), plus full samples, Alone outputs and repeated branch outputs under `recount/gate-*`.


The full gate arithmetic audit covered 1,196 native outputs, 6,068 option outputs and 170,975 rollout samples across roots, repeats, Alone and branch calls: zero nonfinite, range, sample-count, mean or standard-error problems. The only saved-gate numerical difference is the middle gap: Linux Python 3.10 ordinary summation gives 0.38242891053945033 versus saved 0.38242891053945044. NumPy mean and `math.fsum` give the saved value exactly. This 1.1102230246251565e-16 discrepancy is accumulation rounding, not a different draw, rollout or position. Evidence: [numeric-audit.json](recount/numeric-audit.json), [middle-gap-arithmetic.json](recount/middle-gap-arithmetic.json).


**REACHABLE: opening hands, first player and actual closing boards.** The separate inspector replayed 279 recorded games, including the 30 rerun search games, their paired plain games, the independent 30 plain games and both search-seat counterparts, all 64 gate games and selected rare-refusal cases. It compares whole-game CRC, winner, reason, decisions, move indices, actors, option counts and refusal categories against the records. Inspector errors: `[{"folder": "m2-final-search", "id": "g112_n1703_1", "errors": [["refusal_reasons", {"4": 504}, [0, 0, 0, 0, 496, 0, 0, 0, 0, 0, 0, 0]]]}, {"folder": "m2-final-search", "id": "g31_n1702_1", "errors": [["refusal_reasons", {"4": 408}, [0, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0]]]}, {"folder": "m2-final-search", "id": "g31_n1703_0", "errors": [["refusal_reasons", {"4": 560}, [0, 0, 0, 0, 552, 0, 0, 0, 0, 0, 0, 0]]]}]`. Paired initial hands and first actor agree in 156/156 pairs. Full summary: [inspection-summary.json](recount/inspection-summary.json).

All 63 estimated closing positions are captured as actual board states in [closing-boards.md](recount/closing-boards.md) and [closing-board-states.json](recount/closing-board-states.json). This is an inspection replay of the existing jobs, not new scored games. These five examples were selected at fixed indices 0, 12, 25, 38 and 50 in protocol order:

| Game / decision | Actor; LP 0 / 1 | Action | Seat 0 field | Seat 1 field | Played estimate |
| --- | --- | --- | --- | --- | ---: |
| g3_n1702_0 / 96 | 1; 1600 / 8600 | Direct attack with Elemental HERO Stratos | face-down S/T | Elemental HERO Stratos (attack, 1800/300); Jinzo (attack, 2400/1500) | 99.489423% |
| g24_n1703_0 / 81 | 0; 7000 / 1100 | Direct attack with Goyo Guardian | Five-Headed Dragon (defense, 5000/5000); Blue-Eyes White Dragon (attack, 3000/2500); Goyo Guardian (attack, 2800/2000); Torrential Tribute; Cards of Consonance; Future Fusion | face-down S/T; face-down S/T | 96.237330% |
| g61_n1703_0 / 29 | 0; 8000 / 2000 | Direct attack with Caius the Shadow Monarch | Caius the Shadow Monarch (attack, 2400/1000); Machina Fortress (attack, 2500/1600); Machina Fortress (attack, 2500/1600) | face-down S/T | 98.186109% |
| g104_n1703_1 / 119 | 1; 1200 / 8000 | Direct attack with Elemental HERO Stratos | empty | Elemental HERO Stratos (attack, 1800/300); Bottomless Trap Hole | 100.000000% |
| g137_n1703_1 / 100 | 0; 3700 / 2000 | Direct attack with Machina Fortress | Super-Nimble Mega Hamster (defense, 1100/1800); Machina Fortress (attack, 2500/1600); Machina Fortress (attack, 2500/1600); Solemn Judgment | empty | 100.000000% |

The frozen binary was also called after each recorded closing move: 64/64 end immediately with a nontruncated result. These checks establish the actual terminal character of this selected cohort; they do not make the closing gate an unbiased estimate of general ranking accuracy. Further interpretation follows after reviewing the card states.

Observed refusal subcategories in the inspected cohort: `{"(4, 'live selection')": 168, "(4, 'processor unit')": 260, "(4, 'label object')": 218, "(4, 'pending chain list')": 33, "(4, 'current chain')": 64, "(4, 'script group')": 7, "(6, '')": 1, "(3, 'added effect')": 28, "(4, 'battle or summon pointer')": 1}`. These are positions, whereas the original refusal counters count eight failed deal attempts per fully refused position.


**Inspector accounting correction.** I initially counted three hypothetical deal probes at positions where the actual repeat guard left only one candidate: `[{"id": "g112_n1703_1", "decision": 120, "compared": 1, "hypothetical_refusal": 4}, {"id": "g31_n1702_1", "decision": 55, "compared": 1, "hypothetical_refusal": 4}, {"id": "g31_n1703_0", "decision": 233, "compared": 1, "hypothetical_refusal": 4}]`. The real search returns before dealing at those roots. Restricting the refusal comparison to actually attempted comparisons removes all three apparent eight-refusal discrepancies. The native replay traces themselves already matched; no game output or frozen counter was changed. Corrected result: **279/279 whole-game replays match, 156/156 paired openings match, and zero inspector errors**. All 279 initial states have engine first player 0, first actor 0, six cards in seat 0 after its first-turn draw and five in seat 1. The initial miscounts are retained in `targeted-inspection.execution.log`; the corrected comparison is [inspection-summary.json](recount/inspection-summary.json).

**REACHABLE: actual-board interpretation.** The five fixed-index examples above are ordinary immediate lethal direct attacks: Stratos 1800 into 1600 LP; Goyo 2800 into 1100; Caius 2400 into 2000; Stratos 1800 into 1200; Fortress 2500 into 2000. Multiple examples also have overwhelming spare attackers. The full 64 recorded closing moves all end immediately in the frozen binary with the recorded winner, including the one root whose deals refuse. The 63 estimated roots range down to 80.5127%, so they are not all assigned a constant 100%. High estimates and top ranks are plausible for this selected terminal class. The source draw does not read winners beyond eligibility; winner/last-choice position selection is explicit in the protocol, and no future moves or winner labels are passed to the rank command.

**REACHABLE: a concrete opponent-network weakness, rather than an illegal line.** In the independent ten-game cohort, the one closing below 80% is `g59_n1702_0`, decision 109, estimated 78.5233%. It is an End turn decision seven recorded choices before the game ends, unlike every closing in the original 64-game cohort. Search had set Honest in defense. On the next turn the plain opponent attacks Honest with an 1100-ATK Gorz token, dropping from 1500 to 700 LP against Honest's 1900 DEF, then attacks the now face-up Honest with 800-ATK Vayu and loses. The last attack is predictably fatal from public stats. This game is in the 30-game frozen sample and reproduces at both worker counts. It illustrates a legal network reaction pattern that need not transfer to human opponents, and why the terminal character of the original gate cohort matters. It does not establish how much of the full gain this pattern causes.

The narrow inspected-trace census of final attacks into a sole, already face-up defender whose DEF exceeds ATK by at least the attacker's remaining LP is preserved in [post-inspector-analysis.json](recount/post-inspector-analysis.json). It finds one example in the selected inspected games. This cohort is partly selected for ranker positions, so that count is descriptive, not a population rate or a causal attribution.

Actual refusal subcategories in the inspected cohort are {"(4, 'live selection')": 168, "(4, 'processor unit')": 260, "(4, 'label object')": 215, "(4, 'pending chain list')": 33, "(4, 'current chain')": 64, "(4, 'script group')": 7, "(6, '')": 1, "(3, 'added effect')": 28, "(4, 'battle or summon pointer')": 1}. All 28 object-state positions from the three rare games are specifically **added effect** refusals; no equip/counter cause was needed to explain those 224 attempts. The foreign-owner position is also reproduced. Core-reference subcategories include live selections, processor units, label objects, pending/current chains, script groups and a battle/summon pointer. Each counted actual refused position corresponds to eight attempts in its frozen record.

The inspector also identifies the three genuine neutral zero estimates that were conservatively removed with the fallback placeholders earlier. Removing only the 5,482 actual fallbacks leaves 6207 middle-bin estimates, averaging 55.071315%, with actual score 82.745288%. The calibration reporting defect and remaining underestimation are unchanged in substance.


The paired plain baseline for `g59_n1702_0` is won by seat 1, so this example contributes D = +1 to search's paired total. The winner's End turn at decision 109 is followed by six opposing decisions; the final Vayu attack is decision 115. This is evidence of one reachable, reproducible model-specific win, not a measurement of its frequency in all 1,008 games.

Across all 1,008 raw final games, 986 winner-last-real-choices are also the final recorded decision (791/809 search wins and 195/199 plain-opponent wins). A uniform 64-game draw has a 23.249% chance of containing only such immediate closings, calculated without replacement from that finite population. Thus the gate's 64 immediate closings are not an anomalous draw. Of its 63 estimated moves, 59 are direct attacks, two attacks, one activation and one other selection. This gives concrete support for the easy terminal-class explanation of 63/63, without needing an extra outcome-dependent selection mechanism. It does not establish that the earlier cohorts had the same class mix. Evidence: [closing-population.json](recount/closing-population.json). The board artifact marks own set cards explicitly and masks opposing hidden identities.


**TEST completed: causal calibration follow-up on fresh seeds.** Sixty new search games used only 86610000-86610059; search RNG seeds were 86620000-86620059. Deck/net/seat templates were drawn independently of locked outcomes. One non-refused 50-60% position was selected per eligible game before consulting its winner. The move already played was fixed; the frozen binary then replayed that prefix and continued both seats greedily without search. This preserves the state and changes future policy. These outcomes do not enter the locked experiment.

| Quantity | Result |
| --- | ---: |
| fresh_games | 60 |
| fresh_OK | 60 |
| fresh_search_wins | 44 |
| eligible_selected_positions | 46 |
| completed_continuations | 46 |
| valid_pairs | 46 |
| truncated | 0 |
| mean_estimate | 0.5532467391304348 |
| continued_search_score | 0.7608695652173914 |
| plain_future_score | 0.5869565217391305 |
| paired_gain_from_continued_search | 0.17391304347826086 |
| continued_search_minus_estimate | 0.2076228260869566 |
| plain_future_minus_estimate | 0.033709782608695726 |
| search_only_wins | 11 |
| plain_only_wins | 3 |
| paired_exact_two_sided_sign_p | 0.057373046875 |
| paired_game_bootstrap_95 | [0.021739130434782608, 0.32608695652173914] |

The percentile interval resamples these paired fresh games, using `random.Random(86609120)`, 20,000 resamples. The exact paired sign calculation uses discordant deterministic outcomes. This is an exploratory, selected-position experiment with one position per game, not a substitute for a broader calibration study. The difference between continued search and plain continuation estimates the contribution of continued search here. Any residual plain-continuation minus rollout-estimate gap remains unexplained by that policy change. Evidence: [calibration-analysis.json](recount/calibration-analysis.json), fresh jobs/games, selected positions and full continuation move histories under `recount/`.


The plain-continuation implementation was separately checked against six frozen plain reference games, two per net. Starting halfway through each recorded game, repeated frozen `rank --max-candidates 1` calls must reproduce every remaining move, actor, option count and the final winner without truncation. Results: `[{"id": "p99_n1702", "net": "n1702", "start_decision": 71, "rank_calls": 73, "errors": []}, {"id": "p55_n1704", "net": "n1704", "start_decision": 52, "rank_calls": 53, "errors": []}, {"id": "p31_n1702", "net": "n1702", "start_decision": 133, "rank_calls": 134, "errors": []}, {"id": "p142_n1703", "net": "n1703", "start_decision": 55, "rank_calls": 56, "errors": []}, {"id": "p84_n1703", "net": "n1703", "start_decision": 48, "rank_calls": 50, "errors": []}, {"id": "p156_n1704", "net": "n1704", "start_decision": 57, "rank_calls": 58, "errors": []}]`. This checks the continuation method against the separate native `play` reference, including reconstruction of repeat-guard history.


**Final interpretation of calibration.** In the 46 eligible fresh positions, estimates average 55.325%. Plain continuation wins 58.696% (27/46), close to the rollout prediction; continued search wins 76.087% (35/46). Changing only future policy adds 17.391 points here. The observed search-versus-estimate gap is 20.762 points, leaving a plain-versus-estimate difference of 3.371 points. This supports continued search as a substantial explanation in this sample. It does not prove that it is the only explanation for the locked calibration curve. There are 11 search-only wins and three plain-only wins; the exact two-sided paired sign p-value is 0.057373, while the descriptive paired percentile interval is [2.174, 32.609] points. The small exploratory sample and method-dependent uncertainty accompany that conclusion.

**Final execution accounting.** All four requested phases completed in order, including all 64 protocol-drawn games. No requested phase was skipped and no job was stopped for the deadline. Fourteen of the 60 fresh games had no position meeting the conservative nonzero middle-bin eligibility filter; all 46 eligible counterfactual continuations completed without truncation. All 60 fresh games finished OK. The continuation implementation separately matched 424 frozen rank calls across six plain reference games, two per net, with no move/actor/count/winner differences.

| Completed work | Scope | Outcome |
| --- | --- | --- |
| Frozen native play | 30 search + 30 plain, each at 1 and 4 workers | 120 executions; every non-CPU field matches |
| Independent ten-game ranker sample | Closing, middle, Alone, branches, repeats | 122 Alone matches; 3/3 branches agree; exact repeats; weaker middle separation disclosed |
| Prespecified 64-game ranker gate | 64 closing + 128 middle positions, Alone, branches, repeats | All rules pass; 668 Alone matches; 33/34 branches agree |
| Full-gate numeric audit | 1,196 outputs / 170,975 samples | Zero finite/range/mean/error/count problems |
| Recorded-action inspector | 279 games / 156 paired openings | All CRCs/traces/results and paired hands/first player match |
| Actual closing boards | All 63 estimated closings; five fixed-index examples reviewed | Terminal class explains high scores; no extra outcome-selection filter found |
| Causal calibration | Seeds 86610000-86610059; 46 eligible paired continuations | Supports continued-search explanation, with uncertainty reported |

First worker launch was 2026-11-09T21:47:26.717725+00:00; final verification is 2026-11-09T22:47:31.600102+00:00, about 60.1 minutes of wall time. The hard deadline was 2026-11-09T23:47:26.717725+00:00. The configured cap was four game/search workers, every native invocation used `nice -n 19`, monitoring observed at most 4 leaf workers and only nice 19, and zero workers remain. [execution-final.json](recount/execution-final.json), [phase-history.json](recount/phase-history.json) and [load-monitor.jsonl](recount/load-monitor.jsonl) preserve the accounting. CPU timings alone were excluded from equality checks.

The deliberately omitted larger work remains: a full 1,512-game native replay, a fresh privacy corpus and all planted-fault stages, a conformance rebuild, novel opponent families and human play, and manual historical adjudication of every line. The full final search corpus alone recorded about 16 CPU-hours, exceeding two hours at four workers. These omissions limit broader claims; none is represented as passed. No new experiment used an 87M game seed. The only new scored games were the separate 60-game 86610000 cohort. Locked records, tracked files and other worktrees were not modified. The supplied database remains read-only; its hash and the frozen binary hash were reverified at completion.

**Final acceptance after execution: YES, I accept the PASS for the frozen protocol's known-list, three-network experiment.** The independent gain remains 30.258 percentage points with block interval [27.778, 32.639], and all independently checked decision rules pass. The calibration report must exclude fallback placeholders. Reachable load-order/privacy limits and a reproduced, avoidable fatal attack by the plain network constrain interpretation beyond this opponent family. The new continuation experiment supports the future-search explanation without becoming a new strength gate. Evidence: [acceptance-checks.json](recount/acceptance-checks.json).
