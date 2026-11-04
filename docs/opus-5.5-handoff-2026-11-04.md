# Opus 5.5 handoff — Edison-format AI

Prepared November 4, 2026. Read this before changing the simulator or starting
another experiment.

## Start here

Latest completed work: the decision-context ablation, committed as **4e3452c**
on branch **edison**. The repository was clean before this handoff was added.
All experiments initiated in the preceding chat have finished. There is no
pending experiment to resume.

The project has a pinned Edison simulator, deterministic certification, legal
fixed decks, audited observations/actions, random and heuristic benchmarks, and
a reproducible CPU PPO learner. Retain full decision context as the baseline.
Its average advantage over masking is positive but variable; the learned policy
still loses most games against the heuristic.

**Recommended next task:** diagnose recurring mistakes in validation losses,
then choose one concrete improvement from the evidence. The user's latest
request is this handoff, not a new training run. The preceding ablation task is
complete; do not repeat it by default.

## Workspace and execution

- Actual checkout: `/home/mada/ygo-agent` in WSL Ubuntu-22.04.
- Windows path: `\\wsl.localhost\Ubuntu-22.04\home\mada\ygo-agent`.
- Branch: `edison`. Latest code/evidence commit: `4e3452c`.
- The desktop advertises `C:\home\mada\ygo-agent`, but that path was unavailable
  in the execution environment. Reuse the WSL checkout.

Windows commands were run from an existing directory such as `C:\Users\i-sk-`,
using `wsl -- bash -c 'cd /home/mada/ygo-agent && ...'`. WSL execution needed
sandbox escalation in this session; automatic review approved task-scoped
actions. Respect the next session's actual permissions.

Inside WSL:

~~~bash
cd /home/mada/ygo-agent
git status --short
git log -6 --oneline
export PATH="/home/mada/.local/bin:$PATH"
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
source .venv/bin/activate
~~~

Use `.venv/bin/python`; system Python lacks required packages such as NumPy.
XMake exists at `/home/mada/.local/bin/xmake` but may be absent from a
noninteractive shell's PATH. `rg` was unavailable in WSL; fall back to
`grep`/`find` if necessary. No AGENTS.md was found under `/home/mada` during
this session; recheck if the checkout changes.

The production module and certification executable already exist. After
native/core changes:

~~~bash
xmake f -y
xmake b ygopro_ygoenv
xmake b edison_rules
~~~

Workers require the repository's `scripts` directory as cwd. Prefer the existing
`run_worker()` wrapper: it handles cwd, absolute paths, timeouts, diagnostics and
report validation.

## Checkpoints and certification

| Commit | Completed work |
| --- | --- |
| `e8a8b67` | Audited heuristic/random agents, stress coverage, imitation baseline |
| `9fa3b58` | Decision-context and selected-card observations |
| `7f02418` | Initial context-aware PPO pilot |
| `f19798b` | Three additional 768-duel optimizer-seed replications |
| `792a6f1` | Controlled 768-versus-3,072-duel budget comparison |
| `4e3452c` | Decision-context ablation and evidence |

Latest recorded full certification: **544 passed, 0 failed**, with identical
event/response traces across two executions. While preparing this handoff,
current runtime sources matched all **102 archived source fingerprints**.
The suite was not rerun merely to write documentation.

Older documents retain historical counts such as 519, 527, 536 and 540.
The latest ablation report and appended certification sections supersede those
counts. These fixtures do not establish exhaustive Edison correctness.

## Current results — keep test sets distinct

The budget study used test seeds 50000000–50000015. Increasing training from
768 to 3,072 duels improved all three optimizer seeds:

| Opponent | 768-duel mean | 3,072-duel mean | Mean paired gain |
| --- | ---: | ---: | ---: |
| Random | 69.27% | 87.85% | +18.58 percentage points |
| Heuristic | 14.06% | 29.69% | +15.62 percentage points |

The ablation used fresh test seeds 60000000–60000015. Both versions had the same
3,072-duel budget, architecture, initial parameters, optimizer seeds
1702/1703/1704, training schedule, rewards and opponents. Full-context models
were reused from the immutable budget archive and evaluated again; three masked
models were trained from scratch.

| Opponent | Full mean ± sample SD | Masked mean ± sample SD | Full minus masked ± sample SD |
| --- | ---: | ---: | ---: |
| Random | 87.50% ± 1.38 pp | 83.85% ± 4.63 pp | +3.65 ± 5.80 pp |
| Heuristic | 32.99% ± 3.70 pp | 28.12% ± 2.27 pp | +4.86 ± 5.52 pp |

Full context won two of three paired seed comparisons against each opponent.
Per-seed final-test wins, each out of 192:

| Seed | Full/random | Masked/random | Full/heuristic | Masked/heuristic |
| --- | ---: | ---: | ---: | ---: |
| 1702 | 167 | 158 | 57 | 59 |
| 1703 | 166 | 171 | 62 | 51 |
| 1704 | 171 | 154 | 71 | 52 |

The shared heuristic-versus-random control won 185/192 (96.35%).

The completed ablation had **23,424 executions**: 9,216 new masked training
games, 9,216 validation executions and 4,992 final-test executions. Evaluation
totals include serial/parallel repeats. All completed-study duels finished;
all evaluation replays matched; all 96 masked optimizer passes replayed exactly;
all 24 full-context validation comparisons reproduced the previous archive.

Parallel evaluation averaged 3,883 decisions/s against random and 3,822 against
heuristic for full context; masked rates were 3,685 and 3,547. These count both
players, reset, encoding, inference, audits, hashes and logging, and exclude
worker startup. Masking occurs after encoding: this is not an optimized
encoder-speed experiment. Masked training averaged 291 seconds/run.

The old 29.7% and new 33.0% full-context heuristic scores concern the same models
on different test seeds. Do not describe that difference as learning. Three
optimizer seeds sharing environment seeds are not independent tournament samples.
Paired SD exceeds the mean ablation gain; do not claim a universal or
statistically established advantage.

## Simulator and observation invariants

Pinned components:

- Core: `ygopro-core 0.0.2`, commit
  `f96929650ff8685b82fd48670126eae406366734`.
- Scripts: commit `8e7fde9bf1b5a3934cfb8c91f23fadb322106d3e`.
- Database source: commit `7b1874301fc1aa52bd60585589f771e372ff52cc`.
- `scripts/script` points to `/home/mada/ygopro-scripts`.
- Database: `assets/locale/en/cards.cdb`.
- Locks: `tests/edison/assets.lock.json` and `agent_assets.lock.json`.
- Core recipe/patch: `repo/packages/y/ygopro-core/xmake.lua` and
  `early-trigger.patch` in that directory.
- Historical adaptations: `ygoenv/ygoenv/ygopro/historical_scripts.h`.

The historical backlog covered Field Spell replacement, ignition priority,
SEGOC/Early Triggers, private-hand and delayed triggers, Damage Step exceptions,
continuous/persistent negation, cost departures, face-down resets,
leaving/returning, and card interactions. Preserve Rule 5 comparison behavior
when fixing Edison rules.

Production uses legal Blackwing, Frog Monarch and Machina Gadget fixtures under
`assets/edison/decks` and `assets/edison/agent_decks`. The preload inventory
`assets/edison/agent_codes.txt` contains 67 identities. Modern example decks
fail Edison legality checks. Synthetic exact-state fixtures intentionally need
not contain legal decks.

Preserve these contracts:

- `oppo_info=False`; no privileged opponent state, deck order or core queries.
- Visible IDs index `agent_codes.txt`; they are not card passcodes.
- Action column zero is a one-based current card-row reference; zero means none.
- Hidden identities/statistics stay redacted. Own offered search candidates can
  be visible without exposing deck draw order.
- `decision_`: int32[12]; `selection_`: uint8[2 * max_cards, 3].
- Source/selection row indices can move between prompts.
- Stage 2 is an activation setup callback, not proof of formal targeting.
- Offered actions determine legality; selection bounds need not be remaining counts.
- `info:winner` is -1 ongoing/truncated, 0/1 winner, 2 draw.
- Attribute terminal training rewards to the learner's absolute seat.
- Workers reject overflow, truncation, crashes and diagnostics. Bounds are
  256 actions, 10,000 decisions, core timeout 10 seconds and worker timeout
  180 seconds. Incomplete games must not become wins or losses.

Known interface limits: no unselection action, automatic end-phase hand-limit
discard shortcut, unsupported selection modes, imperfect reveal memory and
no persistent deck-top reveal observation.

## Learner and mask

`ygoai/edison_rl.py` contains the NumPy learner. The actor has 128 inputs,
32 tanh hidden units and one logit per legal action. The critic uses the mean
offered-action feature vector. The actor starts uniform; the critic starts at
zero. There is no imitation warm start, search or opponent model.

The first 64 inputs are engineered public features, including a conservative
activation eligibility indicator derived by the heuristic. Heuristic action
scores and imitation labels are discarded. This is not raw-card end-to-end RL.

Only completed episodes supply rewards: +1 win, -1 loss, 0 draw; gamma 1.
Advantages are terminal returns minus the frozen rollout critic, normalized
with inverse learner-decision-count weights. Adam settings: learning rate .002,
256 decision segments/minibatch, up to four epochs, clip .2, entropy .01,
value coefficient .5, gradient cap .5 and full-round KL stop .03.

The ablation zeroes **52 inputs: columns 64–111 and 121–124**, preserving
76 independent inputs, including 12 public features added alongside context.
Masking all columns 64–127 would be a different experiment. Actor, critic,
stored rollouts and gradients all use the mask.

Checkpoint formats:

- Full: `edison_context_ppo_v1`; absent input_mode means full.
- Masked: `edison_context_masked_ppo_v1`, input_mode `context_masked_v1`.
- Unknown/inconsistent modes or formats fail closed.
- Legacy full-context serialization stays unchanged.
- The 64-feature imitation model is a separate policy.

One preliminary masked run was interrupted after 17 completed rounds to correct
the distinct checkpoint format before evaluation. Evidence remains under
`build/edison-rl-ablation-format-draft` and in the archive. All 17 parameter and
optimizer states matched after restart. That run is excluded from completed-study
totals; no evaluation-based selection occurred.

## Read these files

- `docs/edison-rl-ablation.md` — latest protocol, results and limitations.
- `docs/edison-rl-budget.md` — budget comparison.
- `docs/edison-rl-pilot.md` — learner design.
- `docs/edison-decision-context.md` — observation schema and privacy semantics.
- `docs/edison-agents.md` — audits, benchmark policies, limits.
- `docs/edison-certification.md` — rules fixtures and current appended tests.

| Implementation | Responsibility |
| --- | --- |
| `ygoai/edison_rl.py` | Encoder, model, mask, PPO gradients, Adam |
| `ygoai/edison_policy.py` | Frozen heuristic, context heuristic, imitation policy |
| `tests/edison/train_context_rl.py` | Schedule, collection, training, replay, certification gate |
| `tests/edison/ablate_context_rl.py` | Latest paired study and pretest manifest |
| `tests/edison/budget_context_rl.py` | Prior budget study |
| `tests/edison/replicate_context_rl.py` | Prior optimizer replications |
| `tests/edison/evaluate_agents.py` | Six cross-deck pairings, both seats, serial/parallel replay |
| `tests/edison/agent_match.py` | Production worker, audits, traces and rollouts |
| `tests/edison/rl_tests.py` | Test 083A–H |
| `tests/edison/run.py` | Certification and fingerprints |
| `ygoenv/ygoenv/ygopro/ygopro.h` | Production protocol/environment |
| `ygoenv/ygoenv/ygopro/decision_context.h` | Decision/selection context |

Committed evidence lives under `tests/edison/reports/` in
`rl-2026-11-02`, `rl-replication-2026-11-02`, `rl-budget-2026-11-02` and
`rl-ablation-2026-11-02`. Start with `summary.json`. Detailed reports are often
gzip-compressed JSON. Snapshots record source, binary, asset and artifact hashes.

Complete JSONL worker traces and rollouts remain in ignored `build/edison-rl-*`
directories. Archives preserve representative rollouts, policies and reports,
not every raw worker trace.

Full final model:
`tests/edison/reports/rl-budget-2026-11-02/seed-1702/final-policy.json`.

Masked final model:
`tests/edison/reports/rl-ablation-2026-11-02/context_masked_v1/seed-1702/final-policy.json`.

Equivalent paths exist for seeds 1703 and 1704. Current traces contain choices,
offered actions, decision context, selection rows, state hashes and terminal
results. Inspect their schema before assuming they contain a fully readable
board history. Loss diagnosis may need richer visible-state logging.

## Seeds and reproduction

| Use | Seeds |
| --- | --- |
| Optimizer | 1701 pilot; 1702/1703/1704 paired studies |
| Training rounds 0–7 | 21000000 + round * 100000 + cell * 1000 + environment_id |
| Training rounds 8–31 | 30000000 + (round - 8) * 100000 + cell * 1000 + environment_id |
| Pilot/replication test | 22000000–22000015 |
| Validation | 40000000–40000007 |
| Budget test | 50000000–50000015 |
| Ablation test | 60000000–60000015 |

The test blocks have been inspected and are no longer fresh holdouts. Develop
on validation, then predeclare and verify new final-test seeds. Check older
baseline/stress scripts too. Extending the original 21-million training
arithmetic would collide with the 22-million test block.

The trainer supports only 8 or 32 rounds and defaults to 8 rounds, seed 1701
and full context. Specify intended settings explicitly.

Focused RL regressions:

~~~bash
python tests/edison/run.py --test 083 \
  --output build/edison-handoff-2026-11-04/test-083
~~~

Full certification when the change/build requires it:

~~~bash
python tests/edison/run.py \
  --output build/edison-handoff-2026-11-04/certification
~~~

Complete ablation reproduction, only if that is the intended task:

~~~bash
python tests/edison/run.py \
  --output build/edison-rl-ablation-reproduce-2026-11-04/certification
python tests/edison/ablate_context_rl.py \
  --output build/edison-rl-ablation-reproduce-2026-11-04
~~~

Use a fresh output directory: the runner refuses to overwrite study reports.
The full instrumented pipeline takes tens of minutes. Older study runners pin
the old learner source hash; reproduce them at their recorded commits instead
of weakening hash checks.

Validation evaluation of a saved model:

~~~bash
python tests/edison/evaluate_agents.py \
  --challenger rl --opponent heuristic --count 8 --seed 40000000 \
  --model tests/edison/reports/rl-budget-2026-11-02/seed-1702/final-policy.json \
  --output build/edison-validation-review-2026-11-04/seed-1702-vs-heuristic
~~~

Evaluation uses stochastic sampling, not argmax. Change model/seed/output
deliberately. Certification fingerprints runtime sources, including Python
files directly under `tests/edison`. Regenerate the gate after editing/adding
those sources, before training. Do not edit runtime source during a study.
Documentation-only edits do not invalidate these runtime fingerprints.

## Recommended continuation

1. Diagnose full-context validation losses against the heuristic across all
   three optimizer seeds, decks and seats. Predeclare a bounded sampling rule
   that includes varied losses. Keep consumed final tests out of tuning.
2. Expand diagnostic traces only as needed: readable visible cards/board,
   offered choices, chosen action, probabilities, context and terminal result.
   Preserve redaction and verify logging does not change actions. Label
   diagnostic overhead rather than comparing it as normal benchmark speed.
3. Classify observed issues: policy choice, inadequate observation, unavailable
   legal action, historical card/rule behavior, or ordinary outcome uncertainty.
   A strategically poor choice alone is not an environment bug.
4. Turn confirmed environment bugs into minimal deterministic fixtures with
   Rule 5 controls where applicable. Run affected tests first, then relevant
   integration/full certification before a new training gate.
5. Choose one evidence-supported learner/feature change and compare it at the
   same budget against frozen references, using validation and fresh final-test
   seeds. Report completion, paired gains, variation and measured speed.

A useful immediate deliverable is a reproducible loss-review tool/report with
concrete examples, frequency by decision type/deck/seat, and one justified
proposed improvement. Include a regression and validation for any actual bug.

More optimizer seeds, another legal archetype, and stronger policies or search
are later options. They are not completed work or instructions to run everything
immediately.

## Collaboration preferences

The user prefers execution over repeated permission requests. Once a task is
authorized, complete implementation, appropriate checks, review and a concrete
report. They requested scoped tests during incremental rules work and full
certification at checkpoints; match testing to the actual current task.

Keep progress updates useful during long runs. Use new report directories,
explicit seeds/checkpoints, preserve Rule 5 controls and historical evidence,
and report failures honestly. Never discard failed trials to improve an average.
Do not equate bounded certification with exhaustive rules correctness or
fixed-deck win rates with tournament strength.
