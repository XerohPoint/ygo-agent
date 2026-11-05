# M2 final test: protocol

M2's exit: search at a fixed decision budget beats the same network without search on fresh seeds, and a command ranks actions with estimated win rates from a recorded position.

`tools/edison/search/m2_final.py` is the exact form of this plan. Where the two disagree, the script is what ran, and the disagreement is a defect to report.

## Frozen before any game

`m2_final.py freeze` writes `m2-manifest.json`, which is committed alone on top of the commit it names. It records that commit and the sha256 of the binary, the Lua scripts, the card database, the lists, the code list, the nets, all job lines and the search scripts. `play` and `analyze` refuse to run if any of these differ. Before freezing, `test_search.py` passes on all stages, `run_all.sh` passes, and `m2_final.py selftest` passes on that build.

A code change after that voids the run. A voided run is still reported, and a new one uses a new seed block.

## Setting

Budget 64, depth 48, 4 candidates, margin 1.0, halving on. Chosen on pilots with seeds 86100000-86310167, none reused.

## Design

- **Blocks:** `neighbour_pairs` over the 21 sorted lists gives 168 ordered pairs. Block `b` has seed `87000000 + b`, so seeds 87000000-87000167.
- **Search games:** each block x net (n1702, n1703, n1704) x search seat (0, 1): 1,008 games. The other seat plays the same net with no search. The search seed is `3000 +` the job's index.
- **Plain games:** each block x net, the same seeds: 504 games, each read from both seats.

## Scoring

Win 1, draw 1/2, loss 0, for the seat in question. A **comparison** is one search game and the plain game with the same seed and net, read from the same seat. D is its search score minus its plain score. If either game is void, the comparison is excluded. One void plain game excludes two.

Every block counts once: a block's value is the mean of its surviving comparisons, in whatever subset is being measured, and a statistic is the mean over blocks. Ranges are 95% percentile intervals from resampling whole blocks: 20,000 resamples, generator `numpy.default_rng(87000000)`, drawn in the script's fixed order.

The unit of inference is the block, and the claim covers these 168 ordered pairs at these seeds' mix, not any one matchup. With no exclusions a net's plain score is exactly 50%, because each plain game is read from both seats. So D and score carry the same information overall and per net. A seat's plain score is not fixed that way.

## Decision: all must hold

1. Overall D's lower bound is above 0.
2. For each net, D's lower bound is above 0.
3. For each search seat, D's lower bound is above 0.
4. At most 10 of 1,008 comparisons are excluded.
5. Counting each excluded comparison as the worst result, overall D's lower bound stays above 0: `(1 - f) x low - f > 0`, for excluded fraction `f`.
6. No missing games, worker deaths, failures in any attempt's log, network refusals of a true observation, rollout network errors, option mismatches, step overruns or job mismatches.
7. **Ranker.** From 32 finished, decisive search games drawn with `random.Random(87000000)` before reading scores, take each game's last real choice by the winner (closing), and 2 random real choices (middle). Rank them with budget 256, depth 48. An all-refused position abstains and is counted. These must hold:
   - Every shown mean and error follows from the rollout results it reports.
   - At least 20 closing positions were estimated. In at least 70%, the move played is estimated at 80% or higher, and in at least 70% it is within 5 points of the best option.
   - At least 10 middle positions on each side. The move played is estimated at least 25 points higher when that seat went on to win.
   - The whole ranking repeats exactly.

These thresholds were set from the check runs in `test_search.py`'s `estimates` stage on seeds 86007200-86007231, not from this test's games. They are a floor for "the numbers mean what they say", not a measure of how accurate the estimates are.

There are no interim looks at scores. An infrastructure failure may resume missing jobs on the same inputs. Void files stay and every attempt is logged, so a resume can't hide a failure.

These are separate 95% ranges, not a simultaneous region. All of them must pass, so no multiplicity correction is applied.

## Reported, not gated

- Each seat's and net's absolute score. A seat can lose on average even when search improves it.
- A different opposing net: `search_study.py cross --seed 87100000 --tag m2-cross --configs b64d48k4z1`, 336 games with the pilot's rotation, plus its plain games.
- Per-list scores, calibration, departure and fall-back rates, deals refused, voided worlds and their first messages, and CPU per game. One seed per matchup doesn't support a claim about any one matchup, so these are for reading.

## What a pass does not claim

- Strength against people or other kinds of opponent. Every opponent here is one of three networks from one training method, and the gated runs use the search's own net.
- Play without knowing the opposing list. The search is told it, which is a declared assumption.
- Accuracy of the estimates as win rates in real play. Rollouts assume both seats play on as the network does, and they have run low in the pilots.
- Anything about hidden-information play such as bluffing. Each deal is searched as if the viewer knew it.
