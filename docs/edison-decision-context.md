# Edison decision context

Checkpoint **e8a8b67** freezes the audited heuristic/random/learned baseline,
including its reports, datasets and weights. This change adds explicit selection
context while preserving the original card, action, global and history layouts.
The original heuristic and 64-feature learned policy remain available unchanged;
the new consumer is opt-in as the context policy.

The full certification suite passes **536 cases** with identical replay output.
A 16-seed matrix across all six ordered deck pairings and both seats completed
**768/768 evaluation executions**, including fresh serial/parallel repeats.
The context policy won **180/192 games against random (93.75%)** and **96/192
against the frozen heuristic (50%)**. The frozen heuristic also scored 180/192
against random on these seeds. These results establish compatibility and tested
context coverage; they do not demonstrate a playing-strength improvement.

## Additive observation schema

The environment returns two additional arrays. They are zero at terminal states.
On other prompts, decision_[0] is the schema version (1). An inactive selection
has all other decision_ entries and the entire selection_ array zero.

decision_ is an int32 array of length 12:

| Index | Meaning |
| ---: | --- |
| 0 | Schema version: 1 |
| 1 | Standard selection hint, or 0 when unknown |
| 2 | Execution stage: 0 unknown, 1 cost callback, 2 activation setup callback, 3 operation callback |
| 3 | One-based visible source-card row; 0 if unavailable/private |
| 4 | Protocol minimum selection bound |
| 5 | Protocol maximum selection bound |
| 6 | Number of selected cards, including mandatory selections |
| 7 | Number of mandatory selected cards |
| 8 | A finish action is currently offered |
| 9 | A cancel action is currently offered |
| 10 | Raw sum target for a sum-selection prompt, otherwise 0 |
| 11 | Card-selection prompt is active |

Common hints include 500 release, 501 discard, 502 destroy, 503 banish, 504 send
to Graveyard, 505 return to hand, 506 add to hand, 507 return to Deck, 509 Special
Summon, 512 Synchro material, 531 tribute, 549 attack target, and 551 effect target.
Other standard hints in the pinned 500–575 range are preserved. The hint describes
the requested operation; it does not establish whether that operation is a cost.

Stages come from the innermost executing Lua callback in the pinned core's waiting
processor stack. This is read-only: no core patch or duel-rule change is involved.
Stage 2 does not prove formal targeting; scripts also use target callbacks for
non-targeting setup. Stage 3 includes summon procedures as well as resolving
effects. Some scripts pay costs inside a different callback, so stage is not an
exhaustive replacement for card-specific rules or REASON_COST.

For attack-target choices, the source is the current attacker. Otherwise it is
the executing effect's handler, provided that card is visible in the observation.
No card identity, private position or unredacted effect description is exported
through this reference.

selection_ is a uint8 array of shape (2 * max_cards, 3), aligned with cards_ rows:

| Column | Meaning |
| ---: | --- |
| 0 | Card is referenced by a currently offered legal selection action |
| 1 | Card is already selected |
| 2 | Card is a mandatory selection (also marked in column 1) |

Rows can move when observations change; use the current masks rather than
persisting row numbers across prompts. Local multi-selections accumulate choices.
Select/unselect messages supply their authoritative selected list. Sum-selection
messages supply mandatory cards separately. Protocol bounds are not automatically
remaining counts, especially for sum/weighted selections. Offered actions remain
the authority for legal continuations.

## Hidden information and lifecycle

Hints are scoped to the recipient and consumed at the next prompt. Unknown or
card-passcode hint payloads are suppressed instead of being copied into the
observation. Prompt state clears on transitions and reset, preventing prior
targets, costs or selections from appearing in the next decision.

Selected-card records use locations, ignoring protocol card codes. Existing
visibility rules still determine identities and features. A selected opponent's
hidden card can have a selection flag while its identity and statistics remain
zero. Source references are suppressed entirely when the source is private.
The owner retains knowledge of offered Deck candidates during a multi-selection;
candidate display order remains based on identity, not draw order.

This does not expand the existing legal-action interface. In particular,
unselection remains unavailable, the existing end-phase hand-limit discard
shortcut remains automatic, and unsupported selection modes remain unsupported.
Only card-selection prompts receive this context in version 1.

## Consumer and validation

ContextHeuristic uses the visible source for attack-target ranking, distinguishes
discard/material choices from searches, and prefers opposing removal targets.
It does not infer an attacker from an earlier action. Random, heuristic and learned
policies keep their original inputs and policy behavior. Collecting demonstrations
for the new context consumer is intentionally rejected until a versioned learned
feature schema is added; the saved 64-feature model does not consume these arrays.

Test 081 checks hint privacy/lifecycle, real Brionac cost-versus-target choices,
and Dark World Dealings discards during resolution. Both card fixtures have
Edison and Rule 5 controls. Test 082 checks:

- Identical offered cards produce different choices for attack, destruction,
  discard and search context.
- Seeded production games actually reach costs, attack targets, removals, searches,
  selected hidden cards, and mandatory Synchro materials.
- Local multi-step selections retain earlier choices.
- Every mask, count, source reference and action reference is consistent; private
  identities and statistics remain hidden.
- Serial and parallel runs produce identical context-inclusive decision hashes.
- The frozen heuristic and saved model retain checkpoint step counts, interaction
  counts and outcomes over their compatibility seeds.

The benchmark runner hashes both new arrays and logs decision context and selected
row indices. Reports include context coverage counters. Original checkpoint
evidence remains immutable; new evidence is stored under
[decision-context-2026-11-02](../tests/edison/reports/decision-context-2026-11-02/summary.json).

## Reproduce

From the repository root with pinned assets and dependencies installed:

    export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
    source .venv/bin/activate
    xmake b edison_rules
    xmake b ygopro_ygoenv
    python tests/edison/run.py --output build/edison-context/certification
    python tests/edison/evaluate_agents.py --challenger context --opponent random --count 16 --output build/edison-context/vs-random
    python tests/edison/evaluate_agents.py --challenger context --opponent heuristic --count 16 --output build/edison-context/vs-heuristic

Focused regressions use run.py --test 081 or --test 082. No new training run or
replacement of the frozen learned weights is part of this change.


A subsequent versioned learner consumes these observations in the
[context-aware PPO pilot](edison-rl-pilot.md). The context checkpoint's original
64-feature model and benchmark results remain unchanged.
