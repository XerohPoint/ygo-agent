# M2 final test: protocol

M2's exit: search at a fixed decision budget beats the same network without search on fresh seeds, and a command ranks actions with estimated win rates from a recorded position.

`tools/edison/search/m2_final.py` is the exact form of this plan. Where the two disagree, the script is what ran, and the disagreement is a defect to report.

## Frozen before any game

`m2_final.py freeze` writes `m2-manifest.json`, which is committed alone directly on the commit it names. It records that commit and the sha256 of the binary, the Lua scripts, the card database, the lists, the code list, the net files, all job lines and the search scripts. `play` and `analyze` refuse to run unless HEAD is that manifest commit, nothing is uncommitted, and every hash still matches. Before freezing, `test_search.py` passes on all stages, `run_all.sh` passes, and `m2_final.py selftest` passes on that build. Those three are steps the operator runs and reports. `freeze` doesn't enforce them.

`analyze` checks results before scoring anything. Each must sit under its own file name, hold its exact job line and name the frozen build, and each directory's recorded inputs and attempt log must match the manifest.

A code change after that voids the run. A voided run is still reported, and a new one uses a new seed block.

## Setting

Budget 64, depth 48, 4 candidates, margin 1.0, halving on. Chosen on pilots with seeds 86100000-86310167, none reused.

## Design

- **Blocks:** `neighbour_pairs` over the 21 sorted lists gives 168 ordered pairs. Block `b` has seed `87000000 + b`, so seeds 87000000-87000167.
- **Search games:** each block x net (n1702, n1703, n1704) x search seat (0, 1): 1,008 games. The other seat plays the same net with no search. The search seed is `3000 +` the job's index.
- **Plain games:** each block x net, the same seeds: 504 games, each read from both seats.

## Scoring

Win 1, draw 1/2, loss 0, for the seat in question. A **comparison** is one search game and the plain game with the same seed and net, read from the same seat. D is its search score minus its plain score. If either game is void, the comparison is excluded. One void plain game excludes two.

Every block with a surviving comparison in the subset being measured counts once: its value is the mean of those comparisons, and a statistic is the mean over such blocks. Ranges are 95% percentile intervals from resampling whole blocks: 20,000 resamples, generator `numpy.default_rng(87000000)`, drawn in the script's fixed order.

The unit of inference is the block, and the claim covers these 168 ordered pairs at these seeds' mix, not any one matchup. With no exclusions a net's plain score is exactly 50%, because each plain game is read from both seats. So D and score carry the same information overall and per net. A seat's plain score is not fixed that way.

## Decision: all must hold

1. Overall D's lower bound is above 0.
2. For each net, D's lower bound is above 0.
3. For each search seat, D's lower bound is above 0.
4. At most 10 of 1,008 comparisons are excluded.
5. Counting each excluded comparison as D = -1 in its own block, and bootstrapping as above, overall D's lower bound stays above 0. A real comparison can't always reach -1, given its known plain game, so this is conservative.
6. No missing games, worker deaths, worker failures in any attempt's log, missing logs, logs or inputs that don't match the manifest, network refusals of a true observation, rollout network errors, option mismatches, step overruns, or results with the wrong job, name or build.
7. **Ranker.** There must be at least 64 finished, decisive search games. From those, draw 64 with `random.Random(87000000)`, a draw that doesn't read which seat won beyond eligibility. Take each game's last real choice by the winner (closing), which need not end the game itself, and 2 random real choices (middle). Rank them with budget 256, depth 48. An all-refused position abstains and is counted. These must hold:
   - Every shown number is finite and in range, and follows from the rollout results reported with it.
   - **Alone.** Every compared option, rolled out alone on the deals it had, gives the same rollout results to the last bit. This ties each option's numbers to that option, whichever it is.
   - At least 20 closing positions were estimated. In at least 70%, the move played is estimated at 80% or higher, and in at least 70% it is within 5 points of the best option.
   - At least 10 middle positions on each side. The move played is estimated at least 25 points higher when that seat went on to win, on the unrounded numbers.
   - **Branches.** In positions where the shown best and worst options are at least 20 points apart, play each in the true duel, through any forced steps, and rank from there with the same settings. A branch that ends at the decision limit gives no result. At least 15 such positions must be usable, and in at least 75% the shown best must really come out above the shown worst. This is what ties an option to the action the true duel plays.
   - No ranking, including those for branches, lost a world to a failure, and all of them repeat exactly.

These thresholds were set from `test_search.py`'s `estimates` stage on seeds 86007200-86007263, not from this test's games, and two smaller check cohorts of Sol's. They are floors for "the numbers mean what they say", not measures of how accurate the estimates are. A branch compares estimates with estimates, not with a known truth, and it has limited power: a ranker with numbers on the wrong options can pass it, which is why Alone exists. Failing a coverage floor would fail this test for a reason unrelated to strength, and the report would say so.

There are no interim looks at scores. An infrastructure failure may resume missing jobs on the same inputs. Void files stay and every attempt is logged, including one cut off or exiting with an error. Those are reported, and don't fail the test by themselves. A worker failure inside any attempt does.

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
