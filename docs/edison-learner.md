# Edison M1 learner: network, league trainer and pilot

Prepared November 4, 2026. Base commit 99cfd45 on branch edison. Simulator: native module
sha256 c5ecba9c, assets per `tests/edison/agent_assets.lock.json`. The pilot numbers below belong to this
older binary; [the re-baseline](#re-baseline-on-the-hardened-simulator) has the numbers on the hardened one. No
runtime source, fixture or lock file changed; all new code is under `tools/edison/learner/`.

## M1 exit on the fixed Deck labels

**M1's exit rule is met, on fresh seeds and the [fixed Deck labels](edison-deck-labels.md). That means these networks beat one weak heuristic on deck lists they trained on. It is not human, tournament or held-out-deck strength.** Three optimizers trained for 245,760 gauntlet duels each on certified module b8fcd2bc (about 6 hours each, sharing the machine). No run aborted, and the only training void was one 8-game cell at the decision limit. The [protocol](../tests/edison/reports/retrain-2026-11-04/protocol.md) and [final-test protocol](../tests/edison/reports/retrain-2026-11-04/final/protocol.md) were hashed before training. [Evaluation summary](../tests/edison/reports/retrain-2026-11-04/summary.json), [final summary](../tests/edison/reports/retrain-2026-11-04/final/summary.json).

| Evaluation, seeds 82M, consumed | Round 160 | Round 320 | Round 640 | Round 1280 | Round 2560, by optimizer | Round 2560 | NumPy baseline |
| --- | ---: | ---: | ---: | ---: | --- | ---: | ---: |
| Fixtures, 768 games each | 44.4 | 49.6 | 51.7 | 60.4 | 73.1, 65.2, 68.1 | **68.8%** | 49.2% |
| Gauntlet, 1,344 games each | 59.0 | 64.4 | 67.6 | 75.3 | 81.6, 77.4, 79.0 | **79.3%** | 54.1% |

G1 `MORE_GAMES_HELP` and G2 `STILL_RISING` on both blocks (round 2560 minus 1280 is +8.4 and +4.0 pp). G3 `CANDIDATE`. G4 `NET_AHEAD`, with 71.4% head to head against the baseline. G5 is valid, with 0 void games in evaluation. So training had not plateaued, and the same settings with more games may go further. That is untested.

Round 1280 scored 60.4% and 75.3%, against 58.2% and 70.5% for the earlier run on the defective interface. Seeds differ, so this is not a clean measure of the fix, but the fix did not cost strength.

| Final test, seeds 83M, consumed | 1702 | 1703 | 1704 | Mean | Lowest lower bound | Verdict |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| F1, fixtures, 3,072 games each | 70.18 | 64.55 | 68.20 | **67.64%** | 62.84 | PASS |
| F2, gauntlet, 5,376 games each | 82.33 | 77.29 | 79.85 | **79.82%** | 76.15 | PASS |

**Verdict: `M1_EXIT_MET`.** Fixture decks pooled are Blackwing 54.75%, Frog Monarch 68.23% and Machina Gadget 79.95%. Fixture seats are 71.14% and 64.15%, and gauntlet seats are 81.91% and 77.74%. There were 0 void games and 0 draws in F1 and F2, and 52 replayed cells with 0 mismatches. Without the guard the gauntlet scores 79.71% with 96 void games at the decision limit, and the guard's paired effect is +1.10, -0.52 and -0.32 pp.

**Checks.** My recount from raw cells matches, and the network hashes match the training checkpoints. [Sol's independent read](../tests/edison/reports/retrain-2026-11-04/review/reviewer_report.md) scored the raw cells with its own code and got the same verdict. It found no privacy leak the study could reach, including in new duels past the first turn: 180 compared observations through turn 8 with different hidden cards, and 1,008 full duels on the real networks reaching turn 49 with 0 prompt-audit violations. These are bounded checks, not a proof. I confirmed its findings below from the raw data or the code.

**What limits this result:**

- **The heuristic is weak.** It has a whitelist of activations. In Sol's 1,008 duels it took 0 of 857 Substitoad offers, 0 of 1,232 Solemn Judgment offers and 0 of 259 Honest offers. It also sees fewer inputs than the network. How much of the margin comes from this was not measured.
- **Passing pools hide losing matchups.** Network Blackwing against heuristic Machina Gadget wins only 424 of 1,536 games (27.60%), and optimizer 1703's Blackwing in seat 1 wins 254 of 512 (49.61%). F1 only tests pools, so neither breaks the rule, but "wins every matchup" is not supported.
- **Decks and opponent are familiar.** Training and testing share all 21 lists and the heuristic. Fresh seeds are new starts, not new decks, and M3 measures generalization.

**Test gaps found, neither changing a game or the verdict:**

- **Mirror controls could not fail.** Both challenger seats replay the same duels, so the aggregate is 50% by construction. The reports now add seat 0's rate, counting each duel once: 54.75% on final fixtures and 50.93% on the final gauntlet. Those fit the networks' seat gaps.
- **Trace hashes omit history.** `actor.py` hashes cards, selection, global, actions and decision, but not `h_actions_`, which the network also reads. A history-only divergence with unchanged actions would pass replay. No such divergence is known, and Sol's paired checks compare history directly. Fixing it changes every trace hash, so it waits for the next change to the actor.

Seeds consumed: 81000000-81245759 training, 82000000-63 and 82010000-7 evaluation, 83000000-255 and 83010000-31 final test (`evaluate.py` now refuses 83M), and 84000000-84002300 and 84010000-7 by Sol's review.

## Long gauntlet run and M1 final test

**This run did not meet M1's exit, and every number in this section comes from an interface with [a known Deck-label defect](#known-defect-deck-option-labels). [The retrained result](#m1-exit-on-the-fixed-deck-labels) replaces it.** Three optimizers trained for 122,880
gauntlet duels each on the GPU, on certified module 67423a99, with 0 void games and no aborted run (about 3.6 hours each, sharing the machine). The [protocol](../tests/edison/reports/gauntlet-training-2026-11-04/protocol.md)
was hashed before training. [Summary](../tests/edison/reports/gauntlet-training-2026-11-04/summary.json). All play is greedy, against the heuristic.

| Seeds 78M, now consumed | Round 160 | Round 320 | Round 640 | Round 1280, by optimizer | Round 1280 | NumPy baseline |
| --- | ---: | ---: | ---: | --- | ---: | ---: |
| Fixtures, 768 games each | 43.2 | 43.5 | 52.1 | 57.0, 62.9, 54.6 | **58.2%** | 46.2% |
| Gauntlet, 1,344 games each | 56.3 | 60.6 | 66.4 | 70.8, 73.9, 66.9 | **70.5%** | 54.1% |

| Predeclared | Result | Verdict |
| --- | --- | --- |
| G1, more games | +15.0 pp on fixtures, +14.2 pp on the gauntlet, every optimizer above 0 | **MORE_GAMES_HELP** on both |
| G2, plateau | +6.1 pp and +4.1 pp from round 640 to 1280 | **STILL_RISING** on both |
| G3, candidate | Lowest optimizer 54.6% and 66.9% | **CANDIDATE** |
| G4, baseline | Paired gain +11.98 and +16.44 pp; 63.98% head to head | **NET_AHEAD**. Budgets differ, so this does not rank the methods |
| G5, failure | 0 void games in training and evaluation; 129 replayed cells, 0 mismatches | Valid |

Round 1280 scored 97.3, 98.6 and 97.0% against random. A separate recount of the raw cells matched every total, and each evaluated network's hash matched its training round.

**Final test.** The [rules](../tests/edison/reports/final-test-2026-11-04/protocol.md) were committed before any game on these seeds and before the per-deck tables were read.
[Summary](../tests/edison/reports/final-test-2026-11-04/summary.json). Verdict: **NOT_MET**.

| Fresh seeds 79M | By optimizer | Mean | Lower bounds | Verdict |
| --- | --- | ---: | --- | --- |
| Fixtures, 3,072 games each | 55.92, 61.46, 50.39 | **55.92%** | 54.16, 59.72, **48.62** | **F1 FAIL** |
| Gauntlet, 5,376 games each | 72.04, 74.03, 67.84 | **71.30%** | 70.83, 72.84, 66.58 | **F2 PASS** |

F1 fails two checks: optimizer 1704's lower bound is below 50%, and Blackwing (48.67%) and Frog Monarch (45.38%) are below 50%, with Machina Gadget at 73.73%. Seats are
57.94% and 53.91%. On the gauntlet, seats are 73.51% and 69.10%, and the lowest lists are `g_frog_otk` 49.61%, `g_diva_frog` 54.17% and `blackwing` 55.08%. There were 0 void games, 52 replayed cells with 0 mismatches, and a recount from raw cells matched.

**Guard diagnostic.** Without the guard the gauntlet scores 70.69%, and the guard's paired effect is +0.71, +0.30 and +0.56 pp. So the guard is not what produces the gauntlet result. It is still needed: optimizer 1704 lost 64 games (1.2%) to the decision limit without it.

These networks beat a heuristic that they also trained against, and the heuristic is weak with some lists (it wins 22.3% with Frog Monarch in the logged fixture cells). This is not tournament strength.

## Known defect: Deck option labels

**Fixed since, in [the Deck label report](edison-deck-labels.md), and [retrained](#m1-exit-on-the-fixed-deck-labels). Other results in this report still belong to the defective interface.**

**When a player chooses a card from the Deck, the policy sees the wrong card names. This affects every policy trained or evaluated so far.** The owner's description of a Frog combo led to the replays that exposed it.

- **Cause.** For Deck candidates the core's `card::get_select_info_location` sends a masked sequence (0, 1, 2 and so on, in prompt order) to hide real positions. `ygopro.h` skips the code in that message and treats the masked sequence as a real position, so the
  option's card id and row come from whichever card sits at that real Deck position. The prompt's true code is available and ignored.
- **Evidence.** In one replayed prompt the engine offers Treeborn, Treeborn, Swap and Swap, while the policy sees ids 433, 263, 112 and 347. Over 2,184 Substitoad Deck prompts from the logged networks, the chosen label matched the summoned card 5.1% of the time, against
  19.7% for a random pick. Only 29.4% of labels were Frogs, though Substitoad can only summon Frogs. (Corrected: the first count, 5.6% of 2,042, also read some tribute prompts.) The engine itself obeys the card: all 638 summons I checked were Frogs.
- **Consequences.** Policies choose blind among Deck candidates. Because `card_hidden` unhides rows matching an offered spec, **the identities of hidden cards at those real Deck positions are also exposed to their owner.** That breaks the contract that hidden Deck contents stay hidden. Rows are sorted by identity and carry no position, but which
  cards appeared is still information. Whether any policy gained from it was not measured. Rules are not affected.
- **Scope.** `MSG_SELECT_CARD` and `MSG_SELECT_UNSELECT_CARD` use this path, so all Deck searches should be affected. I measured Substitoad's prompts and read Swap Frog's. Other cards are inferred from the code path.
- **After the fix,** seeded games change, and networks must be retrained and re-evaluated on fresh seeds. All results above belong to the defective interface.

## The Substitoad line

The owner described a Frog line: use Substitoad repeatedly to pull Frogs through the field, Swap Frog to bin more, finish on Substitoad and Dupe Frog, and later tribute the revived Treeborn for Monarchs. `line_probe.py` replays the scored games and `frog_line.py`
counts this, on consumed seeds. Every replayed game matched its stored trace. [Summary](../tests/edison/reports/frog-line-2026-11-04/summary.json). Frog Monarch fixture, greedy:

| 768 games per row, except the heuristic's 256 | Heuristic | Round 160 | Round 1280 |
| --- | ---: | ---: | ---: |
| Won | 22.3% | 29.9% | 43.6% |
| Games where Substitoad could activate, then did | 126, then 0 | 268, then 208 | 439, then 415 |
| Own turns with 2+ / 3+ / 5+ activations | 0 / 0 / 0 | 76 / 32 / 3 | 96 / 37 / 15 |
| Treeborn revived, of offers | 175 of 187 | 2,148 of 2,261 | 2,815 of 2,844 |
| Treeborn tributed for a Summon | 57 | 538 | 657 |
| Turns ending on Substitoad and Dupe Frog | 9 | 7 | 13 |
| Full line in one turn | 0 | 2 | 2 |

"Full line" is my definition: 3+ activations, Treeborn tributed to Substitoad, Swap Frog sending a Substitoad, and ending on Substitoad and Dupe Frog. So **the net plays the end of the plan and fragments of the combo, and rarely the whole combo.** It revives Treeborn almost every time and tributes it for Summons. Its most common Substitoad tribute is Substitoad
itself (502 times), which gives up the end board. It cannot aim its Deck picks because of the defect above, which may be why the line is rare. That explanation is untested. The win-rate gain from round 160 to 1280 is not shown to come from this line. Whether the full line is strong in this simulator has not been measured either.

### On the fixed Deck labels

[Summary](../tests/edison/reports/retrain-2026-11-04/frog-line.json), on the consumed 82M evaluation games. Every replayed game matched its stored trace. Frog Monarch fixture, greedy:

| 768 games per row, except the heuristic's 256 | Heuristic | Round 160 | Round 1280 | Round 2560 | Old build, round 1280 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Won | 20.3% | 26.8% | 54.9% | 70.6% | 43.6% |
| Games where Substitoad could activate, then did | 153, then 0 | 364, then 322 | 539, then 506 | 558, then 540 | 439, then 415 |
| Own turns with 2+ / 3+ / 5+ activations | 0 / 0 / 0 | 70 / 36 / 6 | 269 / 198 / 77 | 288 / 192 / 76 | 96 / 37 / 15 |
| Treeborn revived, of offers | 91 of 102 | 2,448 of 2,953 | 3,887 of 3,897 | 3,998 of 4,054 | 2,815 of 2,844 |
| Treeborn tributed for a Summon | 38 | 611 | 1,097 | 1,136 | 657 |
| Turns ending on Substitoad and Dupe Frog | 6 | 2 | 26 | 39 | 13 |
| Full line in one turn | 0 | 0 | 5 | 4 | 2 |

**The label fix shows up directly.** Across these logs the chosen label matched the summoned card at all 5,034 Deck prompts, and all 26,764 labels shown were Frogs. The same counter on the old logs gives 5.1% and 29.4%, so it separates the builds. (The counter first read 93.6%. Those misses were tribute prompts, taken when a single Deck candidate made the pick automatic, so it now requires the cost to have been paid.)

**At round 1280, turns with 3+ Substitoad activations are about 5 times as common as on the old build (198 against 37). At round 2560 they are 192 of 5,201 own turns. The whole line is still rare, at 4 turns.** The networks differ from the owner's plan in one clear way: at round 2560 Swap Frog sends Treeborn most often (352 times) and Substitoad less (108), which puts a reviver in the Graveyard instead of thinning Substitoads from the Deck. It still most often tributes Substitoad itself (634), which gives up the end board. Whether its version or the full line is stronger in this simulator has not been tested, and the win-rate gain is not shown to come from this line.

## Re-baseline on the hardened simulator

Supervisor's replay of the frozen models on the certified module 038e6dd1, with no retraining. The
[protocol](../tests/edison/reports/rebaseline-2026-11-04/protocol.md) was hashed before play, on fresh seeds
75000000-75000063 and 75010000-75010007. 42,624 games, 95 cells replayed with 0 mismatches, 0 void games.
[Summary](../tests/edison/reports/rebaseline-2026-11-04/summary.json). All 13 learner checks pass on this module,
including identical games against the unmodified production worker.

| Fixtures, greedy, 768 games per optimizer | Against heuristic, by optimizer | Mean | Before | Against random | Head to head with baseline |
| --- | --- | ---: | ---: | ---: | ---: |
| NumPy baseline | 39.7, 47.4, 44.8 | 44.0% | 45.2% | 93.4% | |
| League, 3,072 duels | 31.4, 31.8, 33.1 | 32.1% | 32.8% | 91.2% | |
| League, 15,360 duels | 49.2, 47.5, 46.7 | 47.8% | 49.8% | 95.3% | |
| League, 30,720 duels | 51.8, 52.1, 52.1 | 52.0% | 51.0% | 96.7% | 58.6% (63.3, 52.2, 60.2) |
| Gauntlet training, 15,360 duels | 40.5, 43.1, 40.9 | 41.5% | 43.4% | 94.6% | 50.3% (53.3, 49.0, 48.7) |
| Heuristic | | 50% | | 95.4% | |

| Predeclared | Result | Verdict |
| --- | --- | --- |
| H1, within 3 pp of before | Shifts -1.23, -0.73, -1.97, +0.99, -1.91 | **CARRIES_OVER** for all five |
| H2, league 30,720 over baseline | Paired gain +8.03 pp (+12.11, +4.69, +7.29); head to head 58.55% | **NET_AHEAD**. Budgets differ, so this does not rank the methods |
| H3, candidate | Best 51.99% mean, lowest 51.82%; needs 55% and 50% | **NO_CANDIDATE** |
| H4, repetition guard | 0 decision-limit voids on the gauntlet | **GUARD_WORKS** |

The net's gain over the baseline is now positive for every optimizer, including 1703, which trailed before. The blocks differ, so this may
be sampling noise rather than an effect of the rule fixes.

Gauntlet, descriptive, 1,344 games per model, against the heuristic. Networks use the guard: gauntlet-trained net 55.1% (2222/4032), NumPy baseline 53.8%
(2170/4032), fixture-trained net 48.1% (1941/4032), all 0 void. So the earlier result still holds: the fixture net does not generalize. **The guard fires
about once per game** (4,141 and 4,064 times in 4,032 games), far more than the livelock alone, so it also changes ordinary repeated decisions. Whether
that helps or hurts play was not measured. It stays off on the fixture block.

**Review corrections to the report below:**

- **Seeds:** the proposed final-test block 74000000-74000063 is not fresh, because the hardening soak used 74000000-251. `evaluate.py` now refuses the 74M block.
- **XX-Saber Gottoms:** hardening made it legal, so `decks.py` would have played the card in 2 lists although code list v1 lacks it. It stays removed for v1. The list regenerates to the same hash.
- **Archived check:** 132/288 and 259/288 belong to the older binary and are not expected to reproduce now.
- **Recount:** I recomputed the pilot and gauntlet tables from the raw cells, and they match.

## Result

**Write a smaller Edison network. At the baseline's budget it loses to the baseline; with more games
it passes it, reaches heuristic parity, and does not clearly beat the heuristic.**

- **Equal budget, 3,072 duels: below the NumPy baseline.** This is the predeclared result. League arm:
  32.8% against the heuristic, against 45.2% for the baseline, a paired -12.45 pp. Head to head it wins
  42.8%. The baseline's 128 hand-built tactical features are worth more than card identity at this budget.
- **Larger budget: more games help a lot.** 16.5%, 32.8%, 42.1%, 49.8% and 51.0% at 768, 3,072, 7,680,
  15,360 and 30,720 duels. At 30,720 it scores 97.3% against random (the heuristic gets 96.1%) and 59.5%
  head to head against the baseline.
- **No checkpoint qualifies for a final test under the predeclared rule (mean 55%).** One optimizer of
  three stays below the baseline against the heuristic. The curve flattens at parity between 15,360 and 30,720 duels.
- **New finding: greedy play can livelock.** On the gauntlet, 9 cells voided because a net re-activated a
  free effect that an opposing Destiny HERO - Plasma negates, past 10,000 decisions. No fixture game did.
  Greedy evaluation needs a repetition guard before any gauntlet test.

M1's exit, clearly beating the heuristic on fresh seeds, is **not** met.

## Decision: write a smaller network

Measured on real Edison observations, one thread, quiet machine:

| Batch | Edison net, ms | Upstream `RNNAgent` defaults, ms | Upstream / Edison |
| ---: | ---: | ---: | ---: |
| 1 | 0.21 | 5.03 | 24× |
| 4 | 0.94 | 11.3 | 12× |
| 16 | 3.11 | 41.9 | 13× |
| 64 | 28.1 | 203 | 7× |
| 256 | 135 | 930 | 7× |
| Parameters | 207,800 | 3,841,482 | 18× |

The upstream network does run on these observations and returns finite values. I did not train it on
Edison, so this decision rests on cost and contract, not on a learning comparison.

| Upstream part | Decision | Why |
| --- | --- | --- |
| LSTM, 512 units per seat | Rejected | A search leaf would need hidden state per world. `h_actions_` gives memory statelessly |
| 1,024-dim frozen embeddings, `n_embed=999` | Rejected | The weights would need a download. Replaced by a trained 32-dim table on a generated list |
| FiLM self-attention over 256 option slots, 160-row attention | Rejected | Cost. About 60% of rows are hidden deck, hand and Extra with no information beyond public counts |
| `Encoder` inputs | Rejected | Cannot read `decision_` or `selection_` |
| `oppo_info` critic and `mask_` | Rejected | A privileged path. No centralized critic was added here at all |
| `cleanba.py` | Rejected, not run | Async thread actors, no league, no anchor, no fail-closed path, and `jax.device_put_sharded` was removed from public JAX in 0.10 |
| Thermometer ATK/DEF/LP bins, action-to-row pointer, mean offered action | **Kept** | Cheap and sound |

**The network** (`net.py`). Rows take a trained identity embedding, categorical columns, ATK/DEF
bins, type bits, `selection_` flags and an is-source flag. A context token takes `global_`,
`decision_` and pooled own history. Two pre-norm attention layers (64 channels, 4 heads) run over the token and
rows. Each option is scored from its fields, its identity, the transformed row it points at, the state
and the mean offered option. The actor starts uniform and the critic at zero, as the baseline does.

**Code list** (`decks.py`, `codes_v1.txt`, sha256 53fcea4d). 509 ids: 248 from the 21 lists including
Side, all 238 database tokens, and 23 from alias and script-reference closure run to a fixed point. It
carries all tokens because scripts often compute token ids. The 2 lists with XX-Saber Gottoms play with it
removed in a generated copy, as in the audit. Checkpoints store the list's hash and fail closed on a
mismatch. Moving to the full pool needs a new list and a table remap by passcode, which is not built.
**Soak:** every ordered pair of the 21 lists, random and heuristic play, 1,680 duels, 0 void.

**Trainer** (`train.py`, `actor.py`). A cell (one vector env, consecutive seeds, one policy per seat) is a
pure function of seeds and checkpoints. Actors are subprocesses, so a native abort voids only its
cell. League rounds rotate thirds: frozen heuristic, a past self drawn uniformly from snapshots every 4
rounds, and current self in both seats. It supports multiple decks and both seats, resumes exactly, and
refuses a resume if configuration, sources, binary or code list differ.

**Fail-closed behavior.** `obs.py` ports the worker contract as vectorized checks and adds bounds for
every embedded column, because `nn.Embed` returns NaN out of range (confirmed). Truncation, decision limit,
contract failure, native abort, worker death and native diagnostics all void the whole cell.

## Checks

`test_learner.py`, all 12 pass ([tests.json](../tests/edison/reports/learner-2026-11-04/tests.json)):

| Check | Result |
| --- | --- |
| Same games as the unmodified `agent_match` worker on the 67-id list | 16/16 identical seed, length, winner and reason (heuristic, random, sampled NumPy) |
| Baseline through this harness, validation seeds | **132/288 greedy against heuristic and 259/288 against random, equal to the archive** |
| Cell replay across workers and env thread counts | Identical traces and rollout hashes |
| Resume after a stop against uninterrupted | Identical parameters, optimizer and rollouts each round; a changed config is refused |
| Different opponent list, same seed | Packed network input byte-identical; `info:deck` and `mask_` never read |
| Reordered rows and options | Logits reorder with them, value unchanged |
| Planted violations, tampered checkpoint, wrong code list, truncation, `os.abort()` | All rejected or voided, with no result counted |

Evaluation replays: 69 cells on a different thread count, 0 mismatches.

## Pilot

Protocol written and hashed before any trained network played an evaluation seed:
[protocol.md](../tests/edison/reports/learner-2026-11-04/protocol.md), sha256
`a2c659c3d59493371a11ea63ca2f50602bf80e4bfa470522a06307491348b96e`. 73,176 evaluation games.
[Summary](../tests/edison/reports/learner-2026-11-04/pilot-summary.json).

### Fixtures, greedy, pilot seeds 73900000-73900063

768 games per model and opponent, 6 ordered pairings, both seats. Win rate per optimizer 1702, 1703, 1704.

| Policy | Training duels | Against heuristic, by optimizer | Mean | Against random | Head to head with baseline |
| --- | ---: | --- | ---: | ---: | ---: |
| **NumPy baseline** | 3,072 | 41.8, 51.4, 42.4 | **45.2%** | 92.7% | |
| Net, baseline's schedule | 3,072 | 30.6, 29.7, 21.1 | 27.1% | 82.3% | 34.5% |
| Net, league | 768 | 20.6, 16.4, 12.6 | 16.5% | | |
| **Net, league** | **3,072** | 33.7, 30.6, 34.0 | **32.8%** | 91.4% | 42.8% |
| Net, league | 7,680 | 43.2, 44.4, 38.7 | 42.1% | | |
| Net, league | 15,360 | 50.1, 50.5, 48.7 | 49.8% | 96.6% | 54.4% |
| Net, league | 30,720 | 48.8, 49.6, 54.6 | 51.0% | 97.3% | 59.5% |
| Net, gauntlet training | 15,360, other decks | 42.1, 43.9, 44.4 | 43.4% | 94.7% | |
| Heuristic | | | 50% by construction | 96.1% | |

Same models on validation seeds 40000000-40000007, 96 games each (288 per arm):

| Policy | Against heuristic | Against random |
| --- | ---: | ---: |
| NumPy baseline | 132 (45.8%) | 259 (89.9%) |
| Net, schedule, 3,072 | 85 (29.5%) | 240 (83.3%) |
| Net, league, 3,072 | 102 (35.4%) | 258 (89.6%) |
| Net, league, 15,360 | 142 (49.3%) | 276 (95.8%) |
| Net, league, 30,720 | 148 (51.4%) | 283 (98.3%) |

### Predeclared conclusions

| Rule | Subject | Paired gain over baseline against heuristic, pp | By optimizer | Verdict |
| --- | --- | ---: | --- | --- |
| R1 equal budget | League, 3,072 (primary) | -12.45 ± 7.26 SD | -8.07, -20.83, -8.46 | **BELOW_BASELINE** |
| R1 equal budget | Schedule, 3,072 | -18.10 ± 5.98 | -11.20, -21.74, -21.35 | **BELOW_BASELINE** |
| R2 more games | League 30,720 minus league 3,072 | +18.23 | +15.10, +19.01, +20.57 | **MORE_GAMES_HELP** |
| R3 candidate | Best checkpoint | Mean 51.0%, lowest 48.8%; needs 55% and 50% | | **NO CANDIDATE** |
| R4 void | All fixture comparisons | 0 void | | Valid |

Validation agrees in sign: -10.42 and -16.32.

Descriptive, because the budgets differ: over the baseline, league 15,360 is +4.56 pp (+8.33, -0.91,
+6.25) and league 30,720 is +5.77 pp (+7.03, -1.82, +12.11), with bootstrap over starts +3.3 to +8.2.
Optimizer 1703 stays just below in both. That bootstrap ignores optimizer variance, and optimizer
1703's baseline partner is the strong 51.4% model, so pairing by index matters here.

By deck against the heuristic at 30,720 duels, with the heuristic mirror and the baseline in the same
deck: Blackwing 44.7% (52.7%, 38.7%), Frog Monarch 37.6% (24.2%, 28.3%), Machina Gadget 70.7% (73.0%,
68.8%). Seats 51.5% and 50.5%. So it passes the heuristic with Frog Monarch and trails with Blackwing.

**Equal duels are not equal decisions.** Self-play collects both seats, so at 3,072 duels the league arm
trained on 323,000-333,000 learner decisions, against 222,000-225,000 for the baseline and 231,000 for the
schedule arm. That favors the net, which still lost. The league's lead over the schedule arm (+5.6 pp) may
partly reflect this.

Sampled play, diagnostic only (league 3,072 and 30,720; baseline): 25.0%, 46.5%; 30.9% against the
heuristic, and 85.5%, 96.3%; 87.3% against random. The baseline's 30.9% is close to the archived fresh-seed 30.0%.
Greedy's gain over sampling is 14.3 pp for the baseline, 7.8 pp for the league at 3,072 and 4.5 pp at 30,720, as
the net sharpens: end-of-run entropy is 0.57-0.72 nats, against about 1.0 for the baseline.

### What I think limits it (judgment, not tested)

- **At 3,072 duels:** the baseline starts with ATK margins, a safe-activation flag and summon values. The net
  must learn what a number on a card means, and 3,072 duels is too few. Greedy at 768 duels already shows
  how far behind that start is.
- **Near parity:** against the heuristic the curve gains 1.2 pp from 15,360 to 30,720, while head to head against
  the baseline it gains 5 pp. So it is still improving, but in ways this frozen benchmark barely rewards. Three
  optimizers cannot separate plateau from noise.
- **Candidates for the plateau:** the chain stack is still not observable, which Solemn-style errors need; the KL
  stop binds in most late rounds, so updates per round fall; the learning rate is constant; only a third of
  duels face the heuristic; and there is no search. None of these was tested. I did not tune on evaluation.

### Gauntlet, descriptive only

Seeds 73910000-73910007. 21 lists, each against the next 4 in a cycle, both seats, 8 seeds; 1,344 games per
model and opponent, against the heuristic. No conclusions were predeclared.

| Policy | By optimizer | Wins / completed | Void games |
| --- | --- | ---: | ---: |
| Net, trained on gauntlet, 15,360 | 53.0, 57.3, 56.6 | 2230/4008 (55.6%) | 24 |
| NumPy baseline (never trained on these decks) | 50.1, 56.9, 57.1 | 2204/4032 (54.7%) | 0 |
| Net, trained on fixtures only, 30,720 | 46.6, 43.2, 44.7 | 1786/3984 (44.8%) | 48 |
| Heuristic mirror | | 50% by construction | 0 |

- Counting every void game as a loss, the gauntlet net is 55.3%. The fixtures-only row has 1.2% void, above
  R4's 1%, so **that row is invalid as a comparison** and is shown only as a description.
- The fixture net does not generalize, and the baseline does. Its features are card-blind, which helps
  here: its 54.7% is a real reference, and the gauntlet net at the same budget only ties it.
- The heuristic is weak with some lists: g_gigavise wins 3.1% in the mirror, against 61.5% for the gauntlet net
  and 38.5% for the baseline. Per-deck rows are in the summary; each is 64 games at most, so read them lightly.
- The gauntlet list is a sample of placed decks, so none of this is tournament strength.

### Greedy livelock

9 cells (72 games) voided, all at the 10,000-decision limit, all with `g_hero_frog` opposing the net:

```
step 1498 net activates Lyla, Lightsworn Sorceress ; 1499 selects target ; 1500 opponent declines; repeat
step 1477-1500 net activates Honest, every decision
board: opposing Destiny HERO - Plasma, net's monsters flagged negated
```

Plasma negates the net's monsters, the activation has no lasting cost, resolution changes nothing, and argmax
repeats forever. Sampling escapes it, which is why training never voided. The heuristic has a repeat penalty,
and the baseline has a repeat feature. I replayed one of the 9 cells, so the same cause in the other 8 is
inferred from all having that opponent. **For the conformance workstream:** whether activating these effects
while negated is correct under Edison rulings is unchecked here.

## Speed

Quiet machine (load under 2), Ryzen 7 9800X3D, unless marked. [speed.json](../tests/edison/reports/learner-2026-11-04/speed.json)

| Env steps, random actions, no audit, no policy | Decisions/s |
| --- | ---: |
| 64 envs, 1 / 4 / 8 threads | 3,905 / 13,079 / 18,320 |
| 1 env, 1 thread | 2,835 |

| Full cell: audit, policy, env, bookkeeping | 1 worker, 8 games | 8 workers, 8 games | 8 workers, 64 games |
| --- | ---: | ---: | ---: |
| Net against heuristic | 916 | 3,406 | 7,793 |
| Net self-play | 750 | 3,583 | 7,584 |
| NumPy against heuristic | 2,190 | 12,145 | 14,010 |
| Heuristic mirror | 2,522 | 12,844 | 16,672 |

So actors are 2-4× slower than the NumPy learner. The net takes 42-69% of cell time. Its mean batch at 8 games
is only 2.5, padded to shape buckets, costing 1,311 µs per decision against 210 µs at batch 1 in isolation.
Finer buckets or larger cells are cheap wins. Audit plus pack is 354 µs at batch 1, and 27 µs per decision at
batch 64. The baseline's policy costs about 120 µs per decision (derived).

| Training updates, minibatch 256 | 1 thread | 2 | 4 | 8 |
| --- | ---: | ---: | ---: | ---: |
| Updates/s | 4.25 | 6.02 | 6.82 | 8.02 |

CPU scaling is poor, so 3 runs at 1 thread beat 1 run at 8. Compiling all shapes takes 25-50 s per run.

| Wall clock | 3,072 duels | 30,720 duels |
| --- | ---: | ---: |
| Net, 8 workers + 8 learner threads, quiet | **692 s** measured (collect 136, update 555) | About 80 min, derived |
| Net, 1 worker + 1 learner thread, 6 runs sharing the machine | 1,890-1,977 s | **12,735-13,739 s** measured (3.5-3.8 h) |
| NumPy, archived run | 222-225 s | About 37 min, derived, never run |

The update is 80% of training time, so a GPU learner would help until actors bind, at 3,400-7,800 decisions/s depending on cell size.

## Against the search plan

**What PIMC needs.** The forward-model report gives about 280 µs for restore, step and encode. To keep the
network from dominating, a leaf evaluation should cost about that or less. The arena allows one duel per
process, so a worker blocks on each evaluation, and batch size cannot exceed the worker count.

| Option on 8 cores | Value evaluation | Leaves/s, derived |
| --- | ---: | ---: |
| Inline, batch 1, this net | 0.19 ms | About 17,000 |
| Inline, batch 1, upstream | About 5 ms | About 1,500 |
| GPU server, batch 8 | Round trip L | Beats inline only if L is under 0.19 ms |
| GPU server, batch 16 on 16 SMT workers | Round trip L | Beats inline only if L is under about 0.66 ms |

A GPU round trip under WSL2 was not measured, but I would not expect it under 0.2 ms. **Run this size of
net inline on CPU for search, and use the GPU for training.** About 17,000 leaves/s is roughly 5,400 per option per
second at 3.14 options. With playouts truncated at about 8 network-guided decisions (derived 490 µs each), that
is about 2,000 rollouts/s, or about 640 per option. Nothing in that estimate was run.

**Is it shaped for it? Mostly yes.**

- It is a pure function of one observation with no recurrent state, so any world can be evaluated with no setup.
- The value path needs no actions, though it saves only 3-10%, because attention dominates.
- It has no list-position input. Rows and options are permutation-equivariant (tested), so the prompt-order leak
  in the forward-model report cannot reach the logits through position. History order is pooled as well.
- **The value head is informative and well calibrated** (post-hoc, 3 models at 30,720, dev seeds): correlation with
  the outcome 0.52-0.61, squared error 0.62-0.75 against 1.0 for a constant. Value -0.88 averaged outcome -0.89,
  +0.12 gave +0.19, and +0.87 gave +0.78, in one model. Correlation is about 0.53 on turns 1-4 in the two models I read. Those
  games are net against heuristic, not sampled worlds, and accuracy on determinized positions is untested.

**What is still missing.** The arena is a dedicated native executable, so JAX cannot sit in that process. The net
needs a small native forward pass (dense, attention, embeddings) checked against JAX, and row trimming must move
to C++. Python's audit and pack cost more than the network at batch 1.

## GPU requirements

**Update after install:** Option A works on this RTX 5090, in the ignored `.venv-gpu` (5.5 GB installed), with actors kept in `.venv`. The real update ran at about
220 updates/s after compiling, against 8 on 8 CPU threads. Each new shape compiles first, which took 105 s in round 1 and 45 s in round 2 of the smoke test. Collection is now the limit, at
7-10 s per round against under 1 s per update. Repeated GPU runs differ from round 1 without `--xla_gpu_exclude_nondeterministic_ops=true`. With it, which `train.py` now
sets, repeated and resumed runs are byte-identical in rollouts, parameters and optimizer state over 3 rounds. [Evidence](../tests/edison/reports/gauntlet-training-2026-11-04/gpu-smoke.json).
Learning on the GPU has not yet been compared with learning on the CPU.

The rest of this section is from before the install, from documentation only. The machine: RTX 5090, Windows driver 617.42,
WSL driver 615.78.05, CUDA 13.4 in `nvidia-smi`, `/usr/lib/wsl/lib/libcuda.so.1` present, Python 3.10.12, 947 GB
free. JAX docs list WSL2 GPU as **experimental**.

| | A: `jax[cuda12]==0.6.2` | B: `jax[cuda13]`, latest 0.12.0.1 |
| --- | --- | --- |
| Python | >=3.10, **runs on the existing 3.10** (0.6.x is the last line; 0.7 needs 3.11) | >=3.12; not installed here |
| NumPy | >=1.26, matches `.venv` | >=2.2 |
| Built with | CUDA 12.8 (changelog, from 0.6.0) | CUDA 13.0 |
| Driver floor | 525 | 580 |
| Flax | `flax==0.10.7` (jax>=0.6.0, py>=3.10) | 0.12.12 (jax>=0.11.1, py>=3.12) |
| Download | **About 3.4 GB** | Not totaled |

Option A sizes in MB, wheels for cp310 and x86_64: cudnn ~800 (nearest listed 9.24.1.1; the newest was not read),
cublas 581-594, cusparse 366, nccl 352, cusolver 338, nvshmem 281, cufft 201, jax-cuda12-pjrt 125, jaxlib 90, nvrtc
90, nvcc 41, nvjitlink 40, plugin 16, cupti 11, runtime 3.5, jax 2.7. Installed size was not checked and will be larger.

**The installed 0.4.28 almost certainly cannot use this card.** Blackwell needs CUDA 12.8+ according to PyTorch
reports, and I found no JAX-specific statement about sm_120. That is reasoning by analogy, and it also means
Option A working on an RTX 5090 is **not confirmed**. First test:

```
python -m venv .venv-gpu && .venv-gpu/bin/pip install "jax[cuda12]==0.6.2" flax==0.10.7 optax
XLA_PYTHON_CLIENT_PREALLOCATE=false .venv-gpu/bin/python -c \
  "import jax, jax.numpy as j; print(jax.devices()); print((j.ones((4096,4096))@j.ones((4096,4096))).sum())"
```

If that fails on sm_120, go to Option B. A separate environment is the right shape regardless: `ygopro_ygoenv` is a
cp310 module and actors should keep it. Do not touch `.venv`, since other workstreams depend on it.

**Code changes.**

- None in the network or update. `train.py --learner-device gpu --worker-python /home/mada/ygo-agent/.venv/bin/python`
  runs the learner in the GPU environment and actors in the existing one, exchanging plain msgpack and npz. The path
  through `jax.device_put` has only run on CPU, and a learner in a new environment also needs numpy and the local modules.
- Set `XLA_PYTHON_CLIENT_PREALLOCATE=false`. The desktop already holds 13.5 of 32 GB.
- Reproducibility must be retested with `test_learner.py` on GPU, since `resume-matches-uninterrupted` is CPU evidence.
  OpenXLA documents `--xla_gpu_exclude_nondeterministic_ops`, which covers scatter, GEMM and convolution, and it may cost
  speed. Resume also refuses a different JAX version by design.
- `XLA_FLAGS` for actors stays as it is, since it applies only inside the old environment.
- `scripts/cleanba.py` cannot move to JAX 0.10 or later without replacing `jax.device_put_sharded`.

## What I would predeclare for a fresh final test

**No checkpoint passes R3, so my recommendation is not to spend a final block on M1's exit yet.** Train
further first. If you want to confirm the narrower claim that the net has passed the baseline, this is what I would write:

- **Models:** league checkpoint-0320, optimizers 1702/1703/1704, frozen hashes from
  `build/edison-learner-2026-11-04/pilot/league-*/checkpoint-0320.json`, greedy. No repetition guard, because no fixture game looped.
- **Seeds:** a fresh block. The 74000000-74000063 I proposed here was already used by the hardening soak.
- **Matrix:** 3 fixtures, 6 ordered pairings, both seats; 768 games per model per opponent against heuristic,
  random and the same-index baseline, with the baseline replayed greedily on the same starts. Replay 1 cell in 24 at a different thread count.
- **Pass:** mean head-to-head at least 52% with every optimizer above 50%; mean paired gain over the baseline against the heuristic above 0.
- **Fail:** any incomplete duel beyond R4's 1%, or either condition missed. Given optimizer 1703's -1.82 pp, the gain could fail.
- **Not claimed:** beating the heuristic, or M1's exit.

The pilot block 73900000-73900063 and 73910000-73910007 are consumed. Validation 40000000-40000007 was read for this study too.

## Deviations and discarded runs

- **Development runs.** The protocol says ten; the true count is 11 (d0-d10), plus one first d0 attempt that crashed
  on a padding bug. d10 was still running when the protocol was hashed; it decided nothing. Sampled results at 3,072 duels:

  | Run | Setup | Heuristic, last block | Note |
  | --- | --- | ---: | --- |
  | d2 | 64 ch, lr 3e-4, league | 16.8% | |
  | d3 | 64 ch, lr 1e-3, league | 14.5% | Faster mid-run; lr chosen |
  | d4, d8 | 64 ch, lr 1e-3, schedule, 2 seeds | 19.8%, 20.3% | Random 77.1%, 81.0% |
  | d5, d6 | lambda 1.0, league and schedule | 7.8%, 14.1% | Clearly slower; lambda .95 kept |
  | d7, d9 | 96 ch, schedule, 2 seeds | 16.7%, 15.9% | Random 79.4%, 69.5%; no gain seen, so 64 kept |
  | d10 | 96 ch, league | 19.5% | |

  d0, d1 and a later speed run on seeds 73540000+ are not results.
- **Extending the league to 320 rounds** was decided on wall clock before round-160 results were read, but after the 32-round
  results had been read. The protocol says "without reading any evaluation result". It rests on my word that those results played no part.
- **Replay coverage is below the protocol's "at least 1 in 24".** Main fixture phases met it. Gauntlet net phase was 1 in 64. The
  sampled, `league8`, `league80`, `control-random`, fixtures-only and baseline gauntlet phases had none.
- **Budget:** 73,176 games, under the stated 90,000.
- **Changed after hashing:** `pilot_eval.py` (skip finished phases; gauntlet checkpoint fixed at 160), `pilot_report.py` (add 160 and 320 tables),
  `bench.py`, `test_learner.py` (one new check). Added: `bench_cells.py`, `loop_diag.py`, `training_summary.py`,
  `value_check.py`. `actor.py`, `net.py`, `obs.py`, `train.py`, `decks.py` and the code list are unchanged, and resume verified this.
- **Summary file labels:** `pilot-summary.json` also applies R1's formula to league 160 and 320, giving `NOT_DISTINGUISHED`. Those budgets
  are unequal, so that label has no standing.

## Not verified

- **Anything on GPU.** Versions, sizes and compatibility are from documentation. No GPU run of any kind.
- **Upstream learning on Edison.** Only its forward cost and finiteness were measured.
- **Independence of samples.** Three optimizers share environment seeds; bootstraps resample starts and ignore optimizer variance.
- **Strength against anything adaptive or human.** Heuristic, random and a frozen baseline only. Fixed decks are not tournament strength.
- **Rules correctness.** Simulator commit as stated; full certification was not rerun because no runtime source changed.
- **Whether the plateau is real, and its cause.** Untested hypotheses above.
- **The cause of 8 of 9 void cells,** inferred from one replay. Side Decks were not played.
- **Search.** Every PIMC figure is derived from separately measured parts. Value accuracy on determinized worlds is untested.
- **Full-pool scale.** 509 ids tested, not 4,021. Actor, trimming and hash checks should carry over; the Gottoms decision remains the owner's.
- **Equal CPU conditions.** The archived NumPy time is from another day with a different pipeline. Shared-machine rows are ±30%.

## Seed use

| Block | Use |
| --- | --- |
| 73000000-73030719 | League and schedule training (schedule uses the first 3,072) |
| 73100000-73115359 | Gauntlet training |
| 73500000-73543071 | Dev runs and speed run |
| 73580000-73593360 | Tests and code-list soak |
| 73800000-73800015 | Value calibration, post-hoc |
| 73890000-73892535 | Speed benchmarks |
| 73900000-73900063 | Pilot evaluation, consumed |
| 73910000-73910007 | Gauntlet evaluation, consumed |
| 40000000-40000007 | Validation, read for this study |

No final-test block and no 22M, 50M, 60M, 70M, 71M or 72M seed was used. `evaluate.py` refuses them.

## Reproduce

Set up ignored files as in [the forward-model report](edison-forward-model.md), then:

```bash
P=/home/mada/ygo-agent/.venv/bin/python; L=tools/edison/learner
$P $L/decks.py                                             # regenerates the code list; the hash must match
$P $L/test_learner.py --output <fresh>/tests.json
$P $L/soak_codes.py --output <fresh>/soak.json
# Training commands are in the protocol. Each run: distinct --learner-cpu-offset, fresh --output.
$P $L/pilot_eval.py control ; $P $L/pilot_eval.py numpy ; $P $L/pilot_eval.py equal
$P $L/pilot_eval.py league-curve --large-round 320
$P $L/pilot_eval.py gauntlet --large-round 320 ; $P $L/pilot_eval.py sampled --large-round 320
$P $L/pilot_report.py --large-round 320 --output <fresh>/pilot-summary.json
```

`pilot_eval.py` refuses to run if the protocol hash changed, and every output directory must be fresh.

## Code list v2 and identity dropout

Added for [M3 Stage B](edison-m3b-protocol.md), November 12, 2026. Everything above describes code list v1, which is
still the default and trains and plays as before (`test_learner.py` gives the same rollout and parameter hashes).

- **Selecting it.** `EDISON_CODE_LIST=v2` in the environment of the learner and every actor. The list is
  `codes_v2.txt`, 4,042 ids whose first 509 lines are v1's. Checkpoints are format `edison_net_v2` and name the
  identity map's hash; `actor.py` refuses a checkpoint of the other format.
- **The identity map**, `identity_v2.json`. Ids that a training list plays read their own row, artworks of a card
  share one, and every other id reads one unknown row. `NetPolicy.infer` applies it, so no caller can skip it.
  `python decks.py identity` rebuilds it.
- **Wider action tables** for prompts only the wider pool raises (a declared card name, declared Life Points).
- **Identity dropout**, `--id-dropout-clean` and `--id-dropout-max` on `train.py`. Per game, from the game's seed:
  with probability `clean` nothing is hidden, otherwise each known row is hidden with a rate drawn uniformly up to
  `max`, for the whole game and both seats. Stage B trained with 0.25 and 1.0.
- **Loading a v1 checkpoint into v2**: `load_v1_into_v2` in `actor.py`. The unknown row is the mean of the known
  rows and new action rows are zero.
- **The native forward pass** (`tools/edison/search/edison_net.h`) reads the map from the exported file and requires
  the caller to name its hash. `edison_search` does not load v2 files yet.
- **Checks.** `tools/edison/heldout/identity_check.py all` compares what a network reads with the map built a second
  way, on the v1 build as a control and on six planted defects. Its summary is in
  `tests/edison/reports/m3-stage-b-2026-11-12/identity/summary.json`.

Stage B's result: the v2 networks trained this way score 75.6% on held-out builds with unknown cards, against 75.4%
for the M1 networks loaded with no retraining, and 1.6 points lower on the training lists. See
[the report](edison-heldout.md#stage-b-lists-with-cards-no-training-list-plays).

## Files

Code in `tools/edison/learner/`: `net.py`, `obs.py`, `actor.py`, `train.py`, `evaluate.py`, `decks.py`, `codes_v1.txt`,
`codes_v1.json`, `pilot_eval.py`, `pilot_report.py`, `test_learner.py`, `bench.py`, `bench_cells.py`, `soak_codes.py`,
`value_check.py`, `loop_diag.py`, `training_summary.py`, `curves.py`, `wait_for.py`.

Evidence in `tests/edison/reports/learner-2026-11-04/`: `protocol.md`, `protocol.sha256`, `protocol-sources.json`,
`pilot-summary.json`, `eval-summaries.json`, `training-summary.json`, `speed.json`, `tests.json`, `codes-soak.json`.

Large output in `build/edison-learner-2026-11-04/` (517 MB): checkpoints, per-cell evaluation with traces, logs.

Sources: [JAX install](https://docs.jax.dev/en/latest/installation.html), [JAX changelog](https://docs.jax.dev/en/latest/changelog.html),
[OpenXLA determinism](https://openxla.org/xla/determinism), PyPI metadata for each wheel named above, and PyTorch forum reports on
[RTX 5090 and sm_120](https://discuss.pytorch.org/t/support-for-nvidia-rtx-5090-cuda-sm-120/223683).
