# Edison held-out decks

M3's study report: how the M1 networks play decks they never trained on. The rules were fixed before any game in
[the M3 Stage A protocol](edison-m3-protocol.md), and the list selection in
[`stage-a-lists.json`](../tools/edison/heldout/stage-a-lists.json). The run, analysis and search check all ran at commit
86ddb40, which holds only the manifest [`m3a-manifest.json`](../tools/edison/heldout/m3a-manifest.json) on top of commit
bf2f736. The result files are in [`tests/edison/reports/m3-stage-a-2026-11-09/`](../tests/edison/reports/m3-stage-a-2026-11-09/).

## Stage A: new builds of known cards

**Verdict: `STAGE_A_MET`**, November 10, 2026. Every rule held, on all 488 held-out builds and on the far set F
(the 102 builds at distance 20 or more from every training list, so about 10 or more cards differ).

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

**The run.** 149,646 cells and 771,456 games took 3.8 hours on 15 workers. There were no void games, draws or
interrupted cells. All 1,179 replays, run at 4 env threads, matched their first run. The networks made 126.4 million
decisions, and the repetition guard fired on 0.52% of them.

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

**Against the training lists.** In the known block, each training list met the others with 32 seeds per cell. There the
networks scored 78.8% as pilot, 78.7% as face, and the heuristic's mirror rate averaged 50.3%. On the held-out builds,
the pilot rate is about the same (80.6%) but the face rate is lower (71.5%). The held-out builds are also stronger in
the heuristic's hands: their mirror rate averages 58.1%, and the mirror's seat 0 wins 59.7% against 52.1% in the known
block. Most of this is Blackwing, which is 30% of the held-out builds and 2 of the 21 training lists. The heuristic
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

Search left the network's choice on 16.2% of 30,468 searched decisions, and fell back on 6.1% because the first 8 deals
were all refused. It took 65 s of CPU per game. 24 sampled worlds were voided in 1.79 million rollouts, all in one game
(h_fl39260 against `g_lightsworn_fairy`, seed 88800031). The first diagnostic was "Deck card in unsupported prompt:
select_card". That game finished normally. M2 had no voided world in 4.0 million rollouts.

This check keeps M2's limits: the search is told the opposing list, hidden cards are dealt uniformly rather than from a
belief, and every opponent is one of the same three networks.

## What Stage A shows

On new builds made from cards the networks trained on, the networks play about as well as on their 21 training lists.
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
- strength with siding or in matches, since these are single game-1 duels.

## Next: Stage B

Stage B tests lists with cards no training list plays. Its own protocol is fixed before its training starts. It needs
code list v2, a reserved unknown-identity row with a mask and identity dropout in training, the same in the native forward
pass, retraining on the 21 lists, and a root cause for the corpus soak's one crash (the lead suspect is Neo-Spacian Dark
Panther) before its 7 lists play. The outline is in [the Stage A protocol](edison-m3-protocol.md#stage-b-outline).
