# Edison held-out decks

M3's study report: how the networks play decks they never trained on. It has two stages, each with rules fixed before
any game:

- **[Stage A](#stage-a-new-builds-of-known-cards), new builds of known cards: met**, November 10, 2026.
- **[Stage B](#stage-b-lists-with-cards-no-training-list-plays), lists with cards no training list plays: met**,
  November 16, 2026. Retraining for unknown cards was not shown to help.

Stage A's rules are in [the M3 Stage A protocol](edison-m3-protocol.md), and the list selection in
[`stage-a-lists.json`](../tools/edison/heldout/stage-a-lists.json). The run, analysis and search check all ran at commit
86ddb40, which holds only the manifest [`m3a-manifest.json`](../tools/edison/heldout/m3a-manifest.json) on top of commit
bf2f736. The result files are in [`tests/edison/reports/m3-stage-a-2026-11-09/`](../tests/edison/reports/m3-stage-a-2026-11-09/).

## Stage A: new builds of known cards

**Verdict: `STAGE_A_MET`**, November 10, 2026. Every rule held, on all 488 held-out builds and on the far set F
(the 102 builds at distance 20 or more from every training list, so about 10 or more cards differ). Sol recounted it
independently and the verdict stands; see [the recount](#independent-recount).

A known card is one that some training list plays in its Main or Extra, or one of code list v1's tokens and closure
cards, as the protocol says. One build, h_fl1242, uses a closure card no training list holds: Elemental HERO Bubbleman.
Every other card in the 488 builds is in some training list's Main or Extra.

The networks are `checkpoint-2560` of optimizers 1702, 1703 and 1704, playing greedily with the repetition guard. The
opponent is the rule-based heuristic. Every held-out build met each of the 21 training lists in both seats, 4 seeds per
cell, in three arms: the network piloting the held-out build (pilot), the network facing it (face), and the heuristic on
both sides (mirror). Intervals are the protocol's two-stage bootstrap over builds, then cells.

| Rule | Bar | All 488 builds | F, 102 builds |
| --- | --- | --- | --- |
| A1 pilot mean | at least 55% | 80.6% | 81.8% |
| A1 lowest optimizer's 2.5th percentile | above 50% | 77.8% (1703) | 78.0% (1703) |
| A1 weaker seat | at least 50% | 78.6% (seat 1) | 80.1% (seat 1) |
| A2 face mean | at least 55% | 71.5% | 77.9% |
| A2 lowest optimizer's 2.5th percentile | above 50% | 67.4% (1703) | 73.3% (1703) |
| A2 weaker seat | at least 50% | 69.1% (seat 1) | 75.6% (seat 1) |
| A3 gap, 2.5th percentile | above −10 points | −1.1 points | −1.2 points |
| A4 void share, worst block, arm and optimizer | at most 1% | 0% | 0% |
| A4 means with voids and interrupted cells as losses | at least 55% | 80.6% and 71.5% | 81.8% and 77.9% |
| A4 missing or failed chunks, replay differences, empty build arms | 0 | 0 | 0 |

**Per optimizer**, with the 2.5th percentile in brackets:

| Optimizer | Pilot, all | Face, all | Pilot, F | Face, F |
| --- | --- | --- | --- | --- |
| 1702 | 82.6% (81.9) | 73.9% (72.6) | 83.4% (82.0) | 80.0% (77.8) |
| 1703 | 78.5% (77.8) | 68.8% (67.4) | 79.4% (78.0) | 75.9% (73.3) |
| 1704 | 80.5% (79.8) | 71.7% (70.4) | 82.6% (81.1) | 77.9% (75.5) |

**Gap.** For each build, the network's edge over the heuristic on that build minus its edge on the build's nearest
training list (its twin), averaged over optimizers. The mean is +0.3 points (−1.1 to +1.9) on all builds and +1.5 points
(−1.2 to +4.3) on F. Without the mirror terms, the pilot rate on a build minus the pilot rate on its twin averages
−0.7 points on all builds and −0.8 on F. Between builds the gap runs from −5.9 points (10th percentile) to +6.7 (90th).

**The run.** 149,646 cells and 771,456 games ran on 15 workers; the play log's chunk times sum to 3.8 hours. There
were no void games, draws or interrupted cells. All 1,179 replays, run at 4 env threads, matched their first run. Every
game ended on life points (768,333) or a failed draw (3,123), and the longest took 1,582 decisions. The networks made
40.4 million decisions and the heuristic 86.0 million. The repetition guard fired on 1.63% of the networks' decisions;
a fire need not change the choice.

### Reported, not gated

**By distance from the nearest training list:**

| Distance | Builds | Pilot | Face | Gap | Gap without mirror terms |
| --- | ---: | --- | --- | --- | --- |
| 1-9 | 182 | 79.7% | 66.9% | +0.8 | −0.2 |
| 10-19 | 204 | 80.7% | 72.4% | −0.7 | −1.1 |
| 20 or more (F) | 102 | 81.8% | 77.9% | +1.5 | −0.8 |

The pilot rate is not lower for builds further from training. The face rate rises with distance, and so does the
share of archetypes the networks face well: the 1-9 band is mostly Vayu Turbo (96) and Blackwing (57), while F is led by
Hero Frog (32), which the networks face at 87%.

**By archetype**, the ones with more than 10 builds:

| Archetype | Builds | Pilot | Face | Gap | Gap without mirror terms |
| --- | ---: | --- | --- | --- | --- |
| Blackwing | 148 | 76.8% | 53.3% | +1.0 | −1.7 |
| Vayu Turbo | 143 | 82.6% | 71.0% | +0.3 | −0.3 |
| Hero Frog | 80 | 87.1% | 87.4% | −2.6 | −1.1 |
| Diva Hero | 16 | 81.4% | 82.4% | +12.7 | +3.2 |
| Gladiator Beast | 15 | 64.3% | 78.7% | −0.5 | +1.9 |
| Gigavise | 13 | 75.5% | 98.6% | −1.3 | +0.2 |
| Frog OTK | 12 | 72.3% | 96.8% | −1.5 | −1.8 |
| Diva Hero Beat | 11 | 86.3% | 81.5% | 0.0 | −1.7 |
| Machina Control | 11 | 86.8% | 70.5% | +4.8 | −0.8 |

With each of the 21 archetypes weighted equally, the pilot rate is 79.6%.

**Against the training lists.** In the known block, each training list met all 21 training lists, itself included, with 32 seeds per cell. There the
networks scored 78.8% as pilot, 78.7% as face, and the heuristic's mirror rate averaged 50.3%. On the held-out builds,
the pilot rate is about the same (80.6%) but the face rate is lower (71.5%). The held-out builds are also stronger in
the heuristic's hands: their mirror rate averages 58.1%. Most of this is Blackwing, which is 30% of the held-out builds and 2 of the 21 training lists. The heuristic
plays it well, and the networks face it poorly in the known block too: 48.2% against `g_blackwing`, the lowest of the 21,
and 57.3% against `blackwing`. Without the 148 Blackwing builds, the face rate is 79.4% and the mirror rate 50.2%, close
to the known block's 78.7% and 50.3%. That cut was made after the run. Face has no rule that takes out how strong the
build is, so its rates are not a measured drop in generalization either way.

**The weakest pilot rates** follow the training lists the networks already play worst. The 2 Dragon Turbo builds score
54.6% and 57.5%, against 61.8% on `g_dragon_turbo`. The 15 Gladiator Beast builds average 64.3%, against 62.4% on
`g_gladiator_beast`.

**The lowest gaps** are single builds, and one build's 168 games per optimizer support no claim about it. Three of the
five lowest are Blackwing builds whose nearest training list is `g_vayu_turbo_ddev08` (−24.5, −21.0 and −16.6 points), so
their twin is another archetype's list. The other two are a Quickdraw Frog build at distance 44 (−20.9) and a Machina
Gadget build at distance 40 (−18.4).

**Artwork aliases.** Distance and duplicates count passcodes, so an alternate artwork counts as a different card.
Seven builds use one. h_fl75591 and h_fl85597 differ only in which Foolish Burial artwork they play, so they are one
deck, and with aliases merged 11 builds move 2 closer to their twin, one of them out of F. Sol rescored the same games on
that basis, keeping h_fl75591: on 487 builds the pilot rate is 80.6%, face 71.4% and the gap's 2.5th percentile
−1.1 points; on the 101 builds of F they are 81.7%, 77.8% and −1.3. Every rule still holds. That cut was made after the
run, and the frozen test is the passcode one.

## Independent recount

Sol recounted the run with its own code, which neither imports nor copies the runner's analysis, from read-only copies
of the raw records. Its report and summaries are in
[`tests/edison/reports/m3-stage-a-2026-11-09/recount/`](../tests/edison/reports/m3-stage-a-2026-11-09/recount/).

- It regenerated the job design from the protocol and matched the frozen jobs hash, and the search check's. Every row,
  duel seed and winner, journal and replay checked out, and all 534 frozen hashes matched.
- Every gated and reported number matched to 1e−12, except the bootstrap percentiles and two seat fields. Its own
  bootstrap drew in a different order. Its 2.5th percentiles differ by at most 0.11 points, and none changes a rule.
- It checked that the pilot arm counts the network on the held-out build, face counts the network on the training list,
  and mirror counts the held-out build with the heuristic on both sides, in both seats.
- It reran 42 cells drawn across both blocks, all arms, optimizers and seats, at 1 and at 4 env threads: 84 runs and
  1,120 duels, none different. It reran 12 search pairs, 48 games including the one with voided worlds, none different.

Its findings, all now reflected here:

- **The result file's mirror seat-0 fields are mislabeled.** `known_mirror_seat0` and `held_mirror_seat0` are the held-out
  side's win rate when it sits in seat 0, not seat 0's rate. Seat 0 wins 51.8% of known mirror games and 51.7% of
  held-out ones. An earlier draft of this report quoted the mislabeled fields. No rule uses them.
- **The decision count included the heuristic.** The networks made 40.4 million of the 126.4 million decisions, and the
  guard fired on 1.63% of theirs, not 0.52%.
- **Wording:** the closure card above, the known block's self-matchups, the source of the run time, and the search's
  departures.
- **Scope:** the guard is part of the tested player, and artwork aliases are counted as different cards, both above.

## Search check, reported only

Run after the verdict, at M2's frozen setting: 64 rollouts, depth 48, 4 candidates, margin 1.0, binary `167be8aa`
(build id `d35e8512ba2cce6f`, as in M2). 60 drawn held-out builds each met 4 drawn training lists, on one seed from
88800000 in both seats. The search pilots the held-out build against the same network without search, and each search
game is compared with the plain game on the same seed, decks and network. The plain game scores the held-out build in
plain network play, so it need not be 50%. Ranges resample held-out builds.

| | Comparisons | Builds | Search | Plain | Gain | 95% range |
| --- | ---: | ---: | --- | --- | --- | --- |
| All | 480 | 60 | 79.4% | 50.8% | +28.5 points | 24.0-33.1 |
| Search in seat 0 | 240 | 60 | 82.1% | 49.6% | +32.5 | 26.7-38.3 |
| Search in seat 1 | 240 | 60 | 76.7% | 52.1% | +24.6 | 17.9-31.3 |
| F | 80 | 10 | 78.8% | 58.8% | +20.0 | 8.8-31.3 |

All 480 comparisons counted, with no missing or void games and no draws. On the training lists, M2's final test gave
80.3% and a gain of +30.3 points (27.8-32.6). F holds only 10 of the drawn builds, so its range is wide.

Search departed from the network's choice on 16.2% of 30,468 searched decisions, and fell back to it on 6.1% because
the first 8 deals were all refused. It took 65 s of CPU per game. 24 sampled worlds were voided in 1.79 million rollouts, all in one game
(h_fl39260 against `g_lightsworn_fairy`, seed 88800031). The first diagnostic was "Deck card in unsupported prompt:
select_card". That game finished normally. M2 had no voided world in 4.0 million rollouts.

This check keeps M2's limits: the search is told the opposing list, hidden cards are dealt uniformly rather than from a
belief, and every opponent is one of the same three networks.

## What Stage A shows

On new builds of known cards, the networks play about as well as on their 21 training lists.
The pilot rate is 80.6%. The edge over the heuristic is, on average, within a point of the edge on each build's nearest
training list (+0.3 points, −1.1 to +1.9), and neither is lower on the far set, where about 10 or more cards differ
from every training list. M2's search gains about as much on these builds as on the training lists. This is the first
half of M3's exit.

It does not show:

- strength against people, since the opponent is the same weak heuristic M1 measured against;
- anything about cards the networks never trained on, which is Stage B;
- coverage of the format, since the builds are placed lists from one site's events, weighted toward Blackwing, Vayu Turbo
  and Hero Frog;
- anything about a single build or matchup;
- the strength of the bare networks, since the repetition guard is part of the tested player and was not removed;
- strength with siding or in matches, since these are single game-1 duels, played without Sides and, in 24 builds,
  without XX-Saber Gottoms.

## Stage B: lists with cards no training list plays

**Verdict: `STAGE_B_MET`**, November 16, 2026. Every rule held, on all 747 held-out builds and on the heavy set H (the
206 builds with 6 or more copies of unknown cards in the Main Deck). **The reading that matters most came out
`NOT_SHOWN`: retraining with an unknown-card row did not clearly beat M1's networks loaded with no retraining.** A
Claude Sonnet subagent recounted the run with separate code and the verdict stands; see
[the recount](#stage-b-independent-recount).

The rules were fixed in [the M3 Stage B protocol](edison-m3b-protocol.md) before the dropout pilot and before any
training. The protocol and runner are as of commit a198b71. The pilot and training ran at fd40c56, which adds only
the manifest [`m3b-manifest.json`](../tools/edison/heldout/m3b-manifest.json). The test and analysis ran at 07301a4,
which adds only the seal [`m3b-checkpoints.json`](../tools/edison/heldout/m3b-checkpoints.json). The result files are in
[`tests/edison/reports/m3-stage-b-2026-11-12/`](../tests/edison/reports/m3-stage-b-2026-11-12/). Simulator: certified
module `d1f1f701` (core hardening r5).

**What was tested.** A card is unknown if no training list plays it in its Main or Extra Deck. A network sees an
unknown card's printed features but reads one shared identity row for all of them. Two sets of networks played:

- **Retrained networks.** M1's trainer, settings, budget and seeds on code list v2, with identity dropout: in 75% of
  training games, a random share of the known cards was hidden behind the unknown row. `checkpoint-2560` of optimizers
  1702, 1703 and 1704.
- **Baseline networks.** M1's own `checkpoint-2560` networks, loaded into the wider table with no retraining. Their
  unknown row is the mean of their known rows.

Every held-out build met each of the 21 training lists in both seats, 4 seeds per cell. The arms are Stage A's (pilot,
face, mirror) plus the baseline as pilot. The opponent is the same rule-based heuristic.

| Rule | Bar | All 747 builds | H, 206 builds |
| --- | --- | --- | --- |
| B1 pilot mean | at least 55% | 75.6% | 73.3% |
| B1 lowest optimizer's 2.5th percentile | above 50% | 74.2% (1704) | 70.8% (1704) |
| B1 weaker seat | at least 50% | 73.4% (seat 1) | 70.8% (seat 1) |
| B2 face mean | at least 55% | 76.7% | 77.4% |
| B2 lowest optimizer's 2.5th percentile | above 50% | 75.6% (1702) | 75.7% (1704) |
| B2 weaker seat | at least 50% | 74.4% (seat 1) | 75.0% (seat 1) |
| B3 gap, 2.5th percentile | above −10 points | −0.8 points | −4.9 points |
| B4 void share, worst block, arm and optimizer | at most 1% | 0% | 0% |
| B4 means with voids and interrupted cells as losses | at least 55% | 75.6% and 76.7% | 73.3% and 77.4% |
| B4 missing or failed chunks, replay differences, empty build arms | 0 | 0 | 0 |

**Per optimizer**, with the 2.5th percentile in brackets, and the baseline network of the same optimizer:

| Optimizer | Pilot, all | Face, all | Pilot, H | Face, H | Baseline pilot, all | Baseline pilot, H |
| --- | --- | --- | --- | --- | --- | --- |
| 1702 | 75.9% (75.1) | 76.6% (75.6) | 73.6% (72.1) | 77.4% (75.8) | 77.5% | 74.3% |
| 1703 | 75.9% (75.2) | 76.9% (75.9) | 73.9% (72.3) | 77.5% (76.0) | 73.2% | 69.3% |
| 1704 | 75.0% (74.2) | 76.6% (75.7) | 72.5% (70.8) | 77.3% (75.7) | 75.5% | 72.5% |

**Gap.** As in Stage A: the network's edge over the heuristic on a build minus its edge on the build's twin. The mean
is +0.3 points (−0.8 to +1.4) on all builds and −2.9 points (−4.9 to −0.9) on H. Without the mirror terms it is
−2.1 points on all builds and −4.9 on H. Between builds the gap runs from −10.9 points (10th percentile) to +10.9
(90th), a wider spread than Stage A's.

**The run.** 333,144 cells and 1,875,888 games ran on 15 workers in 11.2 hours. There were no void games and no
interrupted cells. All 2,624 replays, run at 4 env threads, matched their first run. 1,865,589 games ended on life
points, 10,294 on a failed draw, and 5 were draws; the longest took 1,697 decisions. The networks made 102.0 million of
the 298.5 million decisions, and the repetition guard fired on 1.79% of theirs.

### Stage B readings

Fixed in the protocol and reported beside the verdict. They are not part of it.

| Reading | Rule | Result | Outcome |
| --- | --- | --- | --- |
| Retraining, all builds | pilot minus baseline at least 3 points, 2.5th percentile above 0 | +0.2 points (−0.1 to +0.5) | `NOT_SHOWN` |
| Retraining, H | the same | +1.3 points (+0.7 to +1.9) | `NOT_SHOWN` |
| Known cards | on the training lists, pilot at most 5 points below baseline | 77.2% against 78.8%: −1.6 points (−2.0 to −1.3) | `KEPT` |
| Hidden identities | hiding 5 identities of a training list costs under 10 points at the 2.5th percentile | −1.3 points (−1.5 to −1.0) | `SMALL_COST` |

**Retraining.** The baseline networks pilot the held-out builds at 75.4% and the retrained ones at 75.6%. On H the
retrained networks are ahead by 1.3 points, and the interval excludes zero, but that is under half the 3 points the
protocol asked for. The difference grows with the number of unknown copies:

| Unknown Main Deck copies | Builds | Pilot | Baseline | Pilot minus baseline | Face | Mirror | Gap |
| --- | ---: | --- | --- | --- | --- | --- | --- |
| None: Extra Deck only | 66 | 77.7% | 78.6% | −0.9 | 82.5% | 43.2% | −0.8 |
| 1-2 | 319 | 76.1% | 76.4% | −0.4 | 74.0% | 53.9% | +1.2 |
| 3-5 | 156 | 76.8% | 76.4% | +0.3 | 79.0% | 49.6% | +3.1 |
| 6-10 | 128 | 74.9% | 74.2% | +0.8 | 76.7% | 52.9% | −2.8 |
| 11 or more | 78 | 70.7% | 68.5% | +2.2 | 78.6% | 50.8% | −3.0 |

The bands have no intervals of their own. The three retrained networks span 1.0 point on all builds and 1.4 on H,
while the three baseline networks span 4.3 and 5.0, so part of any difference is which M1 run is which.

**Known cards.** On the 21 training lists the retrained networks pilot at 77.2% and the baseline at 78.8%. Dropout
cost 1.6 points on the cards both sets trained on, about what it gained on H.

**Hidden identities**, on the training lists, against the same decks and seeds with nothing hidden:

| Identities hidden | Pilot rate | Change | 95% range |
| --- | --- | --- | --- |
| None | 77.2% | | |
| 2 | 76.8% | −0.4 | −0.6 to −0.2 |
| 5 | 76.0% | −1.3 | −1.5 to −1.0 |
| 10 | 74.6% | −2.6 | −2.9 to −2.3 |
| All (21 to 47) | 68.0% | −9.2 | −9.7 to −8.8 |

With every identity of its own list hidden, a retrained network still beats the heuristic 68% of the time, so much
of its play rests on printed features, position and the action offered. The cost is uneven: `g_frog_otk` falls from
63.2% to 28.6% and `g_frog_monarch` from 78.6% to 55.1%, while `g_lightsworn` falls from 82.9% to 79.4%.

**Usage.** How often the counted player chose an option on a card when one was offered, on the held-out builds:

| Action | Retrained: known | Retrained: unknown | Baseline: known | Baseline: unknown | Heuristic: known | Heuristic: unknown |
| --- | --- | --- | --- | --- | --- | --- |
| Activate | 45.0% | 48.0% | 45.3% | 40.6% | 5.0% | 1.1% |
| Normal Summon | 44.0% | 27.5% | 49.4% | 31.1% | 88.6% | 65.3% |
| Special Summon | 57.0% | 38.1% | 45.5% | 27.3% | 79.2% | 42.2% |
| Set a monster | 14.5% | 8.8% | 13.8% | 3.3% | 1.4% | 1.1% |
| Set a Spell or Trap | 18.3% | 9.7% | 15.3% | 12.0% | 47.8% | 44.0% |
| Attack | 80.3% | 50.9% | 87.3% | 51.2% | 60.6% | 41.9% |

Neither set of networks treats unknown cards as blanks: both activate, Summon and attack with them. The retrained
networks activate unknown cards more often than the baseline does (48.0% against 40.6%) and Set unknown monsters more
often (8.8% against 3.3%). Both attack with unknown monsters much less readily than with known ones. The table does
not say whether a choice was good, and known and unknown cards are different cards, so the two columns of one player
are not a like-for-like comparison.

### Stage B, reported and not gated

**By kind of unknown card:**

| Unknown cards | Builds | Pilot | Baseline | Face | Mirror | Gap |
| --- | ---: | --- | --- | --- | --- | --- |
| Monsters only | 319 | 77.2% | 77.2% | 77.8% | 50.3% | −1.0 |
| Spells and Traps only | 144 | 74.9% | 74.5% | 66.1% | 63.0% | +2.4 |
| Both | 284 | 74.2% | 73.8% | 80.9% | 47.2% | +0.7 |

**By archetype**, the ones with more than 20 builds, and the two that stand out below them:

| Archetype | Builds | Pilot | Baseline | Face | Mirror | Gap | Gap without mirror terms |
| --- | ---: | --- | --- | --- | --- | --- | --- |
| Blackwing | 88 | 75.9% | 75.2% | 53.4% | 74.9% | +2.6 | −1.4 |
| Gladiator Beast | 57 | 63.0% | 61.2% | 81.2% | 49.5% | +2.6 | −0.9 |
| Vayu Turbo | 56 | 80.1% | 79.9% | 69.8% | 60.8% | 0.0 | −1.4 |
| Lightsworn | 42 | 79.9% | 80.2% | 81.9% | 46.2% | +2.9 | −2.6 |
| Frog Monarch | 38 | 77.5% | 80.0% | 86.3% | 38.5% | +0.9 | −2.1 |
| Dragon Turbo | 37 | 56.4% | 56.4% | 95.7% | 25.1% | −0.6 | −5.7 |
| Diva Zombie | 35 | 77.4% | 76.1% | 70.7% | 62.8% | −5.4 | +2.1 |
| Hero Beat | 29 | 80.0% | 80.1% | 87.8% | 45.4% | +6.6 | −6.9 |
| Machina Control | 24 | 86.9% | 87.0% | 66.3% | 64.9% | −0.7 | −0.1 |
| Diva Hero | 23 | 80.0% | 80.4% | 82.1% | 46.6% | +13.0 | +3.7 |
| Hero Frog | 22 | 83.0% | 86.4% | 87.7% | 36.8% | −3.4 | −2.2 |
| Quickdraw Plant | 21 | 81.0% | 80.2% | 82.1% | 44.7% | −0.3 | +3.6 |
| Fairy | 17 | 78.9% | 76.2% | 69.7% | 62.7% | −16.6 | +1.7 |
| Frog OTK | 11 | 53.0% | 58.8% | 97.3% | 13.7% | −9.6 | −10.1 |

With each of the 95 archetypes weighted equally, the pilot rate is 74.6%. The held-out builds' mirror rate averages
51.6%.

- **Fairy's gap is the heuristic's doing, not the network's.** The networks pilot Fairy at 78.9%, above their rate on
  its twin, but the heuristic plays these builds far better than it plays `g_lightsworn_fairy` (62.7% against 44.3%),
  so the edge over the heuristic is smaller. Three of the five lowest single-build gaps are Fairy builds.
- **The weakest pilot rates follow the weakest training lists**, as in Stage A. Frog OTK builds score 53.0% against
  63.2% on `g_frog_otk`, and four of them are the four lowest builds (30.8% to 42.1%). Dragon Turbo builds score 56.4%
  against 62.1% on `g_dragon_turbo`. 15 builds are under 50% for the retrained networks, and 15 for the baseline.
- **By unknown card**, among the 62 in 10 or more builds, the retrained networks do best against the baseline on
  builds with Gravekeeper's Spy (+4.3 points over 23 builds) and Exploder Dragon (+4.2 over 10), and worst on builds
  with Dark Dust Spirit (−3.1 over 31). A cut by one card mixes it with the rest of each build.
- **Wider tables.** 4,395 of the retrained networks' decisions as pilot offered a value only the wider action tables
  hold, such as a declared card name.

**The dropout pilot** ran once, before training, on training lists only: one network per setting, 640 rounds,
1,344 games per setting and dose.

| Setting | Games left alone | Nothing hidden | 5 hidden | All hidden |
| --- | ---: | --- | --- | --- |
| S0, no dropout | all | 67.3% | 65.5% | 51.0% |
| S1 | 50% | 61.5% | 58.5% | 43.5% |
| S2 | 25% | 68.0% | 67.9% | 61.5% |

S1 fell more than 4 points below S0 with nothing hidden and was not admissible, so the rule picked S2. S1 scoring
below both neighbours suggests one short run per setting is noisy, as the protocol warned.

**Training.** The three runs were stopped once at rounds 1535, 1574 and 1657 to free the machine and resumed from
saved state three days later; the trainer resumes from the round's checkpoint and optimizer state, checked against
their hashes. 24, 16 and 0 training duels of about 245,700 per run were thrown out for reaching the decision limit.
No pilot training duel was.

### Stage B independent recount

A Claude Sonnet subagent at high effort recounted the run with its own code, without reading the runner, from the
protocol, the list file and the raw chunks. Its scripts, output and a summary are in
[`tests/edison/reports/m3-stage-b-2026-11-12/recount/`](../tests/edison/reports/m3-stage-b-2026-11-12/recount/).

- It reached `STAGE_B_MET` and the same four readings from its own numbers.
- Every gated number and reading matched to 1.3e−16. Its own bootstrap put interval ends within 0.035 points.
- All 333,144 cells were present once, with the protocol's seeds, and all 2,624 replays matched their first run.
- It derived who is counted in each arm from the rows and checked it on the mirror cells where a list meets itself.
- It matched the usage counts for activate, Normal Summon and attack in the held-out block.

It reran no game, and it did not recount guard fires, the breakdowns by archetype and card, or the per-build table.
The reviews before the freeze (two Claude Opus reviewers and Sol) are tabled in
[the protocol](edison-m3b-protocol.md#checks-before-freezing).

### Stage B search check, reported only

Run on November 16, 2026, after the verdict, as [the protocol](edison-m3b-protocol.md#search-check) set out. Its
design, the change that lets the search load code list v2, the checks before it and the review are in
[edison-m3b-search.md](edison-m3b-search.md), frozen in
[`m3b-search-manifest.json`](../tools/edison/heldout/m3b-search-manifest.json) at commit ea2fbfa before any game on its
seeds. Binary `1ab4aac9` (build id `d67bbcfcf8079dd0`), built on the certified r5 core.

M2's search at its frozen setting (64 rollouts, depth 48, 4 candidates, margin 1.0) pilots 60 drawn held-out builds,
each against 4 drawn training lists, on one seed from 90300000 in both seats. The opponent is the same retrained
network without search. Each search game is compared with the plain game on the same seed, decks and network. Ranges
resample held-out builds.

| | Comparisons | Builds | Search | Plain | Gain | 95% range |
| --- | ---: | ---: | --- | --- | --- | --- |
| All | 480 | 60 | 81.9% | 51.4% | +30.5 points | 25.8-35.3 |
| Search in seat 0 | 240 | 60 | 87.1% | 55.0% | +32.1 | 25.0-38.8 |
| Search in seat 1 | 240 | 60 | 76.7% | 47.7% | +29.0 | 22.7-35.4 |
| H, 6 or more unknown Main Deck copies | 112 | 14 | 75.9% | 50.0% | +25.9 | 14.3-37.5 |
| The other builds | 368 | 46 | 83.7% | 51.8% | +31.9 | 27.2-36.8 |

All 480 comparisons counted, with no missing or void game. One plain game was a draw and scores a half. For
comparison, the gain was +28.5 points (24.0-33.1) on Stage A's builds and +30.3 (27.8-32.6) on the training lists in
M2's final test. The gain on H is lower than on the other builds, but H holds only 14 of the drawn builds and the two
ranges overlap.

Search departed from the network's choice on 15.9% of 31,206 searched decisions, and fell back to it on 8.1%, against
6.1% in Stage A. No sampled world was voided in 1.80 million rollouts and the native network refused no observation.
It took 58 s of CPU per game.

This check keeps M2's limits: the search is told the opposing list, hidden cards are dealt uniformly rather than from a
belief, and every opponent is one of the same three networks. The table's scores were recomputed from the game
files with a separate script by the session that ran the check. No independent reviewer recounted it.

### What Stage B shows

On placed lists that hold cards no training list plays, the networks still beat the heuristic about three games in
four as pilot (75.6%), and their edge over it is within a point of the edge on each build's nearest training list. On
the builds with the most unknown cards the pilot rate is lower (73.3%) and the edge is 2.9 points short of the twin's,
inside the 10 points allowed. M2's search gains about as much on these builds as on the training lists. With Stage A,
this meets M3's exit.

It does not show:

- **that the unknown-card training was needed.** M1's networks, given only a mean row for unknown cards, score 75.4%
  on the same games. Retraining bought 1.3 points on the heavy set and cost 1.6 on the training lists;
- that the networks play an unknown card well. They choose options on unknown cards, but nothing here scores those
  choices, and an action's effect is still invisible to them;
- strength against people, since the opponent is the same weak heuristic;
- coverage of the format, since the builds are placed lists from one site's events;
- anything about a single build, card or matchup;
- strength with siding or in matches;
- that search copes with unknown cards against anything but the same network. The
  [search check](#stage-b-search-check-reported-only) gains 30.5 points over the bare network on these builds, with
  the opposing list given.

Stage A's 80.6% and the baseline's 75.4% here come from nearly the same M1 networks on different builds, and are
not a measured drop.

## Next

- Whether M4 builds on the retrained networks or on M1's is open. The retrained ones accept every legal card, are
  steadier across runs and are slightly better on the heaviest builds; M1's are slightly better on the training
  lists. Nothing here separates them by much.
- What would test unknown-card play properly: an opponent that uses the cards, and inputs that describe what an
  effect does.
