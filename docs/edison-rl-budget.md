# Edison PPO training-budget study

This study compares **768 versus 3,072 training duels**, using the same three
optimizer seeds (1702, 1703, 1704), unchanged policy architecture, features,
rewards, PPO hyperparameters, and random/heuristic opponents. The reference
is the replication checkpoint f19798b. This is a predeclared budget comparison;
the final policy is always round 32.

## Results

**All three runs and comparisons passed.** There were **23,040/23,040**
completed duel executions: 9,216 training games, 5,760 validation executions,
and 8,064 final-test executions, including evaluation replays. All 96
round-level optimization replays matched. The original eight-round training
prefixes reproduced their saved models and optimizer states exactly.

Final-test results use 192 games per opponent, checkpoint and optimizer seed:

| Seed | 768 vs random | 3,072 vs random | 768 vs heuristic | 3,072 vs heuristic |
| --- | ---: | ---: | ---: | ---: |
| 1702 | 128/192 (66.67%) | 169/192 (88.02%) | 21/192 (10.94%) | 56/192 (29.17%) |
| 1703 | 128/192 (66.67%) | 171/192 (89.06%) | 21/192 (10.94%) | 54/192 (28.12%) |
| 1704 | 143/192 (74.48%) | 166/192 (86.46%) | 39/192 (20.31%) | 61/192 (31.77%) |

Mean and sample SD below describe the three optimizer runs; pp means percentage points.

| Opponent | 768-duel mean ± SD | 3,072-duel mean ± SD | Paired gain ± SD | Improved runs |
| --- | ---: | ---: | ---: | ---: |
| Random | 69.27% ± 4.51 pp | 87.85% ± 1.31 pp | +18.58 ± 5.74 pp | 3/3 |
| Heuristic | 14.06% ± 5.41 pp | 29.69% ± 1.88 pp | +15.62 ± 3.65 pp | 3/3 |

Every optimizer seed improved against both opponents on the fresh test set.
The fourfold training budget helped on this fixed benchmark, while the heuristic
still won most head-to-head games against the learned policies.

Final-policy ranges and speed:

| Opponent | Win-rate range | Serial decisions/s | Parallel decisions/s |
| --- | ---: | ---: | ---: |
| Random | 86.46%–89.06% | 2,027 | 3,654 |
| Heuristic | 28.12%–31.77% | 2,099 | 3,698 |

Mean training time was **223.6s** per run (range 221.8–225.3s), including optimizer replay.
The unchanged uniform starting policy and frozen heuristic reproduced identical
test trajectories across optimizer runs. Their final-test control win rates were:

- round-0-vs-random: 103/192 (53.65%).
- round-0-vs-heuristic: 3/192 (1.56%).
- heuristic-vs-random: 185/192 (96.35%).

Validation learning curves (mean win rate across optimizers; each point has 96
games per opponent and optimizer, with serial/parallel replay):

| Training duels | Vs random | Vs heuristic |
| ---: | ---: | ---: |
| 0 | 44.79% | 7.29% |
| 768 | 67.01% | 9.38% |
| 1536 | 79.86% | 19.44% |
| 2304 | 82.99% | 24.31% |
| 3072 | 85.76% | 30.56% |

[Machine-readable summary](../tests/edison/reports/rl-budget-2026-11-02/summary.json)
includes variability, ranges, per-run results and the full protocol. Models,
all intermediate policy checkpoints, final optimizer states, complete compressed
evaluation/training reports, the pretest manifest, certification traces and
source/artifact hashes are archived alongside it. Complete rollouts and worker
logs remain in the build directory.

## Protocol declared before final evaluation

Each round contains 96 training duels: all six ordered cross-deck pairings of
Blackwing, Frog Monarch, and Machina Gadget, both learner seats, eight seeds per
cell. Opponents alternate between random and heuristic. Each longer run contains
1,536 games per opponent and 1,536 per learner seat.

The first eight rounds retain the original training schedule. Their model and
Adam-state hashes must match all eight saved reference checkpoints exactly.
Rounds 9–32 use a new seed block starting at 30000000, with 100000 between rounds
and 1000 between cells. Simply extending the original arithmetic sequence would
reuse old test seeds, so the extended schedule explicitly avoids that block.

Validation uses duel seeds 40000000–40000007, all deck pairings and both seats:
96 games per opponent at checkpoints 0, 8, 16, 24, and 32. Curves are diagnostic;
they do not change hyperparameters, budget, or checkpoint selection.

Final testing uses fresh duel seeds 50000000–50000015: 192 games per comparison.
Initial, round-8, and round-32 policies each face random and heuristic opponents.
The frozen heuristic also faces random. The round-8 and round-32 results on this
same matrix are the primary budget comparison; old reported win rates use a
different matrix and must not be subtracted from these new results.

All three training runs finish before validation. After validation, a saved
pretest manifest records checkpoint hashes and the protocol before any final
test is executed. Frozen hashes are checked throughout final testing. Training,
validation, final test, and the old held-out block are disjoint. Action sampling
remains stochastic and derives from the same duel seeds across compared policies.

Every validation and test comparison repeats in fresh serial and eight-thread
workers, requiring identical per-game traces and results. All 96 round-level
optimization passes replay from copied model/Adam state and identical shuffle
seeds. The full training trajectories are collected once per optimizer seed;
the study does not claim two independent end-to-end training collections.

## Interpretation and timing

The three optimizer runs share environment seeds. Means, sample standard
deviations (n-1 denominator), and ranges describe variability between optimizers
conditional on these decks and seeds. They do not constitute independent
tournament samples or establish general play strength. The reported gain is the
per-run round-32 minus round-8 win rate on identical fresh test games.

Evaluation throughput counts both players' decisions and includes resets,
feature encoding, inference, audits, hashing, and logging. Worker startup is
excluded. Serial and parallel rates are reported separately. End-to-end training
time includes process startup, serialization, and repeated optimizer updates.
Runs execute sequentially to limit timing interference; host load can still vary.

## Reproduce

From the repository root with the pinned production binary, assets, and dependencies:

    export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
    source .venv/bin/activate
    python tests/edison/run.py --output build/edison-rl-budget/certification
    python tests/edison/budget_context_rl.py

The study refuses to overwrite an existing study report. For a repeat, use a
fresh output directory for both certification and the runner's --output argument.
The trainer's original eight-round defaults remain unchanged.

Test 083F checks the extended balanced schedule, separation of evaluation seeds,
original schedule prefix, command-line defaults, and rejection of modified frozen
checkpoints. The full suite passes 542 cases.

## Evidence

The completed archive is under tests/edison/reports/rl-budget-2026-11-02. Reports include
per-cell and per-duel outcomes, source/binary/asset fingerprints, model and optimizer
hashes, the pretest manifest, and replay checks. Runtime logs and complete rollouts
remain in build/edison-rl-budget.

The subsequent [context ablation](edison-rl-ablation.md) compares these immutable
full-context checkpoints with matched masked-context training on fresh test seeds.
