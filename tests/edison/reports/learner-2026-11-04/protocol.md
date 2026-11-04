# Edison learner pilot protocol

Written and hashed before any trained Edison network played a game on an evaluation seed.
Base commit 99cfd45 on branch edison, plus the uncommitted sources listed in
`protocol-sources.json`, whose sha256 is given below.

**Question.** At the same training budget as the NumPy baseline, does a card-embedding
network with league self-play play better than that baseline, greedily, on the 3 fixtures?
Does more training help?

## What has already been seen

Ten short development runs (d0-d10, seeds 73500000-73539999, 1-32 rounds) chose the width,
learning rate and GAE lambda from **sampled training win rates only**. No trained network
has been evaluated on validation or pilot seeds. All dev runs are kept and reported. Their
diagnostics put sampled play at 3,072 duels at about 16-24% against the heuristic and
70-81% against random, which is below the baseline's archived sampled 30-33% and 87%.
That is not the question here, which is greedy play, but it is already known.

## Frozen settings

Network: 64 channels, 2 attention layers, 4 heads, 32-dim identity embeddings, 128 critic hidden
units, 207,800 parameters, on the 509-id code list. PPO: Adam 1e-3, 4 epochs at most, stop after an
epoch with mean KL above .03, minibatch 256, clip .2, entropy .01, value coefficient .5, gradient
norm .5, gamma 1, GAE lambda .95, inverse decision-count weighting, row grouping on. These are
settings from dev, not tuned on evaluation.

Rewards are only the terminal outcome. No imitation warm start, no centralized critic and no
hidden information in training or at play: `oppo_info=False`, and inputs are `cards_ global_
actions_ h_actions_ decision_ selection_`.

## Training arms

Every arm uses optimizer seeds **1702, 1703, 1704**, sharing environment seeds across optimizers.
A round is 12 cells of 8 duels, 96 duels. A budget counts duels, and a self-play duel counts as one.

| Arm | Decks | Opponents | Seed base | Rounds |
| --- | --- | --- | ---: | ---: |
| `league` | 3 fixtures, 6 ordered pairs, both seats | Rotating thirds: frozen heuristic, uniformly drawn past self (snapshot every 4 rounds), current self in both seats | 73000000 | 160, extendable to 320 |
| `schedule` | Same | Baseline's schedule: random on even rounds, heuristic on odd | 73000000 | 32 |
| `gauntlet` | 21 lists, random pairs fixed per round, random seat | Same thirds as `league` | 73100000 | 160 |

`league` continues to 320 rounds only if wall clock allows. That choice is made without reading
any evaluation result, and the run is resumed with the exact-resume check.

The 2 lists containing XX-Saber Gottoms play with it removed, as in the pool audit.

**Fixed checkpoints, no selection.** `league` at rounds 8, 32, 80, 160, and 320 if reached.
`schedule` at 32. `gauntlet` at 160. 32 rounds is 3,072 duels, the baseline's budget.

## Frozen references

- Frozen heuristic (`ygoai/edison_policy.py`, unchanged) and uniform random.
- NumPy baseline: `rl-budget-2026-11-02/seed-{1702,1703,1704}/final-policy.json`, played greedily
  through the same worker. Its training used 3,072 duels on seeds 21M/30M.
- Harness check: the baseline on validation seeds should reproduce the archived greedy result of
  132/288 against the heuristic. A different total means a harness fault and stops the comparison.

## Evaluation

Greedy argmax is the result. Sampling is a diagnostic. Every cell is a fixed
(deck0, deck1, seat, seed) start played by every model.

| Block | Seeds | Fixture games per model and opponent | Use |
| --- | --- | ---: | --- |
| Validation | 40000000-40000007 | 96 | Same starts as the archived 288-game baseline review |
| Pilot | 73900000-73900063 | 768 | Main readout; fresh, no learned model has played it |
| Gauntlet descriptive | 73910000-73910007 | 1,344 (21 lists, next 4 lists in a cycle, 2 seats, 8 seeds) | Table only, no conclusions |

Budget about 90,000 evaluation games, all by `tools/edison/learner/pilot_eval.py`. Phases:
`control`, `numpy`, `equal`, `league-curve`, `gauntlet`, `sampled`. Opponents: heuristic and random for
every fixture model; each net against the same-index baseline head to head on the pilot block.
Replay: at least 1 in 24 cells are repeated with a different env thread count, and any trace mismatch
fails the phase. Pilot seeds are consumed by this study and **must not** be used as a final test.

## Rules for what will be concluded

Gain is net greedy win rate minus baseline greedy win rate, paired by start, computed per optimizer
index and then averaged. Draws count as not-a-win. All numbers are on the pilot block.

- **R1, equal budget.** For `league` at 32 rounds (primary) and `schedule` at 32 rounds (secondary):
  `BEATS_BASELINE` if the mean gain against the heuristic is at least +5 pp, every optimizer gain is
  above zero, and the mean head-to-head rate is at least 50%. `BELOW_BASELINE` if the mean gain is at
  most -5 pp. Otherwise `NOT_DISTINGUISHED`. Validation numbers are reported beside it, and if they
  disagree in sign I will say so.
- **R2, more games.** `MORE_GAMES_HELP` if `league` at its largest checkpoint minus `league` at 32
  rounds, against the heuristic, averages at least +5 pp with every optimizer above zero. Otherwise `NOT_SHOWN`.
- **R3, final-test candidate.** A checkpoint is a candidate for a predeclared fresh test only with mean
  at least 55% against the heuristic and every optimizer at least 50%. This is **not** an M1 pass. M1
  needs all fixture decks and both seats on fresh seeds, with replications. I will not run that test.
- **R4, failure.** Any void cell is reported by name and its games do not count. If more than 1% of a
  compared group is void, that comparison is `INVALID`.
- If the network does not beat the baseline at this budget, I report that as the result and say what I
  think limits it. No hyperparameter, seed or checkpoint changes after this hash in response to
  evaluation results. A later change would be a new study with new seeds.

Three optimizer seeds sharing environment seeds are not independent tournament samples. Bootstrap
intervals resample starts, ignore optimizer variance and are descriptive only. With two arms and several
references, small differences may be chance.

## Commands

```
BASE=build/edison-learner-2026-11-04/pilot; PY=/home/mada/ygo-agent/.venv/bin/python
COMMON="--workers 1 --learner-cpus 1 --channels 64 --id-dim 32 --learning-rate 0.001"
# Learner CPU offsets are distinct per concurrent run.
$PY tools/edison/learner/train.py --output $BASE/league-$S --arm league --optimizer-seed $S --rounds 320 --stop-after 160 $COMMON
$PY tools/edison/learner/train.py --output $BASE/schedule-$S --arm schedule --optimizer-seed $S --rounds 32 --opponents baseline_alternating $COMMON
$PY tools/edison/learner/train.py --output $BASE/gauntlet-$S --arm gauntlet --optimizer-seed $S --rounds 160 --deck-set gauntlet --seed-base 73100000 $COMMON
$PY tools/edison/learner/pilot_eval.py <phase>
$PY tools/edison/learner/pilot_report.py --output tests/edison/reports/learner-2026-11-04/pilot-summary.json
```

Simulator: native module sha256 c5ecba9c... as in the certification record, assets per
`tests/edison/agent_assets.lock.json`. A rules-conformance workstream may change card behavior later, so
these results belong to this binary and the pinned scripts.

Sources manifest sha256: 28ad8e3bd3ec666b8a715d309709dfa9df326e0d3d5f42b9a01464da1fa9d50b
