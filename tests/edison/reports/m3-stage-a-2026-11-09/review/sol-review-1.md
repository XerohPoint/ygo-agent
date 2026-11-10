# M3 review before freezing

Reviewed detached HEAD `c3cb65c`, including corpus commit `fb9902b`. **Fix the A4 coverage and resume/integrity gaps before freezing.** The corpus's headline counts reproduce, and I found no committed player identity in the reviewed outputs. No held-out network/search result was generated or read as a strength result during this review.

Classes below follow the request: REACHABLE means a normal execution/failure of the current runner can reach the issue; TEST means an absent or ineffective check; PLANTED-ONLY means the supplied inputs must change. Findings do not claim that an unrun test has already exhibited the failure.

## Findings

### 1. REACHABLE - A4 can pass with no face games for a held-out list

**Location:** `tools/edison/heldout/stage_a.py:376-381`, with `gaps()` at lines 319-330. **Priority: high.**

The only list-level completeness test is `missing` returned by `gaps()`. That checks held pilot and mirror, plus the twin's known pilot and mirror; it never checks face. If every face cell involving one held-out list voids, all of that list's face results disappear without triggering "a held-out list with no game in some arm" from protocol line 103. One list is only 1/542 = 0.1845% of each optimizer's face block, below the 1% void bar. This can happen when the heuristic piloting X, against the network, reaches an unsupported or looping path that the other arms avoid.

**Demonstration:** I used the runner's synthetic `made_up()` builder with 120 fictitious held-out lists and strong synthetic results, voiding every held face cell for `h000`, for all optimizers and both seats. `decide()` returned `broken=[]`; face games for that list were zero and each face void share was 0.8333%. No corpus game was used.

**Fix:** Explicitly check completed-game coverage for each held-out list in pilot, face and mirror, preferably each relevant optimizer and seat too. Keep this separate from gap availability, and add this negative selftest.

### 2. REACHABLE - An interrupted chunk loses first results and can retry failed cells

**Location:** `tools/edison/heldout/stage_a.py:620-630`; resumed-chunk path at lines 615-619. **Priority: high.**

Nothing is persisted until the entire chunk's first pass and replay pass finish. Suppose an early cell voids from a nondeterministic native crash, other cells finish, and the run is interrupted before the chunk is published. Resume sees no chunk and replays all those consumed seeds. If the native crash does not recur, the first void disappears from the final evidence. A mismatch observed during the replay pass can likewise disappear if publication is interrupted. This matters given the corpus's documented intermittent native fault, even though that particular suspect card is excluded here. Atomic final publication protects JSON completeness, but does not preserve the first execution of cells.

There is a second resume inconsistency: a chunk containing a replay mismatch is published before the exception at line 635. A subsequent `play` only checks its job digest and skips it, then consumes further chunks despite the protocol's stop-on-mismatch rule. Analysis eventually sees that mismatch, so this second case does not itself produce a passing verdict.

**Fix:** Journal cell starts, first results, and replay results as they arrive, then assemble immutable chunks from that journal. Preserve failures across attempts and define recovery for an interrupted, unresolved cell before freezing. On resume, validate existing chunks and stop immediately on a recorded mismatch. Add interruption/resume tests with a failure before the interruption.

### 3. TEST - Chunk integrity trusts a header instead of checking stored jobs and replays

**Location:** `tools/edison/heldout/stage_a.py:640-655`, also lines 615-619. **Priority: high.**

The digest is compared against the expected jobs, but it is a stored assertion: it is never recomputed from the actual rows. Equal row count is sufficient. Decks, arm, optimizer, counted seat, list, seed, game count, and duel seed coverage are not compared against the corresponding expected job. Replay coverage is also unchecked: `replays=[]` has zero mismatches, even for a chunk that requires replays. Duplicate replay indices and replays for the wrong cells are accepted.

**Demonstration:** An in-memory fake chunk with the correct expected header hash and length, but every row naming an unrelated list, seed -1, games 999, and an empty PASS duel array, plus no replays, was accepted by `load_results()` with all three integrity counters zero. This demonstrates acceptance by the loader, not a claim that these particular fake rows would pass A1-A3.

**Fix:** Validate each row's job metadata, each PASS row's exact expected duel count and unique consecutive seeds, allowed terminal winners, and the exact scheduled replay-index set. Store policies or a per-row job identity so policy pairing is verifiable. Use the same validator during resume and analysis. Add planted row swaps, wrong seeds, short PASS arrays, and missing/duplicate replay tests.

### 4. TEST - The freeze does not hash the protocol or all scoring code, or enforce publication

**Location:** `tools/edison/heldout/stage_a.py:68-71`, `freeze()` at lines 574-588, `check_frozen()` at lines 592-598, and `search_inputs()` at lines 738-742. **Priority: high.**

Protocol lines 3-5 say the protocol document itself is hashed into the manifest. It is absent from `DEPENDS`. Editing the written rules after freeze therefore does not change the checked inputs. The search check also imports `search_common.py` and `search_study.py` without freezing their hashes; for example, changing `study.score()` changes the reported search result without a frozen-input rejection. The corpus summary used for selection/seed checks is also absent.

Unlike `m2_final.py`, this runner does not verify that the manifest was committed before play or refuse to overwrite an existing manifest after results exist. Hash equality checks current files against the current manifest; it does not establish that those files were fixed before outcomes were available. These are missing safeguards, not evidence of post-outcome editing in this branch.

**Fix:** Hash the protocol, corpus summary and all invoked search Python dependencies. Require a committed immutable manifest linked to the reviewed source commit, and refuse re-freezing a consumed run. M2's commit/provenance checks are a useful existing implementation to adapt.

### 5. REACHABLE - Side-in does not actually move the whole Side into play

**Location:** `tools/edison/corpus/soak.py:34-44`; claim in `docs/edison-corpus.md:228-229`. **Priority: medium.**

Each incoming Side card is appended to the same array from which the next outgoing card is randomly selected. A later swap can immediately remove an earlier incoming Side card. The documented exception for an empty Extra Deck is not the explanation: this occurs in normal nonempty Main Decks.

**Demonstration:** Reconstructing the existing Side-in plan without playing a duel, at seed 88200000 `fl875` has original Side cards 60082869 and 15800838 absent from its final Main+Extra. Across the plan, 238,790 of 254,016 deck instances lack at least one original Side card in the played zones (that total also includes any empty-zone exceptions). The observed action-choice coverage counts remain valid for the decks actually generated, but "whole Side moved in" overstates the stress exposure.

**Fix:** Select outgoing slots without replacement from the original zones, then insert Side cards into those slots. Define what to do if the incoming count exceeds available original slots. Preserve the existing results under an accurate description; a corrected soak would be a separate loading/crash check.

### 6. REACHABLE - The search report hides exclusions and uses inconsistent populations

**Location:** `tools/edison/heldout/stage_a.py:767-786`. **Priority: medium; reported check only.**

Pairwise exclusion at line 772 is sensible, but only a total `kept` count survives. Plain-only failures, missing results, and exclusion reasons are not reported; `study.summarise(searched)` sees only search games, and scores its own surviving search games even when their paired plain game was void. Thus its nested `search_score` can disagree with the top-level paired `search` score. With one seat excluded in a pair, the headline search/plain scores weight surviving seats, while `difference` equally weights pair means, so the displayed scores' subtraction need not equal the reported difference. Ordinary worker voids can trigger this without changing inputs.

The protocol also promises scores/difference/interval reporting by seat and on F. The implementation gives only a point difference by seat and one point difference under `far_lists`; there are no split search/plain scores, split intervals, or F sample counts. If everything is excluded, it writes NaN statistics instead of an explicit unavailable result.

**Fix:** Report complete, missing and void counts/reasons for both arms, contributing pairs and seats, and relevant native failure counters/attempt history. Derive paired scores and differences with one declared weighting scheme. Emit null plus a reason for empty subsets. Reuse an interval helper for all, each seat and F, with their search/plain scores and sample sizes. Preserve "reported only"; failures should remain visible without becoming a new performance gate.

### 7. TEST - The search pairing selftest cannot catch a mismatched network

**Location:** `tools/edison/heldout/stage_a.py:552-553`. **Priority: medium.**

The first equality compares fields from `search` to the same fields from `search`, so it is a tautology. The remaining cross-arm equality compares only decks and env seed. If the plain arm accidentally used a different optimizer, the check would still pass. The current actual jobs do use the same optimizer; I independently compared decks, seed, net, and search/plain seat semantics for all 480 pairs of job records.

**Fix:** Compare search and plain records directly, including net identity, decks, seed, unique pair/seat ids and the intended -1 plain search seat. Prove the test fails when only the plain net is changed.

### 8. TEST - The privacy command can succeed while detecting a real name leak, or checking no raw data

**Location:** `tools/edison/corpus/privacy_check.py:43-60`. **Priority: medium.**

The exit code is nonzero only for a matched string of at least ten characters containing a digit. A genuine leaked alphabetic player handle is printed but exits successfully. A missing raw directory yields zero collected strings and also succeeds; in this fresh review worktree the default raw directory is absent. The current corpus outputs passed a manual review of all detected overlaps, so this is an ineffective automated guard rather than an actual identity leak found here.

**Fix:** Accept an explicit raw-cache path, fail on a missing/empty cache, and fail on every unreviewed identity hit using a narrowly documented allowlist for vocabulary overlaps. Report tracked files scanned explicitly. Support or explicitly account for very short names and numeric-valued identity fields rather than silently omitting them.

## Brief lower-priority and planted-only notes

- **PLANTED-ONLY - selection validation is narrower than documented.** `stage_a.py:129-135` verifies v1 membership, untrained code identities and twin/distance, but trusts `production_legal` and `in_training`, and does not compare untrained multiplicities or recompute labels. An altered manifest can change legal/nontraining selection without a direct recomputation check. The sole actual illegal list is also outside v1, so today's selection succeeds and this does not change the supplied 542. Revalidate legality/training identity, or narrow protocol line 35's "every field" promise. An illegal list built entirely from v1 cards would currently cause a manifest-disagreement stop before reaching the intended not-legal exclusion, because the corpus's inside-v1 field includes legality and the selector's local `inside` does not.
- **TEST - search-after-verdict is procedural only.** `stage_a.py:745-756` never reads `RESULT` or checks that analysis completed for the same manifest. The documented command order is safe; enforce a matching completed verdict before admitting search if the CLI is intended to protect test order.
- **REACHABLE, documentation only - privacy file count.** `docs/edison-corpus.md:60` says 1,337 committed files; commit `fb9902b` adds 1,336 files, which is also the four documented output roots' tracked-file count here. Correct the count or identify the additional scanned file. The 2,152 strings and four reviewed overlaps reproduce.

## Design concerns: what the rules measure

These are distinct from implementation/protocol mismatches.

1. **542 corpus rows represent 488 different played builds.** Distinct corpus identity includes Side, but Stage A strips Side. There are 36 groups of duplicate Main+Extra multisets, 54 extra rows, and as many as six rows for one played build. They get independent env seeds, but additional weight and separate bootstrap sampling units despite representing the same build. Deduplicate after the Gottoms rule for the build-generalization claim, or explicitly call this weighting by source-list variants and bootstrap build groups.
2. **A pooled pass can coexist with substantial bad coverage.** Vayu Turbo, Blackwing and Hero Frog contribute 412/542 rows. There is no minimum per archetype or per list; the equal-archetype score is only reported. The far gate helps with distance, but not archetype breadth. Describe a pass as average performance on this concentrated corpus subset, not broad Edison coverage. Stage B remains necessary.
3. **A3's bootstrap conditions on noisy shared twins.** Many X share one twin (147 use one Vayu twin), and all three optimizers share starts. Resampling held-out rows does not propagate uncertainty in the reused known-block estimates or optimizer variation. It implements the written bootstrap, but its interval is not a complete uncertainty interval for a population-level generalization claim. Consider hierarchical/pair resampling of starts with shared twin draws, or state the conditional interpretation.
4. **Wilson is implemented correctly for the declared formula, but the sampling model is approximate.** Both seat arrangements reuse a seed and starts share deck/optimizer structure. Treating all games as independent Bernoulli trials can overstate effective sample size. A seed/matchup cluster interval would better reflect the paired design. Do not silently change the statistical rule after seeing results.
5. **Mirror correction is an interaction with a weak policy, not a universal deck-strength correction.** A build's bad heuristic score can offset a substantial network decline in A3 (the supplied weaker-list selftest deliberately demonstrates this). A1 still imposes an absolute floor. Report raw gaps prominently, along with archetype/build dispersion, and keep claims about the heuristic population explicit.
6. **Allowed voids can select the observed games.** Excluding a small number of looping/bad games can move a borderline rate or gap over its threshold. A whole cell is dropped, including otherwise complete games, and the 1% budget is block-wide rather than list-specific. Report a prespecified worst-case treatment/sensitivity alongside the principal result; M2 already has a relevant worst-case check.
7. **Replay sampling covers counted seat 0 only.** Every cell index divisible by 128 is even, and seats alternate fastest. All 1,294 scheduled Stage A replays therefore have counted seat 0. This follows the written schedule exactly, but supplies no matrix replay evidence for counted seat 1; choose a stratified sample before freeze if both-seat reproducibility is intended.
8. **Search remains an oracle-list, same-family opponent check.** It is given the opponent's exact Main list, uses M2's uniform hidden dealing and retains M2's documented load-order/refusal limitations. The 11 registry groups are a loading workaround, not a belief over those 21 lists. The interval resamples 240 opponent pairs even though four pairs share each of 60 held-out builds; report that conditional interpretation, or cluster by held-out build for a broader claim. None of this should be promoted into human-strength or unknown-list inference evidence.

## Confirmed claims and verification

All Python ran through `/home/mada/ygo-agent/.venv/bin/python` in this worktree under WSL, with `OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1` and bytecode writes disabled. Maximum active evaluation/validation workers: three. No web request, prohibited Stage A command, corpus network/search play, or corpus win-rate calculation was performed. Existing corpus raw responses and soak counters were read only. No source/checkpoint/binary/database/Lua file was edited and no commit was made.

### Corpus

- **Provenance/counts:** Read the supplied raw cache, reconstructed appearances/exclusions and list multisets, and compared all representative YDK section/card lines and every appearance's cards. Confirmed 176 gallery events, one excluded event, 175 used events, 1,385 listed placements, one missing deck, 1,384 included appearances, 1,324 unique lists, and 46 repeated lists accounting for 60 extra placements. All 1,384 source YDKs agree with source card arrays by normalized card name; stored card lines preserve order and leading zeros. No stored-list/source-card mismatch was found.
- **Fetch evidence:** 91 seeded responses and 1,469 successful logged fetches reproduce (160 event responses and 1,309 deck responses fetched now). Minimum spacing between logged request starts is 2.243015 seconds. Code uses only the three declared public API route families, caches successes and spaces requests. I did not refetch or independently prove the unavailable deck's second HTTP attempt; the final failure cache records HTTP 500 and the document records the retry history.
- **Production legality:** Regenerated the local full code list: 4,035 lines, SHA-256 `4d52f62e2498fe0423556612de9e265bf0164d665f3730e46c5c6a793a795e4d`. Re-ran all 1,324 source lists individually through the unmodified production `init_module`, using three workers and no env reset/play. Exactly 1,323 passed. Only `fl27828` failed, for combined copies above limit 1 for Reinforcement of the Army (32807846). No verdict differed from the manifest. Independently confirmed no list's passcodes fall outside the legal pool. The five earlier planted legality controls are documented evidence; I did not rerun them.
- **Coverage/training/distance:** Confirmed 619 legal lists inside v1 in Main+Extra, 601 nontraining; 380 strictly inside v1 in all zones; 395 inside after the Gottoms rule including Side; 31 inside only after that rule. There are 18 corpus training rows; 569 v1 lists with no untrained Main/Extra identity, 551 nontraining, including nine exact training Main+Extra clones with different Side. Distance counts 27/222/320 for 0/1-9/10+, and 9/222/320 without training, reproduce. Held-out distances have median 18 and maximum 87. Recounted archetype and year tables against the manifest and summary with no differences; 98 normalized labels.
- **Privacy:** Read raw person-field schemas, recollected 2,152 strings and scanned every one of the 1,336 files added by the corpus commit. Only `Jinzo`, `Rose`, `Soul`, and `beastmode` matched, in card/archetype/community/event contexts or the explanatory documentation. Builder headers are replaced by provenance and output fields omit person objects. No player-name/handle/user-id leak was found in these outputs. This is a review of the committed corpus artifacts, not a proof about every historical external file or unlogged process.
- **Soak:** Inspected the production soak/worker/report code and existing raw soak counters. Main: 127,008 planned/completed, 25,986,082 decisions, no crash/diagnostic/contract violation. Side-in: 127,008 planned, 127,007 completed plus one crash, 28,039,373 decisions, no diagnostic/contract violation. Every legal list has 192 exposures in each stage. Summary choice coverage 557/583 and 626/653, activation choices 500 and 556, maximum options 75 and repetition 136 agree with the artifacts. The worker records action choices, not core activation logs, as disclosed; it does not return winners in its persisted results or compute win rates. The old wrong crash signature and its qualification are explicitly preserved. I did not reproduce the intermittent crash or independently validate the stripped-binary diagnosis/Dark Panther attribution; the latter remains a lead, not a confirmed cause. Apply finding 5 to the "whole Side" claim.

### Stage A and search construction

- `stage_a.py selftest` passed all cases. Its passing status does not cover the missing negative checks above.
- `stage_a.py select` reproduced `stage-a-lists.json` byte-for-byte: 542 held-out, 104 far, distance bands 222/216/104, exclusions 704 outside v1 + 50 untrained + 18 training + 9 exact training Main/Extra + 1 illegal. All archetype/twin counts reproduced; 27 selected lists lose Gottoms.
- Ran the permitted `livecheck(2)` after overriding only the in-memory `LIVE_SEED` to **86630000**. Default 88410000 was not used. All 542 held-out decks loaded; only training lists played, two games per cell on 86630000-86630005, with training-only replays on the same seeds at four env threads. All three cells passed and traces matched. No Stage A test seed was consumed.
- Independently checked the real job construction: **165,522 cells, 834,960 games, 89 chunks**, 1,294 scheduled replay cells. All three arms share the intended physical deck arrangements and env seed blocks, with face counting the opposite deck from pilot/mirror. Policies/optimizers and counted seats are assigned correctly. Known seeds end at 88194111; held-out seeds end at 88175527, within the declared disjoint blocks and outside recorded corpus-soak ranges. Draws count as nonwins; strict/non-strict comparisons at the A1-A4 thresholds and the Wilson formula match the protocol. Gap arithmetic, equal optimizer averaging, all/F list bootstrap order and seed, and far-set gating match the stated algorithm, subject to the integrity/statistical concerns above.
- **No new information path into the plain network was found.** The registry extension only loads named decks; job specs select the played lists. Training's schedule still selects from `decks.registry()`'s 21 lists, not the optional runtime extension. Stage A collects no training rollouts, changes no weights/code-list rows, and uses public observations with `oppo_info=False`; it does not pass `info:deck` or corpus metadata to the net. Current native/code-list/checkpoint hashes match the frozen M1/M2 identities. This supports absence of a new training/observation leak, without claiming a new exhaustive native privacy proof.
- **Search grouping is correct for the actual inputs.** Independently reconstructed the 60-list/240-pair draw and 480 search plus 480 plain records. All **11** groups contain exactly 21 unique names; each sampled held-out name occurs in exactly one group, which contains all four of its opponents. Fillers are training lists. The per-group job filter neither duplicates nor omits current jobs. Native `fm_env.h:851` checks registry cardinality 21, not an immutable set of the original 21 names, so substituting legal v1 held-out names is compatible with that restriction. Checked the paired decks, env seed, same net, both seats, budget 64/depth 48/candidates 4/z 1.0 and fixed binary hash without executing search. Search uses the declared opponent-list assumption rather than reading the hidden hand/deck order as a new Stage A shortcut.

Only this report is a new tracked-scope artifact. Generated verification files/logs are under this worktree's ignored `build/`; selection regeneration left no tracked diff. The report makes no held-out performance prediction or verdict.
