The verdict stands: `STAGE_A_MET`; all A1–A4 rules pass on all 488 builds and on F (102 builds).

| Rule / number | Bar | All: reported | All: recount | F: reported | F: recount |
| --- | --- | ---: | ---: | ---: | ---: |
| Builds | F gated if ≥10 | 488 | 488 | 102 | 102 |
| A1 pilot mean | ≥55% | 80.550677% | 80.550677% | 81.798553% | 81.798553% |
| A1 pilot, 1702 rate | reported component | 82.583187% | 82.583187% | 83.432540% | 83.432540% |
| A1 pilot, 1702 2.5th percentile | >50% | 81.947697% | 81.957425% | 81.967787% | 81.926937% |
| A1 pilot, 1703 rate | reported component | 78.522639% | 78.522639% | 79.353408% | 79.353408% |
| A1 pilot, 1703 2.5th percentile | >50% | 77.843177% | 77.862723% | 77.952848% | 77.935195% |
| A1 pilot, 1704 rate | reported component | 80.546204% | 80.546204% | 82.609711% | 82.609711% |
| A1 pilot, 1704 2.5th percentile | >50% | 79.838719% | 79.855795% | 81.139122% | 81.104108% |
| A1 pilot, seat 0 | ≥50% | 82.472190% | 82.472190% | 83.477280% | 83.477280% |
| A1 pilot, seat 1 | ≥50% | 78.629163% | 78.629163% | 80.119826% | 80.119826% |
| A2 face mean | ≥55% | 71.499724% | 71.499724% | 77.925615% | 77.925615% |
| A2 face, 1702 rate | reported component | 73.946136% | 73.946136% | 80.018674% | 80.018674% |
| A2 face, 1702 2.5th percentile | >50% | 72.609259% | 72.654420% | 77.800975% | 77.725257% |
| A2 face, 1703 rate | reported component | 68.808548% | 68.808548% | 75.875350% | 75.875350% |
| A2 face, 1703 2.5th percentile | >50% | 67.360613% | 67.358265% | 73.325018% | 73.243318% |
| A2 face, 1704 rate | reported component | 71.744487% | 71.744487% | 77.882820% | 77.882820% |
| A2 face, 1704 2.5th percentile | >50% | 70.352721% | 70.386844% | 75.466853% | 75.420168% |
| A2 face, seat 0 | ≥50% | 73.860753% | 73.860753% | 80.201525% | 80.201525% |
| A2 face, seat 1 | ≥50% | 69.138694% | 69.138694% | 75.649704% | 75.649704% |
| A3 gap mean | reported component | 0.341683% | 0.341683% | 1.517517% | 1.517517% |
| A3 gap 2.5th percentile | >−10 points | -1.137477% | -1.151200% | -1.215770% | -1.327031% |
| A4 pilot worst case | ≥55% | 80.550677% | 80.550677% | 81.798553% | 81.798553% |
| A4 face worst case | ≥55% | 71.499724% | 71.499724% | 77.925615% | 77.925615% |
| A4 maximum void share | ≤1% | 0% | 0% | 0% | 0% |
| A4 missing_chunks | 0 | 0 | 0 | 0 | 0 |
| A4 invalid_chunks | 0 | 0 | 0 | 0 | 0 |
| A4 replay_mismatches | 0 | 0 | 0 | 0 | 0 |
| A4 empty_list_arms | 0 | 0 | 0 | 0 | 0 |

Percentages above retain six decimals for comparison; the JSON artifacts retain full precision. Gap percentages are percentage points. Integrity values in the published JSON are global; the F columns show the same applicable global checks and zero empty F arms. All thresholds pass with either set of bootstrap percentiles.

A4 checks each block/arm/optimizer separately. The complete denominators and void shares are:

| Block / arm / optimizer | Planned games, reported / recount | Void games, reported / recount | Void share, reported / recount |
| --- | ---: | ---: | ---: |
| known/pilot/1702 | 28,224 / 28,224 | 0 / 0 | 0% / 0% |
| known/pilot/1703 | 28,224 / 28,224 | 0 / 0 | 0% / 0% |
| known/pilot/1704 | 28,224 / 28,224 | 0 / 0 | 0% / 0% |
| known/face/1702 | 28,224 / 28,224 | 0 / 0 | 0% / 0% |
| known/face/1703 | 28,224 / 28,224 | 0 / 0 | 0% / 0% |
| known/face/1704 | 28,224 / 28,224 | 0 / 0 | 0% / 0% |
| known/mirror/heuristic | 28,224 / 28,224 | 0 / 0 | 0% / 0% |
| held/pilot/1702 | 81,984 / 81,984 | 0 / 0 | 0% / 0% |
| held/pilot/1703 | 81,984 / 81,984 | 0 / 0 | 0% / 0% |
| held/pilot/1704 | 81,984 / 81,984 | 0 / 0 | 0% / 0% |
| held/face/1702 | 81,984 / 81,984 | 0 / 0 | 0% / 0% |
| held/face/1703 | 81,984 / 81,984 | 0 / 0 | 0% / 0% |
| held/face/1704 | 81,984 / 81,984 | 0 / 0 | 0% / 0% |
| held/mirror/heuristic | 81,984 / 81,984 | 0 / 0 | 0% / 0% |

**NOTE — Independent method and complete accounting.** I wrote `sol-recount/audit.py`, `setup_audit.py`, `search_audit.py` and `claim_audit.py`. None imports `stage_a.py`, and none copies its selection, assembly, validation or statistical routines. I used the protocol for the design and inspected the runner only for record/serialization formats and constants. The regenerated jobs reproduce the frozen matrix SHA-256 `64be3cda8f9e3d27bde9e4d4f39ff4d7b50b03199b84bedff7ba827bca6831a3`. The independently regenerated search jobs reproduce `227273a7b9c7f92d1df468eb5171098b0dd147473d2b39ea13cd752a5a6eab79`.

The matrix has 6,174 known cells and 143,472 held cells: 149,646 cells / 771,456 first-run duels. I checked every row’s block, arm, optimizer, X, opponent, deck order, counted seat, starting seed, game count and policy hash, and every duel’s env index, consecutive seed, winner, steps and trace shape. Chunk identities, exact independent chunk partitions, job hashes and the entire ordered cell set agree. Every start/result journal reconciles with its stored row, including counters and interruption fields; no cut tail, duplicate result or outstanding start exists. Every duel decision sum agrees with its cell counters. The replay set is exactly indices `0,127,254,…,149606`: 1,179 replays, all matching status and every duel field. There are no voids, draws, interrupted first attempts, interrupted replays or empty build/arm/optimizer/seat combinations. All 15 Stage A worker logs are free of the actor’s diagnostic markers.

I also recomputed all 488 per-build records, both gap spreads, all distance bands, all 21 archetypes, equal-archetype pilot weighting, all 21 known-list records, all optimizer/seat rates, decision/guard totals, void reasons and interruption totals. Every non-bootstrap published value agrees to 1e−12 except the two mirror-seat fields discussed below. Full results and every numeric difference are in [recomputed.json](sol-recount/recomputed.json), [differences.json](sol-recount/differences.json), [integrity.json](sol-recount/integrity.json) and [extras.json](sol-recount/extras.json).

Commands in this report run from the worktree root in WSL, with:

```bash
cd /home/mada/ygo-agent/.claude/worktrees/sol-m3-recount
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 PYTHONDONTWRITEBYTECODE=1
P=/home/mada/ygo-agent/.venv/bin/python
```

Evidence/reproduction command: `$P sol-recount/audit.py`; setup and hashes: `$P sol-recount/setup_audit.py`; search accounting without playing: `$P sol-recount/search_audit.py --inspect-only`.

**NOTE — Bootstrap differences are simulation order differences, with no changed rule outcome.** My two-stage bootstrap uses NumPy `default_rng(88810001)` and 10,000 draws for all builds, then 10,000 for F. In each replicate I draw 42 cells for each of the 21 known lists, then draw held builds with replacement, then 42 cells for each occurrence of a drawn held build. All seven arm/optimizer columns share cell indices; every occurrence sharing a twin uses that twin’s one known-block draw. Rates pool wins/games per optimizer; the gap is the mean build gap after averaging optimizers. Any undefined build/twin gap is assigned −2, never dropped. No replicate is undefined here.

Drawing the known cells before the builds changes the RNG consumption order from the published analysis. The 12 optimizer lower percentiles and four gap endpoints differ; the largest change is 0.111261 percentage points (F gap lower endpoint). The table above gives every gated difference. The all-build gap interval is reported [−1.137477, +1.860327] points versus my [−1.151200, +1.830465]; F is reported [−1.215770, +4.331579] versus [−1.327031, +4.283149]. These are compatible recounts of the prescribed bootstrap, not discrepancies in the game counts. Raw gap means remain −0.703796 points overall and −0.821127 on F. Evidence/command: `$P -m json.tool sol-recount/differences.json`.

**WRONG — The reported mirror seat-0 fields condition on X being in seat 0.** `known_mirror_seat0=52.090420%` and `held_mirror_seat0=59.711651%` are the counted X-side win rates only in rows whose counted seat is 0. They omit the rows where X is in seat 1 and O is in seat 0. The actual absolute seat-0 rates across all mirror games are **51.817602% known** (14,625/28,224) and **51.658860% held** (42,352/81,984). The reported numerators/denominators are 7,351/14,112 and 24,477/40,992. Thus the written sentence “the mirror’s seat 0 wins 59.7% against 52.1% in the known block” is wrong as an absolute seat claim. Either use 51.7% versus 51.8%, or explicitly label the existing figures “X’s win rate when X occupies seat 0.” The overall mirror strength rates, 58.052791% held and 50.272817% known, are correct and remain in the gap. No gate uses these mislabeled seat fields. Evidence: [seat-evidence.json](sol-recount/seat-evidence.json). Command: `$P -m json.tool sol-recount/seat-evidence.json`.

**CLAIM — “The networks made 126.4 million decisions” and “0.52% of them” use the wrong population.** The 126,435,408 figure counts both policies, including both heuristic seats in mirror games. The networks made **40,414,152** decisions; the heuristic made **86,021,256**. The 659,253 guard fires are **1.631243% of network decisions**, or 0.521415% of all decisions. Correct wording: “All policies made 126.4 million decisions; networks made 40.4 million, with guard fires on 1.63% of those.” The JSON’s generic `decisions` and `guard_fires` totals themselves are correct. Evidence: [claim-evidence.json](sol-recount/claim-evidence.json). Command: `$P sol-recount/claim_audit.py`.

**CLAIM — “New builds made from cards the networks trained on” needs the protocol’s closure exception.** `h_fl1242` contains Elemental HERO Bubbleman (79979666), absent from Main, Extra and Side of every training deck. It enters through v1’s script closure and is explicitly allowed by the protocol. It is the only such card used in these 488 builds, and appears in just that one build. The report’s title/interpretation should say “builds satisfying the frozen known-card coverage rule, including its closure exception,” rather than literally asserting that every card was played during training. This does not invalidate selection or explain the aggregate pass. Evidence: `selection.json` → `closure_only_cards`, plus the source/env deck comparison. Command: `$P -m json.tool sol-recount/selection.json`.

**CLAIM — The known block includes self-matchups.** “Each training list met the others” should read “each training list met all 21 training lists, including itself.” The matrix contains 441 ordered X/O pairs, not 420. The diagonal contributes 294 cells / 9,408 games. This is the frozen design, not an implementation error. Evidence: regenerated jobs and `selected-cells.json`/raw chunk 000; command: `$P sol-recount/audit.py` (which verifies the full jobs hash).

**CLAIM — The exact 3.8-hour wall duration is not independently established by these retained records.** The journals have no timestamps. The first-cell elapsed times sum to 55.541407 worker-hours; adding replay elapsed times gives 55.907286 worker-hours, or 3.727152 hours divided by 15. Fifteen worker logs exist. These observations are consistent with a 3.8-hour wall run, but cannot reconstruct startup, idle time or the first/last wall clock. Keep the duration as operator-reported, or retain a timestamped run envelope. Evidence/command: `$P -m json.tool sol-recount/claim-evidence.json`.

**NOTE — Search wording and other written values.** To avoid ambiguity, “Search left the network’s choice on 16.2%” should say “Search departed from the network’s choice on 16.2%; it retained it on 83.8%.” The records contain 4,935 departures / 30,468 searched decisions. The 1,861 fallbacks / 30,468 decisions are 6.108048%; each game’s refusal count equals eight times its fallback count, supporting the first-eight-refused explanation. I found no other numeric error in the written distance/archetype tables, known-block averages, Blackwing exclusion, two Dragon Turbo rates, five lowest gaps, or search game statistics. The M2 comparison figures quoted there agree with `docs/edison-search.md`. Descriptions of the future Stage B plan and historical training exposure are not independently proved by duel traces.

**NOTE — Arms and counted side agree.** For pilot, the counted side’s deck is X and its policy is the designated greedy, guarded network; O is heuristic. For face, the counted side’s deck is O and its policy is that network; X is heuristic. Mirror is heuristic in both seats and counts X. Deck order is reversed when necessary to put that side in its recorded seat. This holds in both blocks for every row’s independent policy/deck job hash. Wins use `winner == counted seat`, draws would be non-wins. The new game reruns reproduce the traces with these exact mappings. There is no face/pilot inversion, heuristic counted as a network, or seat denominator inversion in the gated rates. Evidence/commands: `$P sol-recount/audit.py` and `$P sol-recount/rerun_cells.py`.

**ARTIFACT — The tested policy includes the repetition guard; its causal contribution is not isolated.** Guard rates per network decision are 1.612394% known pilot, 1.609532% known face, 1.664801% held pilot and 1.613750% held face. The guard selects legal alternatives from the network ranking; it does not award winners or turn truncations into wins. Its fire counter includes revisiting a state even when no different legal action remains, so a fire need not mean a changed choice. All first-run duels terminated naturally: 768,333 by LP and 3,123 by inability to draw. The longest was 1,582 decisions, below the 10,000-decision limit; the actor rejects truncations. There is no recorded forced repetition adjudication masquerading as a win. These records establish the performance of the frozen network-plus-guard system; without a guard ablation they do not establish the bare networks’ rates or whether a guard choice changed a particular winner. Evidence: `claim-evidence.json`, `actor.py`’s `RepeatGuard` / `run_cell`, and `ygopro.h`’s terminal handling. Command: `$P sol-recount/claim_audit.py`.

**ARTIFACT - Alternate artwork inflates logical build diversity and some distances.** The frozen definition uses passcode multisets. Card 81439174 is the alternate-art alias of Foolish Burial 81439173 in the frozen database. Normalizing artwork aliases (the same alias-within-20 convention used in `decks.py`) makes `h_fl75591` and `h_fl85597` identical Main/Extra decks: their only card-copy difference is these two Foolish Burial IDs. Neither matches any training build after normalization. Seven held builds use an artwork alias; eleven builds have a nearest-training distance reduced by two after normalizing both held and training cards, with no twin changes. `h_fl66941` moves from distance 20 to 18, so a logical-card F has 101 builds. The two duplicate Frog OTK entries have reported distances 8 and 10 but both normalize to 8. `h_fl81932` also moves from 10 to 8. Thus there are 488 distinct passcode builds, but only **487 distinct logical-card builds**, and the stated far-set diversity should be understood as passcode distance. This is a semantic setup artifact, not a failed check against the frozen passcode definition.

I performed a post-hoc sensitivity analysis on the existing games: keep the lower-numbered duplicate representative `h_fl75591`, remove `h_fl85597`, normalize distances/twins for grouping, and repeat my own 10,000-draw bootstrap. This changes no played game or network input. On 487 builds, pilot is **80.566148%**, face **71.446090%**, and gap lower endpoint **-1.133915 points**. On logical-card F (101 builds), pilot is **81.734245%**, face **77.822961%**, and gap lower endpoint **-1.267852 points**. Every rule still passes. This sensitivity is not a replacement for the frozen test. Evidence: [alias-evidence.json](sol-recount/alias-evidence.json), [alias-sensitivity/recomputed.json](sol-recount/alias-sensitivity/recomputed.json). Command: `$P sol-recount/alias_audit.py`.

**ARTIFACT — The deck modifications and sampling limits are real, planned scope restrictions.** All 542 eligible lists collapse to the prescribed 488 unique passcode Main/Extra builds under their lowest-numbered representative. Recomputed exclusions, variants, twins, distances and far flags agree. No held build is exactly a training build, and no two chosen held builds are identical as passcode multisets (the artwork-normalized exception is discussed above). Every deployed held Main/Extra agrees card-for-card with its source after dropping Side and Gottoms: 27 eligible lists / 24 distinct tested builds lose Gottoms, exactly the declared rule. No other missing card was found. Therefore the result applies to these modified game-1 builds; it is not a claim about the original complete decks, siding, or untrained identities generally. I trusted the frozen corpus production-legality flags for the full corpus selection; the rerun actor’s production initialization also successfully loaded all 488 held builds. Evidence/command: `$P sol-recount/setup_audit.py`; see `deck-audit.json` and `selection.json`.

The weak heuristic and corpus composition also limit the interpretation. Blackwing + Vayu Turbo + Hero Frog are 371/488 builds (76.024590%). Equal archetype weighting gives 79.586812% pilot, so the high pilot rate survives that descriptive reweighting. The raw pilot-minus-twin gap is also close to zero before mirror correction (−0.704 points all, −0.821 F). Removing Blackwing after the run gives face 79.433357% and mirror 50.150560%, as the report says. These checks support the narrowly defined pass; the mirror correction and post-hoc archetype cut cannot establish general strength against people or a format-wide effect. Evidence/command: `$P -m json.tool sol-recount/extras.json` and `$P -m json.tool sol-recount/recomputed.json`.

**NOTE — Provenance and search integrity.** All 534 checked frozen hashes match: manifest inputs, all 509 training/held deck files, three checkpoints, three native nets, native module, database, Lua aggregate and frozen search binary. HEAD is `555f75c`; `86ddb40` changes only the manifest on `bf2f736`. The independent search draw reproduces all 240 pairs and all 960 search/plain TSV jobs; every result has the expected id, full job string, binary build id and terminal winner. All 11 local search registry deck-byte hashes equal the recorded hashes, and each registry has the frozen 21 names. All 11 search attempts end successfully without worker failure. No missing/void comparison or draw is present.

Search recount: 381/480 = 79.375% search, 244/480 = 50.833333% plain, gain 28.541667 points. Seat 0 is 82.083333% versus 49.583333%, gain 32.5; seat 1 is 76.666667% versus 52.083333%, gain 24.583333. F has 80 comparisons / 10 builds, 78.75% versus 58.75%, gain 20 points. My independent 10,000-build bootstrap (`default_rng(88810003)`) gives all-pairs [23.958333, 32.916667] points, versus reported [23.958333, 33.125]; seat and F interval endpoints match. All native aggregate counters, Wilson bounds and rounded CPU summaries agree with the report.

The 24 voided sampled worlds belong only to `g31_0`, `h_fl39260` versus `g_lightsworn_fairy`, seed 88800031. They are rollout failures, not unfinished real duels counted as wins. That real duel finished by deck-out, and its complete result, moves, CRC, search logs and all 24 world failures reproduce exactly. The unsupported-prompt diagnostic is therefore a genuine search limitation, already disclosed, with no failed real game hidden. Evidence/commands: `$P sol-recount/search_audit.py --inspect-only`, `$P -m json.tool sol-recount/search-recomputed.json`, `$P -m json.tool sol-recount/search-differences.json`, and `$P -m json.tool sol-recount/search-reruns.json`.

**NOTE — Workspace and execution limits.** All new scripts, registries, logs, filtered training copies and summaries are under `sol-recount/`, with this report at the root. I did not run any `stage_a.py` subcommand, fetch anything, install packages, commit, edit a protected input, or write into `m3-corpus`. Reruns used this worktree’s deck files. The cell rerun uses four actor processes; search uses at most three binaries each with one worker. These stages were sequential and stayed below 12 concurrent processes. No fresh diagnostic game or reserved 8664xxxx seed was needed.

**Reruns.** The sample RNG is Python `random.Random(20261110)`: one cell from each block/arm/optimizer/seat stratum (28), then 14 additional cells. The 42 cell IDs are:

`1248, 1300, 2142, 2293, 2684, 3381, 4017, 4049, 4621, 4741, 5143, 5186, 5216, 6010, 13393, 17375, 18961, 21453, 38395, 40330, 42566, 46227, 53946, 54105, 57286, 58619, 69377, 80370, 81214, 81384, 86892, 98095, 98509, 108888, 126396, 128103, 128484, 130280, 131268, 141992, 142291, 148064`

Each full cell ran once at 1 and once at 4 env threads, on its recorded consecutive duel seeds: **84 cell runs / 1,120 duels, zero differing cells and zero differing runs**. The comparison includes every duel field, status, all non-time counters and the journal’s learner-decision field. Independently verified job/row metadata remain fixed. Only elapsed time and counters ending in `_seconds` are excluded. Evidence: [cell-reruns.json](sol-recount/cell-reruns.json); individual full rerun results are `sol-recount/cell-<id>-<threads>.json`. Command: `$P sol-recount/rerun_cells.py`.

Search sampled one pair per loading group using `random.Random(20261111)`, then added pair 31 to test its recorded rollout failure. Pair IDs are **14, 32, 59, 73, 106, 130, 160, 182, 191, 213, 233, 31**. Every pair ran both seats, search and plain, on seed `88800000 + pair id`: **12 deck pairs / 24 paired seat comparisons / 48 games, zero differing games**. Result ids are `g<id>_0`, `g<id>_1`, `p<id>_0`, `p<id>_1`; only `search_cpu` and `total_cpu` are excluded. All other fields, including full moves, CRC, logs and rollout/refusal counters, match. Evidence: [selected-search.json](sol-recount/selected-search.json), [search-reruns.json](sol-recount/search-reruns.json), and `sol-recount/search-group-*/`. Command: `$P sol-recount/search_audit.py`.
