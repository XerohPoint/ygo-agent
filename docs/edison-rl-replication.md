# Edison PPO optimizer-seed replication

The original 768-duel PPO pilot is checkpointed in **7f02418**, preserving its
code, models, optimizer state, reports and reproduction instructions.
This study repeats that experiment with three preselected optimizer seeds:
**1702, 1703 and 1704**. The original seed 1701 is a reference, not one of the
three replication observations.

Final results, checkpoints and evidence are recorded in
[the replication summary](../tests/edison/reports/rl-replication-2026-11-02/summary.json).

## Final results

**All three replications passed.** The new runs completed **8,064/8,064
training/evaluation executions**: 2,304 training games and 5,760 evaluation
executions including replays. All evaluation replays and round-level
optimization replays matched. The frozen controls reproduced the original
pilot trajectories exactly.

| Optimizer seed | Final vs random | Final vs heuristic |
| --- | ---: | ---: |
| 1701 (original pilot) | 129/192 (67.19%) | 33/192 (17.19%) |
| 1702 | 131/192 (68.23%) | 28/192 (14.58%) |
| 1703 | 125/192 (65.10%) | 29/192 (15.10%) |
| 1704 | 148/192 (77.08%) | 38/192 (19.79%) |

Primary statistics use only seeds 1702–1704. SD is sample standard deviation
across the three optimizer runs, expressed in percentage points.

| Opponent | Mean win rate ± SD | Range | Gain over initial | Parallel decisions/s |
| --- | ---: | ---: | ---: | ---: |
| Random | 70.14% ± 6.21 pp | 65.10%–77.08% | +20.66 pp | 4,177 |
| Heuristic | 16.49% ± 2.87 pp | 14.58%–19.79% | +10.24 pp | 3,894 |

Every replication improved on the identical uniform-policy controls
(49.48% against random; 6.25% against the heuristic). The improvement
therefore repeated across these optimizer seeds, while the frozen
heuristic remains substantially stronger.

Mean end-to-end training time was 62.5s
(range 61.1–64.7s), including replayed
optimizer updates. Evaluation rates include the audit and logging overhead
defined below. No longer training run or context ablation was conducted.

## What varies and what stays fixed

Only hidden-layer initialization and minibatch-shuffle randomness vary. The
optimizer seed controls both. Adam's hyperparameters, model architecture,
features, PPO equations, rewards, opponents and checkpoint-selection rule remain
identical to the pilot. The default seed remains 1701 for backwards compatibility.

Each run trains eight rounds of 96 games: 768 games, split evenly between the
random and frozen heuristic opponents and between player seats. All six ordered
cross-deck pairings of Blackwing, Frog Monarch and Machina Gadget are used.
Training duel seeds and the policy's per-environment action-sampling seeds stay
fixed across replications. Subsequent trajectories can diverge as the learned
policies diverge; this is the intended consequence of optimizer randomness.

Each run uses the same held-out duel seeds, 22000000–22000015. Initial and final
policies each play random and heuristic opponents for 192 games per comparison.
The frozen heuristic also plays random on that same matrix. Every comparison
is repeated in fresh one-thread and eight-thread workers, giving 1,920
evaluation executions per run. Final checkpoint selection remains round eight,
without selecting or tuning on held-out results.

The learner, benchmark policies, match runner, evaluator, production binary and
assets are checked against the pilot before the study starts. No new training
budget, ablation, feature change or hyperparameter search is mixed into this
replication.

## Controls and interpretation

The initial actor is uniform even though its hidden weights differ between
seeds. For each replication, the uniform actor's two evaluations and the frozen
heuristic-versus-random control must reproduce the pilot's exact per-game
decision hashes, interaction counts and results. This checks that the comparison
has not drifted underneath the optimizer-seed change.

The primary summary uses only the three new runs. It reports arithmetic mean,
sample standard deviation (n-1 denominator), minimum and maximum of each run's
held-out win rate, plus its gain over the initial policy. A separate secondary
summary includes the exploratory pilot. Failed or missing runs cannot be
silently omitted from aggregation.

These are three optimizer replications with shared duel/sampling seeds, not
three independently sampled environment or test sets. A run still has 192
held-out games per opponent; repeating those seeds does not create 576
independent test scenarios. Standard deviations describe between-optimizer
variation conditional on this fixed benchmark. Three runs are insufficient for
a precise population estimate or a claim of general tournament strength.

Runs execute sequentially to avoid competing training jobs distorting throughput.
Timing can still vary with host load. Rates retain the pilot's definition:
both players' decisions, including reset, public feature encoding, inference,
observation audits, hashing and flushed logging, excluding worker/module/pool
startup. End-to-end training time also includes startup, serialization, loading,
and repeated optimizer updates. Serial and parallel timings are kept separate.

## Validation and artifacts

The full certification suite passes **541 cases**, with identical replay output.
Test 083E verifies seed-option propagation, identical initial uniform behavior
despite different hidden weights, correct sample variability, and rejection of
incomplete/failed replication sets. Existing tests cover PPO gradients, privacy,
reward attribution, legal-action sampling, replay and disjoint train/test seeds.

All 24 round-level optimization passes are replayed from copied parameters and optimizer state
and must match exactly. Every final evaluation is independently repeated in
fresh serial/parallel workers. The training environment trajectories are collected
once per optimizer seed; this is not a second collection of the entire study.

Archived evidence includes certification, study protocol, full per-run reports,
all evaluation reports, initial/final policies, final optimizer states, representative
rollouts and source/asset/model hashes. Complete rollout files, intermediate
checkpoints and logs remain under build/edison-rl-replication.

## Reproduce

From the repository root with pinned dependencies and assets:

    export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
    source .venv/bin/activate
    python tests/edison/run.py --output build/edison-rl-replication/certification
    python tests/edison/replicate_context_rl.py

The runner validates the current source/binary/assets against certification,
executes the three predeclared seeds sequentially, validates the frozen controls
against the pilot, and aggregates only after all three complete.

A single seed can be reproduced separately:

    python tests/edison/train_context_rl.py --optimizer-seed 1702 --certification build/edison-rl-replication/certification/report.json --output build/edison-rl-replication/reproduce-1702

Focused regression testing uses run.py --test 083. The original pilot evidence
under tests/edison/reports/rl-2026-11-02 remains unchanged.
