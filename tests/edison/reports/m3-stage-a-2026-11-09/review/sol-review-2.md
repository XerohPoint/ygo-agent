# M3 focused second review

Reviewed detached HEAD `50a784d` and `c3cb65c..50a784d`. **I still find issues blocking a freeze:** recovery after a cut journal line is broken, replay-only interruptions escape the agreed worst-case rule, and undefined resampled gaps silently drop drawn builds. Most other first-pass fixes are correctly implemented. This pass does not reopen the settled statistical or coverage design.

## Findings

### 1. REACHABLE - A tolerated journal tail makes the next resume corrupt the journal

**File/line:** `tools/edison/heldout/stage_a.py:294-302`, `317-320`, `934-937`. **Priority: high; freeze blocker.**

`read_journal()` ignores an incomplete final JSON line, but neither it nor `run_tasks()` removes that fragment before appending. The next start record is concatenated to the fragment. Once subsequent records are appended, the damaged line is in the middle and `read_journal()` rejects the journal. Recovery can therefore run more cells and only then stop, leaving the chunk unassemblable. A complete final JSON object whose trailing newline was cut has the same problem: the reader accepts the object, but the next append joins two JSON objects on one line.

**Scenario demonstrated, with fake workers only:** a stored VOID for `first:0`, a completed start for `first:1`, and a cut result for `first:1`. The reader initially accepts the journal. Resume runs only fake task 1, then rereading raises `is damaged at line 4`. A separate complete-result-without-newline fixture fails at line 2 after resume. The current selftest checks that a cut line can be read; it never appends and reads again after that cut.

**Fix:** Before launching any resumed task, repair the accepted tail under exclusive ownership: truncate an invalid fragment to the last complete record boundary, and append a newline to an accepted complete final record if needed. Preserve all complete results and preceding starts. Add end-to-end cut-start, cut-result, and missing-final-newline resume tests, including a previously stored VOID and the expected unresolved-start count.

### 2. REACHABLE - Interrupted replays do not count in the worst case or interruption total

**File/line:** `tools/edison/heldout/stage_a.py:343-349`, `419-420`, `477`, `966`. **Priority: medium; freeze blocker under the written A4 rule.**

`assemble()` puts unresolved first-attempt starts into the row and unresolved replay starts into the separate replay record. `arrays()` sees only rows, so only first-attempt interruptions populate `suspect`. The reported `unresolved_starts` total also sums rows only.

**Scenario:** the first execution of a pilot/face cell finishes and wins; the runner stops while replaying that cell; its replay is rerun and matches. The chunk validates with zero mismatches. Its replay record correctly says one unresolved start, but the cell's worst-case wins remain unchanged and the reported total omits the interruption. A4 says games in a cell with an interrupted attempt count as losses, without excluding replays.

**Demonstration:** a synthetic scheduled face cell at index 6223, with matching complete first/replay results and `unresolved['replay:6223']=1`, produces `row_unresolved=0`, `replay_unresolved=1`, `suspect_wins=0`, and `validate() == ([], 0)`. This can decide a sufficiently close worst-case mean.

**Fix:** Carry interruption provenance from both first and replay tasks into the corresponding cell's worst-case annotation. Report first and replay interruptions explicitly, and validate their bookkeeping. Add a replay-only interruption test that checks the changed worst-case numerator, rather than only the journal counter.

### 3. REACHABLE - Sparse allowed voids silently remove builds from bootstrap gap draws

**File/line:** `tools/edison/heldout/stage_a.py:450-454`, with `ratio()` at 424-426 and the gap percentile at 495. **Priority: medium; resolve before freezing.**

The coverage gate guarantees a completed game per build/arm/optimizer/seat in the original data, but not in every resampled set of cells. A build can have completed mirror games in only two of its 42 cells, one per seat. Sampling 42 cells with replacement can select neither. Its mirror denominator is then zero, `gap_of()` returns NaN for that drawn build, and `np.nanmean()` silently omits it. The result is a mean over a smaller, data-dependent set of builds, rather than the prescribed mean over all drawn builds. The same problem can arise through a sparsely observed shared twin. `nanpercentile` also silently drops wholly undefined draws.

**Scenario demonstrated:** entirely artificial data for 120 builds, with complete strong pilot/face results and one build's mirror void in 40 of 42 cells. Its two surviving cells cover both seats. Mirror void share is only 0.79365%, and `decide()` reports no broken rule and no empty coverage combination. In 1,000 draws using the configured seed, 122 draws omit at least one drawn build occurrence because its sampled mirror has no games (128 occurrences total). This scenario also fits the real 488-build block's void budget. No corpus outcomes were used.

**Fix:** Define and implement the zero-denominator behavior before outcomes are available. Do not silently omit a drawn build from the gap mean. A fail-closed error identifying an undefined required draw is preferable to silently changing the estimator; any alternative needs an explicit prespecified rule. Add sparse held-mirror and shared-known-twin fixtures. The two-stage bootstrap itself matches the protocol when its required rates are defined.

### 4. REACHABLE - The revised Side-in count/attribution still does not reproduce

**File/line:** `docs/edison-corpus.md:232-233`. **Priority: low; documentation, not a Stage A freeze blocker.**

The qualitative correction is right: the existing soak did not move whole Sides into play. However, the document labels my earlier 238,790 count as presence-only, whereas that calculation already used multiplicities:

```python
bool(Counter(original_side) - Counter(final_main + final_extra))
```

Reconstructing the full existing plan with the unchanged actual `soak.side_in()` helper, without playing any duel, reproduces **238,790** deck instances under that definition. A pure identity-presence test, `any(code not in played for code in original_side)`, gives **135,019**, not 238,790. I cannot reproduce 245,230 under the document's stated final-played-deck copies criterion.

**Fix:** State the exact counted predicate and correct the attribution/count, or provide the different predicate behind 245,230. If the intended metric tracks the provenance of individual incoming copies instead of the final card multiset, label that distinction explicitly. This does not affect the already-recorded crash or action-choice coverage counters.

## Brief nonblocking edge cases

- **PLANTED-ONLY - Empty F crashes before its nongating branch.** At `stage_a.py:470-471`, `reshape(len(subset), -1, 2, len(COLUMNS))` raises when `subset` is empty, before line 472's empty-subset handling. A synthetic set with no far builds reproduces this. The committed selection has 102 far builds, so it does not affect this freeze. Fix by bypassing the seat reshape for empty subsets or supplying the known opponent dimension explicitly; add a zero-F selftest.
- **PLANTED-ONLY - Interruption annotations are not validated.** `stage_a.py:353-379` accepts a row with `unresolved_starts` deleted, and `arrays()` then treats it as uninterrupted. The writer always supplies the field today, so this requires an altered stored record. Validate nonnegative integer interruption fields; if the stored journal is meant to establish their provenance, verify/reconcile it too. `journal_sha256` is currently written at line 939 but never checked. I did not pursue arbitrary outcome-file tampering beyond this annotation relevant to the new rule.

## Fixes confirmed

- **Selection/deduplication:** `select` reproduces the committed JSON without a tracked diff. Independently checked all variants against their representative's separate, filtered Main and Extra multisets, uniqueness, and lowest numeric list id. Confirmed 542 candidate lists -> **488 builds**, **102 far**, bands **182/204/102**, and all exclusion totals. Copy-sensitive untrained validation and legal-and-inside comparison are present; the protocol accurately narrows the legality claim to the earlier production check plus load-time validation.
- **A4 coverage:** The checks now cover both blocks, every build, every applicable optimizer/arm, and both counted seats. The added no-face-game selftest fails through coverage while remaining below the block void bar. Arrays map opponent and seat to the expected units, and draws remain nonwins.
- **Bootstrap and decision rules:** Compared `bootstrap()` with an independent scalar implementation using artificial heterogeneous counts, repeated twins, and a subset. All optimizer-rate draws and gap draws matched exactly, including RNG consumption and the training-list draw shared by repeated twins. Column ordering, twin indexing, point gap arithmetic, optimizer averaging, seat pooling, all/F thresholds, and first-attempt worst-case wins/denominators agree with the revised protocol. Findings 2 and 3 identify the remaining interruption and undefined-rate cases.
- **Result validation:** The existing tests catch row swaps, another list, wrong/short duel seeds, a different policy identity, missing/duplicate replays, a changed digest, and differing replay results. Replay equality is recomputed rather than trusted. Source inspection confirms `play()` checks all existing chunks for a mismatch before constructing the worker pool. Stored results survive a normal interruption/resume; complete stored VOID results are not replaced. Finding 1 is the missing torn-tail round trip.
- **Freeze/provenance:** The protocol, corpus summary and both search Python modules are now in `DEPENDS`. Source inspection confirms tracked-tree cleanliness, recorded source HEAD, refusal after run artifacts exist, a clean committed manifest directly above its named commit, a manifest-only commit diff, and current-input comparison. The untracked review report does not invalidate these tracked-tree checks. I did not invoke `freeze()` or generate a manifest.
- **Matrix/replays:** Independently reconstructed **149,646 cells**, **771,456 games**, **82 chunks**, and **1,179** replays. Actual replay indices cover all 12 block/arm/counted-seat combinations. Arm deck/policy assignment and seed construction retain the checked first-pass behavior.
- **Search:** The actual new selection produces **11 groups of exactly 21 unique lists**, with every selected held-out build and its four opponents covered once. There are 240 opponent pairs, 480 search jobs and 480 plain jobs. Full pairing passes, and changing only a plain net fails it. The revised predecessor check requires an analysis result for the current manifest. On artificial paired scores with unequal surviving cluster sizes, `paired_summary()` matched a separate build-cluster resampling calculation exactly; search/plain/difference use the same comparisons, and empty subsets return explicit nulls. All/by-seat/F calls and per-arm missing/void reasons are present. The separately named `search_games` diagnostic describes its own unpaired search population. No search was executed.
- **Privacy:** Ran the revised guard against the supplied read-only raw cache and all four committed output roots: exit 0, 1,563 raw JSON files, 2,152 collected strings, 1,336 output files, zero unreviewed hits and no person keys. Only the four allowed vocabulary words matched. Local planted missing-cache, no-person-strings, alphabetic-name-leak and numeric `builderId` cases all exited 1; a clean fixture exited 0. No actual identity leak was found. Minor documentation detail: all-digit *strings* are still text-searched because line 75 filters only by length; numeric JSON values are omitted by `walk()`. This is stricter than the documentation's claim that numeric ids are not text-searched, not a new privacy bypass.
- **Selftest:** The full permitted `selftest` exited 0. Its intentional fake-worker interruption prints a thread `KeyboardInterrupt` traceback; the fixture's assertions still pass. Temporary files were redirected inside this worktree. Its successful status does not cover the additional failure cases above.

## Scope and recommendation

All Python ran under WSL through `/home/mada/ygo-agent/.venv/bin/python`, from this worktree, with both requested thread limits set to 1 and bytecode writes disabled. This pass used synthetic worker/results fixtures and static plan reconstruction only: **no actual duel was played, no corpus win rate was computed, and no Stage A play/freeze/analyze/search command was run**. At most three review Python processes ran concurrently. No read-only dependency was changed, no file outside this worktree was edited, and no commit was made. Existing tracked files remain unchanged; generated checks are under ignored `build/sol-m3-review-2/`.

**Recommendation: fix findings 1-3 before freezing.** Finding 4 and the brief planted-only notes are nonblocking for the current 488-build selection. I found no additional blocker in deduplication, normal finite-denominator bootstrap arithmetic, manifest commit checks, search grouping/pairing, or the revised privacy guard.
