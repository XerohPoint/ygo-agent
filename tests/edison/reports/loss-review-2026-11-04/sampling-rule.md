# Loss review: predeclared sampling rule

Written 2026-11-04 at commit 4e3452c BEFORE any per-game or per-cell outcome of
the round-32 models on the validation seeds was read by this review (only the
already published test-block averages from the handoff were known). The sha256
of this file is recorded by tools/edison/loss_review.py before game selection.
Any later change is an amendment and must be appended under "Amendments" with
its reason; the text above that heading stays frozen.

## Population

- Learner: full-context models, tests/edison/reports/rl-budget-2026-11-02/
  seed-{1702,1703,1704}/final-policy.json (stochastic sampling, as evaluated).
- Opponent: frozen `heuristic` policy.
- Duel seeds: validation 40000000-40000007 only. Test blocks 22M/50M/60M are not
  to be used for anything; the tool refuses seeds outside the validation block.
- Six ordered cross-deck pairings of blackwing, frog_monarch, machina_gadget,
  both learner seats: 8 x 6 x 2 = 96 games per model, 288 games in all.
- Aggregates cover ALL of those games and ALL losses. Nothing below limits them.

## Hand-review sample (at most 30 games)

Rank key for a game: sha256 of the UTF-8 string
`edison-loss-review-2026-11-04|<optimizer>|<deck0>|<deck1>|<learner seat>|<duel seed>`,
ascending. It does not depend on outcome or length.

Stratum: (optimizer seed, learner deck, learner seat): 18 strata of 16 games.

- Tier A, up to 18 losses: in each stratum, the lowest-ranked game the learner lost.
- Tier B, up to 6 losses, for variety in game length: for each learner deck, among
  that deck's losses not already selected, the one with the fewest decisions and
  the one with the most decisions (ties broken by rank).
- Tier C, up to 6 wins, as a control so a "mistake" seen in losses can be checked
  against wins: for each (learner deck, learner seat), the lowest-ranked win of one
  optimizer seed, rotating 1702, 1703, 1704 over decks in the fixed order blackwing,
  frog_monarch, machina_gadget with seat 0 before seat 1. If that optimizer has no
  win in that stratum, use the next optimizer in rotation.

A stratum with no eligible game is recorded as empty rather than replaced.
Draws, if any, count as neither loss nor win and are reported separately.

Rules for review:
- Selection happens once, mechanically, from outcomes. No game is swapped or dropped
  after being seen. Every selected game gets a recorded verdict, including
  "ordinary outcome uncertainty" and "no clear cause".
- Games are reviewed in order A, B, C. If not all can be read, the report says how
  many were read and does not extrapolate from the rest.
- Any further game read for a specific reason (for example, to look for a suspected
  bug) is labelled as a targeted lookup and never counted as part of the sample.

## Predeclared aggregates (all 288 games; losses versus wins)

By decision type (prompt message), learner deck, learner seat, game phase and turn:
heuristic agreement, score gap, pass/no-action rates. Also: how games end
(win_reason), game length in decisions and turns, summons, attacks, LP trajectories
as visible at learner decisions.

Heuristic comparison is a counterfactual score only, at the same decision, using
the learner's own encoder memory; it never changes the game. The heuristic is a
benchmark, not ground truth; disagreement is not by itself a mistake.

## Predeclared policy-surgery diagnostics (separate runs, same validation seeds)

Added so frequency tables are not read as causation. These change the games, so
they are kept apart from the aggregates above and do not count as the hand-review
sample. They do not train anything and no result is carried to any test seed.

- `argmax`: learner plays its most probable action instead of sampling.
- `hybrid:<class>`: the heuristic's choice replaces the learner's at one decision
  class only, for each class among: idle command, battle command, chain, card
  selection (card/tribute/sum), and all other prompts. The learner plays the rest.
  This asks which decision class accounts for the gap, not whether a hybrid is good.

With 288 correlated games per variant, a difference under about 8 percentage points
is not distinguished from noise and will be reported that way.

## Classification vocabulary

Policy choice; inadequate observation; unavailable legal action; historical
card/rule behavior; ordinary outcome uncertainty. A strategically poor choice is not
an environment bug. Suspected bugs get a seed/game/decision reproduction, not a fix.

## Amendments

### Amendment 1 (written after the first surgery run; post-hoc and labelled so)

The predeclared `argmax` run won 132/288 against 88/288 for sampling, and
`hybrid:idle` won 120/288. To decompose that, I add these runs, on the same validation
seeds only. They were not predeclared, were chosen after seeing those two numbers,
and do not change the hand-review sample or the aggregates:

- `argmax:<class>`: argmax at one decision class, sampling elsewhere.
- `resample:<k>` for k = 1, 2, 3: the same sampled policy with a different action RNG
  stream, to measure how much the 88/288 baseline moves from sampling luck alone.
- Control: frozen heuristic against itself on the same seeds and pairings.
- `sampled` and `argmax` against `random`, to check argmax is not merely exploiting
  the heuristic.

### Amendment 2 (capacity probe, post-hoc)

An offline probe fits the same 32-unit architecture and the same 128 input features to
the heuristic's top-scored action, using logged learner-view states from validation
games, with whole duel seeds held out. It exists only to answer whether the model and
features can represent the benchmark's behaviour. It is not a proposed policy, is never
saved outside build/, and is played only on held-out validation seeds.

### Amendment 3 (post-hoc)

To separate "too few games" from "model limit" without training anything: argmax play
of the archived round-8, round-16 and round-24 checkpoints on the same validation
seeds, next to the archived sampled validation results for those rounds.

### Amendment 4 (post-hoc)

Reference control: the untrained checkpoint under argmax, whose logits are all equal, so it
always plays the first offered action. One model only, since the untrained policies are
identical in behaviour. Also, 17 of the 30 selected games were read through a filtered view
(`games-filtered/`) rather than the full compact log; the report says which.
