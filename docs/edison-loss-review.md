# Edison loss review

Date: 2026-11-04. Models: full-context PPO from `rl-budget-2026-11-02`, optimizer seeds
1702/1703/1704, final round. Opponent: frozen heuristic. Seeds: validation 40000000-40000007
only. Test blocks 22M/50M/60M were not used. Runtime sources are unchanged from 4e3452c.

## Fresh-seed result

The recommendation below passed its predeclared test on seeds 72000000-72000015, which played no part in the
review. [Protocol](../tests/edison/reports/greedy-test-2026-11-04/protocol.md),
[summary](../tests/edison/reports/greedy-test-2026-11-04/summary.json). 576 games per arm and opponent; no draws or
incomplete duels; all 36 replayed greedy cells matched by trace hash.

| Opponent | Sampled | Greedy | Mean paired gain ± SD | Gain by optimizer (1702, 1703, 1704) |
| --- | ---: | ---: | ---: | ---: |
| Heuristic | 173/576 (30.0%) | 261/576 (45.3%) | +15.28 ± 4.81 pp | +12.50, +20.83, +12.50 |
| Random | 502/576 (87.2%) | 526/576 (91.3%) | +4.17 ± 0.52 pp | +4.17, +3.65, +4.69 |

The heuristic against itself split 49-47 over 96 games. Greedy remains below parity, and it is 33.9%, 32.8% and 69.3%
with Blackwing, Frog Monarch and Machina Gadget. These are fixed decks and non-adapting opponents: this is not
evidence that greedy play is right against an adapting opponent or that the network understands more.

## Bottom line

**Most of the gap to the heuristic is action sampling, not what the network prefers.** The same
weights, on the same validation seeds, win 132/288 (45.8%) when they play their most probable
action, against a mean of 85/288 (29.5%) when they sample. Heuristic against itself is 50% by
construction here, so that closes about 16 of the 20.5 points. All three optimizer seeds improve.
This has not been tested on a fresh test block, and 288 correlated games are not tournament evidence.

The sampled policy leaves its top choice on half of its main-phase decisions (7.7 of 15.4 per loss).
It is a near-even draw across summons, sets and phase changes, so it often sets a trap instead of
summoning, or passes with an attack available. The games then end the same way: 188 of 200 losses
end on a battle event, usually a direct attack against an empty board.

A second layer of mistakes survives greedy play and is visible in the games: the features are nearly
card-blind. Solemn Judgment negates the learner's own Compulsory Evacuation Device, Soul Exchange
is played with no Tribute Summon after it, and Enemy Controller is fired in the Draw Phase. Those look
bad and are worth fixing later, but replacing every chain choice with the heuristic's did not measurably
move win rate against this opponent, so Solemn and most Enemy Controller misuse are not shown to cost many games.

No environment or rules bug was found.

## What was run

The sampling rule was written and hashed before any per-game outcome was read:
[sampling-rule.md](../tests/edison/reports/loss-review-2026-11-04/sampling-rule.md). Post-hoc additions
are appended there as amendments and labelled below.

- 288 games: 3 models, 6 ordered deck pairings, both seats, 8 duel seeds. 88 wins, 200 losses, 0 draws.
- `tools/edison/diag_worker.py` calls the unmodified `agent_match.main()` and swaps in thin agent
  subclasses. It reads heuristic scores from the `analyze()` call the encoder already makes.
- Hand review: 30 games chosen by the rule (24 losses, 6 wins). 13 were read as full compact logs;
  17 through a filtered view (turn boards, public events, decisions with p<.15 or score gap>=5).
  Verdicts are in [game-verdicts.json](../tests/edison/reports/loss-review-2026-11-04/game-verdicts.json).

Verification:

| Check | Result |
| --- | --- |
| Diagnostic trace against plain `run_worker()` trace, byte comparison | 36/36 cells identical |
| Plain duels against archived round-32 validation evidence | 36/36 cells identical |
| Redaction audit, 14,844 learner and 27,171 opponent records | 0 violations |

Opponent records hold only events a real duel reveals, and declined chain offers are not logged. The
126 options naming an opposing hand card all come from Trap Dustshoot (96) and Machina Fortress (30),
which reveal the hand. Diagnostics ran at 1,449 decisions/s against 1,632 plain, with six workers on
a shared machine. That is overhead, not a benchmark.

## All 288 games

Win rates, with the frozen heuristic in the same deck and seat as the reference (48 distinct mirror
games). "Sampled" is the mean of four action-RNG streams (88, 82, 82, 88 wins).

| Group | Sampled | Greedy | Heuristic mirror |
| --- | ---: | ---: | ---: |
| All | 29.5% | 45.8% | 50.0% |
| Blackwing | 25.5% | 38.5% | 50.0% (16/32) |
| Frog Monarch | 11.5% | 29.2% | 21.9% (7/32) |
| Machina Gadget | 51.6% | 69.8% | 78.1% (25/32) |
| Seat 0 (goes first) | 34.0% | 52.1% | 54.2% (26/48) |
| Seat 1 | 25.0% | 39.6% | 45.8% |

Frog Monarch's low score is mostly the fixture, not the learner: the heuristic also wins only 22%
with it. Losses by opposing deck: Machina 84, Blackwing 78, Frog 38. Losses by optimizer: 71, 71, 58.

Decision types in losses. Agreement counts ties with the heuristic's top score. The heuristic is a
benchmark, not ground truth. Zone placement and single-option prompts are excluded.

| Prompt | Prompts | Agree | Agree in wins | Mean top probability |
| --- | ---: | ---: | ---: | ---: |
| Idle command | 3,072 | 47.6% | 50.7% | .52 |
| Select card | 887 | 51.0% | 52.1% | .45 |
| Chain | 826 | 56.1% | 56.1% | .78 |
| Battle command | 639 | 71.8% | 83.4% | .81 |
| Effect yes/no | 482 | 82.8% | 90.6% | .86 |
| Select position | 446 | 59.2% | 69.6% | .66 |

By game phase, agreement in losses is 51% in Main 1 (4,091 prompts), 49% in Main 2 (665), 70% in Battle
(1,078), 63% in Draw (475) and 63% in Standby (237). By turn it is 53% (T1-5), 58% (T6-10), 57%
(T11-15), 59% (T16-20) and 56% (T21-25); labels above 16 are lower bounds. It barely varies by deck or seat at idle prompts (46-49%). Chain
agreement ranges from 42% for Frog Monarch to 67% for Machina.

Mistake rates per prompt hardly separate wins from losses. The learner leaves its modal idle action
at 50% of idle prompts in losses and 48% in wins. What separates them is the board:

| Per game | Losses | Wins |
| --- | ---: | ---: |
| Length, median turns (decisions) | 11 (105) | 16 (177) |
| Attack taken when one is offered | 76% | 92% |
| Direct attacks by learner | 1.04 | 3.98 |
| Direct attacks by opponent | 3.69 | 1.52 |
| Opponent Special Summons | 1.57 | 0.58 |
| Learner monsters at last decision of own turn 3, games still running | 0.93 | 1.30 |
| Same, own turn 5 | 0.81 | 1.45 |
| Attacks with no favourable visible target | 0.49 | 0.24 |
| Normal Summon taken when offered | 61% | 62% |
| Phase change when another action is offered | 19% | 21% |
| Direct attack taken when offered | 92% | 96% |

- **How games end:** all 288 by LP reaching 0 (win_reason 1). No deck-outs. 161 of the 200 losses end on
  an opposing direct attack and 27 on an attack into a monster.
- **First damage:** the learner took it first in 136 of 198 losses (2 had none visible), against 32 of 88 wins.
- **LP at turn 6, games that reach it:** ahead wins 54% (37/68), even 38% (20/53), behind 19% (29/152).
- **Starting position:** 122 of the 200 losses are starts where the heuristic mirror in that seat
  also lost. The sampled learner wins 46% of starts the mirror wins and 13% of those it loses. Greedy wins
  68% and 24%. That measures shared hands and matchups, not proof those starts were unwinnable.

## Policy surgery

These runs change the games, so they are kept apart from the tables above. Gains are against the
four-stream sampled mean. The interval resamples the 96 (pairing, seat, seed) units and ignores optimizer
variance, so treat it as descriptive. `argmax` and `hybrid` were predeclared. `argmax:<class>` and
the resampling streams are post-hoc (amendment 1).

| Variant | Wins/288 | Gain | Interval | Gain by optimizer |
| --- | ---: | ---: | --- | --- |
| Greedy everywhere | 132 | +16.3 pp | +10.4 to +22.6 | +13.5, +20.1, +15.4 |
| Heuristic at idle prompts | 120 | +12.2 pp | +5.5 to +19.0 | +18.8, +7.6, +10.2 |
| Greedy at idle only | 102 | +5.9 pp | +1.1 to +10.8 | +7.3, +4.4, +6.0 |
| Heuristic at select prompts | 96 | +3.8 pp | -1.3 to +9.3 | +2.1, +0.3, +9.1 |
| Heuristic at battle prompts | 95 | +3.5 pp | -0.4 to +7.4 | 0.0, +2.3, +8.1 |
| Heuristic at other prompts | 94 | +3.1 pp | -2.1 to +8.4 | +3.1, -0.8, +7.0 |
| Greedy at other prompts | 90 | +1.7 pp | -3.4 to +6.6 | +1.0, +2.3, +1.8 |
| Greedy at chain prompts | 89 | +1.4 pp | -3.0 to +5.6 | +3.1, -2.9, +3.9 |
| Greedy at battle prompts | 87 | +0.7 pp | -3.1 to +4.7 | -2.1, +0.3, +3.9 |
| Heuristic at chain prompts | 86 | +0.4 pp | -4.8 to +5.6 | -1.0, +2.3, -0.3 |
| Greedy at select prompts | 82 | -1.0 pp | -5.5 to +3.6 | -2.1, -0.8, -0.3 |

- Only the first three are distinguishable from noise. Idle prompts are where the gap lives.
- Per-class greedy gains sum to about +9 pp, below the +16 pp of greedy everywhere. I did not establish why.
- Against random, greedy also gains: 259/288 (89.9%) against 247/288 (85.8%). So it is not merely exploiting
  the heuristic, though random is a weak check.
- Reference points (post-hoc, amendment 4): the untrained model (uniform sampling) wins 7/96 per archive. Always playing the first
  offered action wins 19/96 (19.8%), so plain consistency is itself worth a lot here.

Comparing learner games directly: of the 200 sampled losses, the greedy game from the same start won 68,
and 24 of the 88 sampled wins became greedy losses.

## Concrete examples

Game IDs are `optimizer/deck0-deck1-seatN/duel seed`; `#n` is the decision index. Regenerate any game with
`loss_review.py render --game <id>`.

**Near-even summon choice.** `1702/frog_monarch-machina_gadget-seat0/40000002`, #48: with Raiza and Swap
Frog available and two Treeborn Frogs to tribute, the offers split .21, .21, .25. Raiza is not summoned
until #98 (turn 13). Over all games, the highest-ATK Normal Summon is picked 56% of the time when sampling
and 87% when greedy (ties count as highest).

**A low-probability draw.** `1702/frog_monarch-machina_gadget-seat0/40000002`, #101: Raiza's effect returns
the learner's own Treeborn Frog to the Deck (p=.04) when Fortress (p=.28) and a set card (p=.38) are
available. In `1703/blackwing-frog_monarch-seat1/40000004`, #88: moves to Main 2 at p=.04 instead of
attacking with Gorz 2700. Sampled games average 2.1 such p<.10 choices per loss.

**Throwing away a board.** `1702/blackwing-frog_monarch-seat0/40000006`, #40: with Black Rose Dragon 2400 and
a set Icarus Attack against one Swap Frog 1000, activates Black Rose's destroy-all (p=.51). The destroy-all
self-damage pattern occurred 4 times in 288 games: rare, but decisive here.

**Self-inflicted Heavy Storm.** `1702/machina_gadget-blackwing-seat0/40000002`, #76: leading 8000/5800, fires
Heavy Storm with only its own Mirror Force and Trap Dustshoot set and the opposing back row empty. Heavy
Storm hit more of its own back row in 17 of 52 activations in losses and 2 of 21 in wins. Under greedy
play this falls to 5 of 42 and 1 of 26. Mostly sampling.

**Solemn Judgment on itself.** `1703/blackwing-machina_gadget-seat1/40000003`, #21-23: activates Compulsory
Evacuation Device, then chains Solemn Judgment (p=.71), negating its own card for 4000 LP. Model 1704
repeats it from the same start (#21-22, p=.91 each), so this is not sampling luck. Overall, a mechanical
check puts 16 of 55 Solemn activations on the learner's own activation. Elsewhere it pays half LP for small
monsters: `1704/frog_monarch-blackwing-seat1/40000004`, #26, Treeborn Frog, 8000 to 4000.

**Soul Exchange without a Tribute Summon.** `1702/machina_gadget-frog_monarch-seat1/40000002`, #17-20: targets
Sangan (p=.87) rather than Machina Fortress, then ends the turn holding Raiza. Soul Exchange also forbids the
Battle Phase. Only 5 of 32 activations are followed by a Monarch, Light and Darkness Dragon or Gorz summon
that turn. The encoder does not show hand contents, so it cannot condition on holding one.

**Enemy Controller in the Draw Phase.** `1704/frog_monarch-blackwing-seat0/40000006`, #62-65: at 1000 LP, in
the Draw Phase, tributes Caius 2400 to borrow Blackwing Shura 1800. Its two modes get p=.50 each because the
encoder gives them identical features. It was activated 159 times, 106 of them in the Draw Phase. The rate per
game is similar in Frog Monarch wins and losses (1.47 and 1.69).

**Attacking into larger monsters.** `1704/machina_gadget-blackwing-seat1/40000001`, #32 and #34: attacks 1700
and 1400 into Fortress 2500 at low LP. This gets worse under greedy (0.86 per loss), since it is the modal choice.

**Ordinary uncertainty.** `1702/blackwing-machina_gadget-seat1/40000000` and
`1704/blackwing-machina_gadget-seat1/40000000` share a start: no summonable monster for 15 turns except Gorz.
Both models lost. `1704/machina_gadget-frog_monarch-seat1/40000002` plays a good line (Soul Exchange into
Caius banishing Fortress) into two Bottomless Trap Holes.

## Classification

Hand review of the 24 sampled losses: **19 policy choice, 5 ordinary outcome uncertainty**, and none primarily caused by
unavailable actions or rule behavior. Those labels are my judgment and the 17 filtered reads got less scrutiny.
Most policy verdicts cite more than one mistake, and encoder blindness contributed to several.

| Issue | Class | Evidence |
| --- | --- | --- |
| Sampling away from the modal action | Policy choice (exploration kept at play time) | 13.6 of 32.7 choices per loss; greedy +16.3 pp |
| Modal idle choices worse than the heuristic's | Policy choice | Heuristic at idle +12.2 pp, greedy at idle +5.9 pp |
| Attacks with no favourable target | Policy choice | 0.49 per loss, 0.24 per win; heuristic at battle +3.5 pp, not distinguishable |
| Traps left unset at end of turn | Policy choice | 0.67 of 1.43 turn-ending idle choices per loss |
| Self-damaging Heavy Storm, Black Rose | Policy choice | 19 of 73 Storms; 4 Black Rose nukes |
| Aliased cards and effects | Inadequate observation, encoder | 13.2% of prompts contain actions with identical features |
| Hand contents absent from action features | Inadequate observation, encoder | Soul Exchange follow-through 5 of 32 |
| Chain stack not observable at chain prompts | Inadequate observation, interface | `decision_` is card-selection only; 1,286 chain prompts; Solemn on own cards |
| End-phase hand-limit discard is random | Unavailable legal action (known limit) | At most 1 of 1,939 own turns; no effect seen |
| No unselection during multi-select | Unavailable legal action (known limit) | No decisive case in the sample |
| Ignition effects at chain prompts after a summon | Historical rule behavior | Sirocco, Dark Grepher, Gearframe; Edison ignition priority |
| Treeborn Frog retries into Light and Darkness Dragon | Historical rule behavior, plus policy | `1704/frog_monarch-blackwing-seat0/40000003` #67-76; LADD falls 2800 to 800; covered by test 048 |
| Bad starts and removal draws | Ordinary outcome uncertainty | 5 of 24 reviewed; 122 of 200 losses from mirror-lost starts |

Blanket chain activation deserves a caution. The learner activates at 74% of prompts the heuristic calls unsafe
in losses and 81% in wins, and at 99% under greedy. It looks terrible in the logs, yet heuristic chain choices
gained only +0.4 pp. Do not assume it is costing many games against this opponent.

Detectors for these card patterns were written after reading games, so they are post-hoc counts.

## Suspected environment bugs

**None.** Four things looked wrong and were checked:

1. **Two Normal Summons under one turn label (21 cases).** In 16, Swap Frog's effect was activated that turn, which
   grants the extra summon. The other 5 are all labelled turn 16 or later. The observation's turn saturates at 16,
   and prompts with one option auto-respond (`ygopro.h`, `legal_actions_.size() == 1`), so a turn with no real choice
   leaves no record and my counter merges two turns. Deck counts drop across the two summons in the 4 cases where they
   are visible, consistent with a skipped turn. `1704/blackwing-frog_monarch-seat0/40000001` (opponent, end of
   game) has no learner record to check it against. An earlier version of the counter also mislabelled the crossing
   into turn 16, and I fixed that and regenerated everything. This is a diagnostics limit: **turn numbers above 16 are
   lower bounds.**
2. **Gearframe's Union equip at a chain prompt.** The script declares it as an ignition effect. This is the
   project's historical ignition-priority behavior.
3. **Treeborn Frog activating repeatedly in one Standby Phase.** Light and Darkness Dragon negates it and it
   retries. Test 048 covers this.
4. **Random end-phase discard.** A documented interface limit.

I did not audit legal-action completeness against the card rules. A missing legal action would not show up in
this review unless it was obvious from a log.

## Recommendation

**Play and evaluate the learned policy greedily: argmax at play time, with the sampled win rate kept as a
training diagnostic only.** Do not change the weights, features or training.

Why this one:

- It has the largest measured effect (+16.3 pp), positive for every optimizer and larger than any single-class change.
- It holds at every checkpoint tested (table below), and against random as well.
- It costs no training and no environment change.
- It re-baselines everything after it. Against a noisy baseline a feature change has to overcome a policy that
  discards half its own preferences, and the sampled 768-to-3,072 gain partly measures sharpening.

Evidence that would falsify it, on a newly predeclared block, with the same 576 games per arm and paired by game
start. Supervisor note: this report first suggested 70000000-70000015, but the forward-model spike already uses the
70M block and the pool audit uses 71M, so the test uses 72000000-72000015 under
[its protocol](../tests/edison/reports/greedy-test-2026-11-04/protocol.md):

- Mean paired gain across the three optimizers below +5 pp. Some shrinkage from +16 is expected.
- Any optimizer with a gain at or below zero.
- Greedy below sampled against random by more than about 3 pp.
- Any incomplete duel.

What it does not do: it does not make the network smarter, and modal mistakes (chain activation, attacks into larger
monsters) remain. Deterministic play could also become exploitable once opponents adapt, so this needs re-examination
at the league and search milestones. Neither current opponent adapts.

Not recommended yet: card-identity features. The aliasing is real, but I have no evidence yet of their win-rate value at
this budget, and the capacity probe below reached near parity without them.

## Is the 32-unit model limiting, or is it too few games?

**At this benchmark, the evidence points to too few games and sampling, not capacity. For going beyond the heuristic,
the features will probably limit, but that is untested.**

Capacity probe (amendment 2): the same 32-unit network and 128 features, fitted by supervised learning to the
heuristic's top choice with whole duel seeds held out, reached **91.4-93.4% held-out agreement** (idle 86-91%). Played
greedily on held-out seeds it won **41/96 (42.7%)**. With 96 games that is compatible with parity (50%) and not much
above greedy PPO. So the model and features can represent roughly heuristic-level play. It says nothing about stronger play.

Learning curve, wins of 288, no new training (amendment 3):

| Training round (duels) | Sampled, archive | Greedy |
| --- | ---: | ---: |
| 8 (768) | 27 | 100 |
| 16 (1,536) | 56 | 94 |
| 24 (2,304) | 70 | 113 |
| 32 (3,072) | 88 | 132 |

- Greedy at 768 duels (34.7%) already beats sampling at 3,072 (30.6%). Much of what the sampled curve shows is the
  policy sharpening, and it is still far from sharp: mean entropy only fell from about 1.17 to about 1.0 nats.
- The greedy curve is still rising, by about 11 pp from 768 to 3,072 duels, though not smoothly (round 16 dips). More games
  look useful. Three seeds cannot say where it plateaus.

Features: 13.2% of prompts (18.7% of idle, 30.5% of select-card) hold two actions with identical features but a different
card, action or effect. Heavy Storm equals Icarus Attack and Mirror Force equals Fissure when set; Caius equals Raiza. A heuristic-derived
"safe" flag separates only some of them. Card-specific errors that persist under greedy play (Enemy Controller, Soul Exchange,
Solemn Judgment) cannot be fixed by a network that cannot tell these cards apart or see the chain. I expect this to bind after
parity, but no experiment here shows that.

## Not verified

- **Nothing was tested on a fresh test block.** Greedy's gain is a validation result across 288 correlated games.
- Three optimizer seeds and 8 duel seeds are small. Differences under about 8 pp are not distinguished from noise.
  With about a dozen variants compared post hoc, the smaller gains could easily be chance.
- Greedy was not tested in self-play or against anything except the heuristic and random.
- Full certification was not rerun. No runtime file changed, and new code is only in `tools/edison/`.
- 17 of the 30 reviewed games were read through a filter that hides ordinary decisions, and hand review only covers the
  sample. Frequencies come from mechanical counts, some written after reading games.
- Heuristic disagreement is not an error measure, and the "unsafe" label is the heuristic's own rule.
- The capacity probe used one fit per fold. Three initializations per fold took about 8 minutes per fit on the shared machine,
  so I stopped after 4 fits and reused them. It trained on learner-visited states only.

## Reproduce

From the repository root, using at most 6 single-thread workers:

~~~bash
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
.venv/bin/python tools/edison/loss_review.py run
.venv/bin/python tools/edison/loss_review.py verify
.venv/bin/python tools/edison/loss_review.py select
.venv/bin/python tools/edison/loss_review.py render
.venv/bin/python tools/edison/loss_review.py audit
.venv/bin/python tools/edison/loss_review.py surgery --control --modes argmax hybrid:idle hybrid:battle hybrid:chain hybrid:select hybrid:other argmax:idle argmax:battle argmax:chain argmax:select argmax:other resample:1 resample:2 resample:3 sampled@random argmax@random
.venv/bin/python tools/edison/loss_review.py surgery-report
.venv/bin/python tools/edison/loss_review.py curve
.venv/bin/python tools/edison/loss_review.py controls
.venv/bin/python tools/edison/loss_aggregate.py
.venv/bin/python tools/edison/loss_aggregate.py --kind surgery/argmax
.venv/bin/python tools/edison/capacity_probe.py
~~~

`run` refuses to overwrite existing output unless given `--force`. Logs are in
`build/edison-loss-review-2026-11-04/games/` (full), `games-compact/` and `games-filtered/`. Summary evidence is in
[tests/edison/reports/loss-review-2026-11-04](../tests/edison/reports/loss-review-2026-11-04/).
