# Edison context-aware reinforcement-learning pilot

This is a bounded CPU PPO experiment built on checkpoint 9fa3b58. It uses the
audited context observations, keeps the random and original heuristic policies
fixed, and learns exclusively from completed-duel outcomes. Existing imitation
weights and prior baseline reports are unchanged.

Final results, hashes and checkpoints are linked from
[the experiment summary](../tests/edison/reports/rl-2026-11-02/summary.json).

## Final results

The experiment passed: **540 certification cases**, **768 training games**,
**63,870 learner decisions**, and **1,920 held-out evaluation executions**.
All 2,688 executions completed naturally, with no crashes, stuck prompts,
overflow or truncation. All held-out serial/parallel replays matched.

| Comparison | Wins | Win rate | Parallel decisions/s |
| --- | ---: | ---: | ---: |
| Initial RL vs random | 95/192 | 49.5% | 4,188 |
| Initial RL vs heuristic | 12/192 | 6.2% | 3,812 |
| Final RL vs random | 129/192 | 67.2% | 4,080 |
| Final RL vs heuristic | 33/192 | 17.2% | 3,876 |
| Frozen heuristic vs random | 184/192 | 95.8% | 4,956 |

Training took 60.9 seconds end to end, including repeated
optimizer updates. Instrumented rollout speed averaged 3,417
decisions/s. Per-configuration timings and full game results are in the summary
and compressed reports. Model and optimizer checkpoints are saved alongside them.

The final policy improved on the initial uniform policy on both held-out
opponents, but still trails the frozen heuristic substantially. This is one
small pilot, not evidence to replace the heuristic benchmark.

## Design fixed before evaluation

The run has eight rounds of 96 games, for 768 training games total. Every round
covers the six ordered cross-deck pairings of Blackwing, Frog Monarch and Machina
Gadget, both learner seats, and eight unique seeds per cell. Opponents alternate
between uniform random and the frozen heuristic by round: 384 games against each,
with 384 games in each learner seat. There are no training mirrors.

Training seeds are generated as 21000000 + round_index * 100000 + cell_index *
1000 + environment_id. A setup guard initially caught overlapping round ranges;
the spacing was corrected before any training duels ran, and Test 083D now
requires 768 unique seeds and balanced seats/opponents.

Evaluation uses seeds 22000000–22000015, excluded from training. The complete
training schedule finishes before evaluation starts. The final checkpoint is
always round eight; no held-out result selects a checkpoint or changes a
hyperparameter.

Initial and final RL policies each play random and heuristic opponents across
six ordered deck pairings, both seats and 16 seeds: 192 games per comparison.
The frozen heuristic additionally plays random on the identical matrix.
Each comparison is replayed in fresh processes with one and eight environment
threads, checking complete decision hashes and terminal results. Evaluation
uses stochastic sampling from the learned policy, with fixed per-environment,
per-seat RNG seeds; it does not switch to greedy play.

## Model and features

The actor is a 128-input, 32-hidden-unit tanh network with one logit per offered
legal action. A separate linear tanh critic estimates the terminal outcome from
the mean offered-action feature vector. Only actual legal rows enter softmax;
padding receives no probability. Overflow, invalid observations, native
diagnostics, timeouts and truncation fail the run.

The initial actor output weights are zero, so the initial policy is uniform
over legal actions. The hidden layer is randomly initialized with seed 1701.
The critic starts at zero. There is no imitation warm start or teacher-label loss.

Feature schema edison_context_ppo_v1 combines the existing 64 public engineered
features with 64 context features: selection hint, callback stage, visible source
stats, selection bounds and counts, selected/mandatory flags and aggregate
visible stats, finish/cancel availability, attack margin, LP, turn/phase, and
context-dependent value interactions. Existing public features include the
heuristic's conservative activation eligibility indicator, but its action scores
and labels are discarded. This is not an end-to-end raw-card encoder.

Card metadata is used only for visible identities. Hidden card statistics stay
zero. Source references fail closed if they point at a private identity. The
encoder uses the current decision context and legal actions, plus the existing
public action-count memory; no core queries or opponent-private observations
are supplied to the learner.

## PPO updates

Rewards are natural outcomes from the learner's absolute seat: +1 for a win,
-1 for a loss, zero for a draw. The last acting player's reward sign is checked
against the environment but never mistaken for the learner's return. Opponent
decisions are excluded from the learner dataset.

Only complete episodes are collected. Gamma is 1. Advantages are Monte Carlo
terminal returns minus the frozen rollout critic. They are normalized with
inverse learner-decision-count weights so long games do not dominate solely
because they contain more decisions. These weights also enter the minibatch loss.

Each round freezes its policy while collecting all 96 games, verifies saved
action likelihoods against that checkpoint, then updates it:

- Adam learning rate 0.002; 256 complete decision segments per minibatch.
- Up to four epochs; PPO ratio clipping 0.2.
- Entropy coefficient 0.01; value coefficient 0.5; gradient-norm cap 0.5.
- Stop remaining epochs if full-batch approximate KL exceeds 0.03.

Every update is independently repeated from copied parameters and optimizer
state using the same shuffle seed. Parameters, optimizer moments, step count
and metrics must match exactly. This is update reproducibility on saved rollouts;
the entire training run was not collected independently a second time.
Fresh-process sampling/replay is tested separately in certification and all
held-out evaluations.

## Certification and evidence

The full suite passes 540 cases with identical replay output. Test 083 covers
finite-difference actor/critic/entropy gradients, reward-gradient direction,
PPO clipping, context encoding, privacy, checkpoint round trips, legal stochastic
sampling, both-seat reward attribution, parallel replay, and seed separation.

Reports preserve per-cell outcomes, action/observation hashes, prompt coverage,
timings and rollout hashes. Initial/final models, final Adam state and a
representative rollout are archived alongside the report. All rollout files and
intermediate checkpoints remain under build/edison-rl. Source, assets and binary
hashes identify the exact run.

Throughput includes reset, feature construction, Python inference, observation
audits, hashing and flushed logging, and excludes worker/module/pool startup.
Rollout rates also exclude the final compressed-dataset write; end-to-end
training time includes startup, serialization, loading and the repeated updates.
Evaluation speed includes both players' decisions, not only learner inference.
Serial and parallel rates are reported separately. It is not raw engine or GPU
training throughput.

This is one optimizer initialization and a small fixed-deck held-out sample.
Paired seeds and reused deck pairings are correlated; descriptive Wilson
intervals are not evidence of broad tournament strength. No context-ablation
study or multiple independent training seeds was performed.

## Reproduce

From the repository root, with pinned assets/dependencies:

    export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
    source .venv/bin/activate
    python tests/edison/run.py --output build/edison-rl/certification
    python tests/edison/train_context_rl.py

The trainer checks current source, binary and asset hashes against certification
before collecting data. It runs the fixed training budget and all five held-out
comparisons automatically. Focused RL regressions use run.py --test 083.

To evaluate the saved final policy separately:

    python tests/edison/evaluate_agents.py --challenger rl --opponent random --count 16 --seed 22000000 --model build/edison-rl/checkpoint-8.json --output build/edison-rl/custom-evaluation

Checkpoint format and feature schema are versioned. The older 64-feature
imitation checkpoint remains a separate policy and is not accepted as an RL model.
