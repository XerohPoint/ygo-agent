# Edison heuristic and learning baseline

This document records checkpoint **e8a8b67**. The subsequent additive observation
schema and opt-in context consumer are documented in
[edison-decision-context.md](edison-decision-context.md).

The production environment now supports an audited public-observation heuristic,
a uniform-random benchmark, and a small CPU behavior-cloning pilot. The original
random checkpoint is preserved in [edison-baseline.md](edison-baseline.md).
Final measurements and evidence are in
[the experiment summary](../tests/edison/reports/agents-2026-11-02/summary.json).

## Final results

The full suite passed **527 cases**, with identical replay traces. The recorded
match, stress and collection runs completed **3,360 of 3,360 executions**, with
no crashes, stuck prompts, action overflow, truncation or replay mismatch.

| Comparison | Wins | Win rate | Parallel decisions/s |
| --- | ---: | ---: | ---: |
| Heuristic vs random | 358/384 | 93.2% | 6,101 |
| Learned vs random | 165/192 | 85.9% | 6,275 |
| Learned vs heuristic | 67/192 | 34.9% | 5,499 |

On the directly matched 192-game subset, the heuristic beat random 180/192
(93.8%). The heuristic remains the stronger fixed-deck benchmark.

Training used 11,005 decisions; validation used
4,886. Final held-out action agreement was
88.8%. Optimizer replay produced identical weights.

Full reports, compressed per-game evidence, datasets, weights and hashes are
saved alongside the linked summary. No claim of general Edison playing strength
or exhaustive rules correctness follows from these bounded fixture results.

## Observation contract

Training and evaluation explicitly disable privileged opponent information.
Card IDs are big-endian indices into agent_codes.txt, not Yu-Gi-Oh passcodes.
Public metadata is looked up only after the environment supplies a visible
identity. Action column zero is a one-based card-row reference; zero means no
reference. Every decision checks identity agreement, known IDs, private-card
statistics, action counts, reward polarity, and natural terminal outcomes.

The audit fixed missing visible-card IDs, own-Deck feature leakage, blanket
private-zone reveal handling, and private target IDs in action/history encoding.
Explicit reveals are scoped to recipient and absolute card location. Both
unknown Decks are hidden. Own search candidates are visible and sorted by
identity rather than draw order; facedown opponent cards and unrevealed private
zones stay hidden. Movement, position changes and shuffles conservatively
invalidate location-based reveals. The interface does not retain everything a
human could remember, and deck-top reveal events are not currently persistent
observations.

The new info:legal_options field reports the pre-cap count; info:action_overflow
reports whether capacity omitted choices. Evaluation rejects overflow.
The new info:winner field is -1 for ongoing/truncated episodes, 0 or 1 for a
winner, and 2 for a draw. Budget exhaustion and core timeouts produce zero reward
and truncation, never a fabricated win. Both max_steps and max_episode_steps
are honored; natural wins on the final decision take precedence.
Truncated duels and recordings are cleaned up.

Test 078 exercises IDs, initial hidden zones, action references, a deliberately
undersized action buffer, both budget limits, and reset after truncation.
Test 080 covers hidden/public/search/reveal boundaries. Stress tests exercise
natural win/reward consistency, but did not produce draws or force timeout
handling. This is a bounded audit, not exhaustive certification of every
information-revealing card.

## Policies and fixed decks

The heuristic prefers useful summons, favorable attacks, cheap tribute/material
costs, valuable search/revival targets, and opposing removal targets. It assumes
1900 DEF for unknown defenders. Conservative activation rules avoid obvious
self-destruction, including Heavy Storm with only friendly back row. Per-turn
action counts discourage repeated effects. Stable tie breaking is deterministic.
There is no search, rollout, opponent model, deck-order access, or core query.

The fixtures are Blackwing, Frog Monarch, and a new 40-card Machina Gadget deck,
all checked by production Edison legality validation. The expanded preload
inventory has 67 identities. agent_assets.lock.json pins cards, decks, scripts
and helpers. The original smoke profile remains unchanged.

Test 079 checks summons, attacks, friendly-board preservation, targets, tribute
selection, actual Machina Gearframe searches, serial/parallel replay, and
relative worker-output paths. Stress orchestration exposed the latter defect:
changing worker cwd could write a report somewhere different from the parent's
reader. Paths are now absolute and worker configurations are verified.

## Evaluation design

Heuristic versus random uses 32 seeds, six ordered cross-deck pairings, and both
heuristic seats: 384 games, each replayed in fresh one-thread and eight-thread
workers. Random self-play adds 64 seeds across all nine pairings including
mirrors (576 games); heuristic self-play adds 32 seeds across that matrix
(288 games). Seed ranges start at 20110424, 20120424 and 20130424 respectively.

Executions record offered actions, selections, state hashes, card interactions,
terminal results, completion, latency and throughput. Replay checks compare
full per-game decision hashes and results. The 10,000-decision guard, overflow,
native diagnostics, crash, timeout or truncation fails evaluation. Failure
traces remain under build/edison-agents; unknown outcomes never count as wins.

Speed includes reset, Python inference, audits, hashing and flushed logging;
it excludes process/module/pool startup. Summary rates separate serial and
parallel configurations. Host load varies and stress groups may run concurrently;
these are instrumented integration rates, not raw training throughput.
Win rates apply only to these fixed fixtures and opponents. Paired seeds are
correlated, so reported Wilson intervals are descriptive.

## Small training experiment

Only after current binary/source/asset gates pass, the pilot collects 96 heuristic
self-play games using seed range 20200424. Seventy-two whole games train a
64-feature linear legal-action scorer; 24 games are held out for validation.
Features describe actions, prompts, card/board values and conservative activation
eligibility. This is behavior cloning, not reinforcement learning.

Training uses full-batch masked-softmax cross entropy for 120 updates, learning
rate 0.4, L2 0.0001 and zero initialization. A second optimization must produce
identical weights and metrics. The model is plain JSON; datasets and gate,
source and model hashes accompany the report.

The learned scorer plays random and heuristic opponents over 16 held-out seeds,
six ordered pairings, and both seats: 192 games per opponent, each replayed.
These are the first 16 seeds of the heuristic benchmark. The summary includes
the directly matched heuristic-versus-random subset. Evaluation seeds are not
used for training demonstrations.

## Reproduce

From the repository root with pinned dependencies and assets installed:

    export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
    source .venv/bin/activate
    xmake b edison_rules
    xmake b ygopro_ygoenv
    python tests/edison/run.py --output build/edison-agents/certification
    python tests/edison/evaluate_agents.py --output build/edison-agents/evaluation
    python tests/edison/evaluate_agents.py --challenger random --opponent random --count 64 --seed 20120424 --output build/edison-agents/stress-random
    python tests/edison/evaluate_agents.py --challenger heuristic --opponent heuristic --count 32 --seed 20130424 --output build/edison-agents/stress-heuristic
    python tests/edison/train_agent.py

Focused regressions use run.py --test 078 (or 079/080). Custom match example,
from the scripts directory:

    ../.venv/bin/python ../tests/edison/agent_match.py --deck1 machina_gadget --deck2 blackwing --policy0 heuristic --policy1 random

Keep random and heuristic benchmarks when moving on to reinforcement learning.
The pilot does not justify replacing the stronger heuristic.
