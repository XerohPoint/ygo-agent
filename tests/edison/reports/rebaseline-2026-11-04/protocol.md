# Edison re-baseline on the hardened simulator

Written and hashed before any model played these seeds on this binary. Simulator: main at ce823ec, certified 567 PASS,
production module sha256 038e6dd19f4efc17..., core e8978744....

**Question.** The frozen models were trained and measured on the older simulator (module c5ecba9c). What do they
score after the rule fixes? This does not retrain anything, so these models are not adapted to the new rules.

## Models and matrix

Frozen, no selection: NumPy baseline `rl-budget-2026-11-02/seed-{1702,1703,1704}`; league at rounds 32, 160 and 320;
gauntlet arm at round 160; each with optimizers 1702, 1703, 1704, on code list v1 (53fcea4d). All play is greedy.

| Block | Seeds | Matrix | Use |
| --- | ---: | --- | --- |
| Fixtures | 75000000-75000063 | 3 fixtures, 6 ordered pairs, both seats, 768 games per model and opponent | Main readout |
| Gauntlet | 75010000-75010007 | 21 lists, next 4 in a cycle, both seats, 1,344 games per model | Descriptive |

Fixture opponents: heuristic and random for every model. League 320 and gauntlet 160 also play the same-index baseline.
Controls: heuristic mirror and heuristic against random. Gauntlet opponent: heuristic only.

Networks use the public repetition guard (`RepeatGuard` in `actor.py`) on the gauntlet block only. It is off on fixtures, where
no game looped in the pilot. The two gauntlet lists with XX-Saber Gottoms still play without it, because code list v1 lacks
that card. Any failure voids its whole cell. At least 1 in 24 cells are replayed with a different thread count, and any trace mismatch stops the run.

## Rules for what will be concluded

Pre-hardening means are from `tests/edison/reports/learner-2026-11-04/` on seeds 73900000-73900063. Against the heuristic:
baseline 45.2, league 32 32.8, league 160 49.8, league 320 51.0, gauntlet 160 43.4.

- **H1, carry-over.** For each arm, `CARRIES_OVER` if the mean against the heuristic is within 3 pp of pre-hardening. Otherwise `SHIFTED`.
  These blocks differ, so a shift under about 2 pp may be sampling noise.
- **H2, order.** `NET_AHEAD` if league 320's mean paired gain over the baseline against the heuristic is above 0 and mean head to head is at
  least 50%. Otherwise `NOT_SHOWN`. Budgets differ, so this cannot rank the two methods.
- **H3, candidate.** The earlier bar: mean at least 55% and every optimizer at least 50% against the heuristic, on fixtures. Not an M1 pass.
- **H4, guard.** `GUARD_WORKS` if no gauntlet cell voids at the decision limit. Other voids are reported by reason. A group above 1% void is `INVALID`.

No model, seed or rule changes after this hash in response to results. Three optimizers sharing environment seeds are not independent samples.
Heuristic and frozen opponents are not a measure of tournament strength.
