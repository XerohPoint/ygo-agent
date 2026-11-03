# Edison decision-context ablation

This study measures the contribution of explicit decision-context and selection
inputs at the fixed 3,072-duel budget. It pairs optimizer seeds 1702, 1703, and
1704 against the full-context models saved in commit 792a6f1. Architecture,
parameter initialization, PPO hyperparameters, rewards, training duel schedule,
opponents, action-sampling seeds, and final checkpoint selection stay fixed.

## Results

**The completed study passed.** All **23,424 study duel executions**
completed: 9,216 masked training games, 9,216 validation executions, and
4,992 final-test executions including evaluation replays. The 9,216 historical
full-context training games are reused controls and are excluded from this total.
All 96 masked optimization passes replayed exactly, all evaluation replays
matched, and all 24 full-context validation comparisons reproduced the prior
archive trajectories exactly.

Final-test wins use 192 games per opponent, mode and optimizer seed:

| Seed | Full vs random | Masked vs random | Full vs heuristic | Masked vs heuristic |
| --- | ---: | ---: | ---: | ---: |
| 1702 | 167/192 (86.98%) | 158/192 (82.29%) | 57/192 (29.69%) | 59/192 (30.73%) |
| 1703 | 166/192 (86.46%) | 171/192 (89.06%) | 62/192 (32.29%) | 51/192 (26.56%) |
| 1704 | 171/192 (89.06%) | 154/192 (80.21%) | 71/192 (36.98%) | 52/192 (27.08%) |

SD is the sample standard deviation across the three optimizer seeds; pp means percentage points.

| Opponent | Full mean ± SD | Masked mean ± SD | Full minus masked ± SD | Full better |
| --- | ---: | ---: | ---: | ---: |
| Random | 87.50% ± 1.38 pp | 83.85% ± 4.63 pp | +3.65 ± 5.80 pp | 2/3 |
| Heuristic | 32.99% ± 3.70 pp | 28.12% ± 2.27 pp | +4.86 ± 5.52 pp | 2/3 |

Full context has a positive average advantage on this fixed fresh-test matrix,
winning two of three seed pairs against each opponent. The differences are
variable: paired SD exceeds the mean gain, and one seed favors masking for each
opponent. This supports retaining full context as the baseline, while three
optimizer seeds do not establish a reliable advantage across all runs or decks.

Ranges and measured speed:

| Opponent | Mode | Win-rate range | Serial decisions/s | Parallel decisions/s |
| --- | --- | ---: | ---: | ---: |
| Random | Full | 86.46%–89.06% | 2,147 | 3,883 |
| Random | Masked | 80.21%–89.06% | 2,141 | 3,685 |
| Heuristic | Full | 29.69%–36.98% | 2,123 | 3,822 |
| Heuristic | Masked | 26.56%–30.73% | 2,019 | 3,547 |

Masked training averaged **291.0s per run** (range 237.3–376.1s), including optimizer replay.
The shared frozen heuristic control beat random **185/192 (96.35%)**.

Validation curves (mean win rate across optimizers; 96 games per mode/opponent/seed at each point):

| Training duels | Full vs random | Masked vs random | Full vs heuristic | Masked vs heuristic |
| ---: | ---: | ---: | ---: | ---: |
| 768 | 67.01% | 66.67% | 9.38% | 11.81% |
| 1536 | 79.86% | 79.17% | 19.44% | 17.01% |
| 2304 | 82.99% | 82.99% | 24.31% | 22.22% |
| 3072 | 85.76% | 86.11% | 30.56% | 25.69% |

[Machine-readable summary](../tests/edison/reports/rl-ablation-2026-11-02/summary.json)
includes all per-run metrics, variability, ranges, protocol and benchmark results.
Archived evidence includes all evaluation reports, masked training reports,
policy checkpoints, final masked optimizer states, representative rollouts,
full-context control checkpoints and their historical training provenance,
the pretest manifest, certification traces and source/artifact hashes.

## Treatment and controls

The masked mode sets 52 zero-based feature columns to zero: 64–111 and 121–124.
These encode prompt hints, stage, source, bounds and counters, mandatory/cancel
context, selection flags and selected-card summaries, and derived interactions.
It retains the other 76 inputs: the original 64 public features and 12 independent
card, life-point, phase, action and bias features added alongside context.
Masking the entire added block would confound decision context with those other
public inputs.

The actor, critic, stored rollouts, and PPO gradients all use the same mask.
The checkpoint persists input_mode=context_masked_v1 and uses the distinct format
edison_context_masked_ppo_v1, so older readers reject masked checkpoints instead
of silently treating them as full context. Unknown modes fail closed.
Legacy checkpoints without an input mode remain full context and retain identical
serialization. Legal actions, observation privacy checks, and available card/global
state stay intact. The agent can still infer information from legal actions and
public state; this ablation measures the explicit input channels.

Three masked models train from scratch for 32 rounds of 96 duels. Each uses the
identical initial parameters and same 3,072-duel schedule as its saved full-context
control, split equally across random/heuristic opponents and player seats. All six
ordered cross-deck pairings of Blackwing, Frog Monarch, and Machina Gadget are used.
Full-context training is reused from the immutable archive, not rerun or counted
as newly executed training. Its models are copied byte-for-byte and hash-verified.

Every masked rollout is checked for zeroed context inputs. All 96 round-level
optimizer passes replay from copied model/Adam state. Masked input weights remain
at their initial values, and masked critic weights remain zero. Initial uniform
policies produce identical full/masked production trajectories in regression tests.

## Validation and final testing

Validation uses the existing diagnostic seeds 40000000–40000007 and checkpoints
8, 16, 24, and 32 (768, 1,536, 2,304 and 3,072 training duels). Both versions rerun
all validation comparisons: 96 games per opponent and checkpoint, with fresh
serial/eight-thread replay. Each full-context validation trajectory must reproduce
the archived budget study exactly; this guards against changes to the control.

All training completes before validation. After validation, the runner writes a
pretest manifest of model hashes and the protocol before accessing final-test
seeds. It checks frozen hashes during and after testing. Selection is fixed at
round 32 for both versions; validation does not tune the budget or hyperparameters.

Final testing uses fresh seeds 60000000–60000015. Each version faces random and
heuristic opponents for 192 games per optimizer seed and opponent, repeated in
fresh serial/eight-thread workers. The frozen heuristic also faces random once
on the same matrix as a shared benchmark control. Training, validation, previous
test blocks and the new test block are disjoint.

The study executes 9,216 new training games, 9,216 validation executions, and
4,992 final-test executions: **23,424 new executions** including evaluation replays.
The 9,216 historical full-context training games are recorded separately.

## Interpretation and speed

The primary comparison is the per-seed full-context minus masked final-test win
rate. Report all three pairs, mean, sample standard deviation, range, and number
of positive differences. Means and variability are conditional on shared
environment seeds and fixed decks; repeated optimizer runs are not independent
tournament samples. Do not compare directly with prior studies' test win rates,
which use different seeds.

Both modes retain stochastic legal-action sampling. Evaluation speed counts both
players' decisions and includes reset, encoding, inference, audits, hashing, and
logging; worker startup is excluded. Serial and parallel speeds are separate.
The mask is applied after feature encoding, so masked timings represent this
ablation implementation, not an optimized context-free environment or encoder.
Masked training time includes startup, serialization and optimizer replay; the
full-context training time is historical, not a contemporaneous speed control.

## Reproduce

From the repository root with pinned dependencies, assets, and production binary:

    export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
    source .venv/bin/activate
    python tests/edison/run.py --output build/edison-rl-ablation/certification
    python tests/edison/ablate_context_rl.py

Use a fresh output directory for both commands when repeating the study; the
runner refuses to overwrite an existing study report. To train a single masked
run, pass --input-mode context_masked_v1 --rounds 32 --skip-evaluation to
train_context_rl.py with an optimizer seed and current certification report.

Prior study runners pin learner source hashes. Reproduce those studies from their
recorded commits; this new runner explicitly verifies full-context compatibility
through archived checkpoint bytes, math tests, and complete validation trajectories.

## Certification and evidence

The full certification suite passes 544 cases. Test 083G checks context invariance,
retained independent features, actor/critic masked gradients, numerical gradients,
checkpoint mode validation, and legacy byte compatibility. Test 083H checks
full/masked initial trajectory equality, exact rollout feature masking and
serial/parallel production replay for both seats.

Final results and the archive are under tests/edison/reports/rl-ablation-2026-11-02.
Complete worker logs, rollouts and intermediate optimizer states remain under
build/edison-rl-ablation.

A preliminary masked run was intentionally interrupted after 17 completed rounds
to correct checkpoint format versioning before any validation or final test.
Its partial evidence remains in build/edison-rl-ablation-format-draft and is
excluded from completed-study totals. Its report and logs are also archived.
The first 17 parameter and optimizer states were verified identical after restart,
confirming the correction changed only checkpoint format. The experiment restarted with the same seeds,
mask, algorithm, initialization and schedule; no evaluation-based selection occurred.
