# M3 Stage A protocol: held-out builds

Written November 9, 2026, on branch `m3-corpus`. **No network or search has played any corpus list.** This file and
[`stage_a.py`](../tools/edison/heldout/stage_a.py), its exact form, are fixed and hashed into
[`m3a-manifest.json`](../tools/edison/heldout/m3a-manifest.json) before the first game. Simulator: certified module `b8fcd2bc`.

M3's exit is that the policy performs on decks held out of training, not only on the 21 lists it trained on. That splits
into two questions, answered by two stages.

- **Stage A, new builds of known cards.** Placed lists that no network trained on, built only from cards that some
  training list plays in its Main or Extra. Today's networks can play these with no change. This file covers Stage A.
- **Stage B, new cards.** Placed lists with a card no training list plays. The native env refuses any card outside code
  list v1, so this needs a new code list, a way to play a card whose identity was never trained, and retraining. Its
  plan is outlined below and will be fixed in its own file before its training starts.

M3's exit needs both. Stage A alone says nothing about cards the networks never trained on.

## Models

`checkpoint-2560` of optimizers 1702, 1703 and 1704, the M1 and M2 networks, with no selection. All three count. They
play greedily with the repetition guard, as in M1's gauntlet test. The opponent is the frozen rule-based heuristic
in `ygoai/edison_policy.py`.

## Held-out set

From the [corpus](edison-corpus.md) of 1,324 distinct placed lists, a list is **held out** if all of these hold:

- it passes production legality;
- it is not a training list;
- every Main and Extra card, after the Gottoms rule in `decks.py`, is in code list v1 and appears in the Main or Extra of
  some training list (v1's tokens and closure cards also count);
- its Main and Extra are not exactly a training list's;
- it did not crash in the soak's Main and Extra stage, which had no crash.

That gives **542 lists**. `stage_a.py select` recomputes every field from the deck files and stops if the corpus manifest
disagrees. The selection is in [`stage-a-lists.json`](../tools/edison/heldout/stage-a-lists.json).

| Excluded | Lists |
| --- | ---: |
| A Main or Extra card outside code list v1 | 704 |
| A Main or Extra card no training list plays (it is in v1 only through a training Side) | 50 |
| A training list | 18 |
| A training list's exact Main and Extra, with a different Side | 9 |
| Not production legal | 1 |

**Distance and twin.** A list's distance to a training list is the sum over passcodes of the difference in copies,
across Main and Extra. Swapping one card for another counts 2. Its **twin** is the training list at the smallest
distance, with ties going to the lowest sorted name. The **far set F** is lists at distance 20 or more, so at least
about 10 cards differ from every training list.

| Distance | Lists |
| --- | ---: |
| 1-9 | 222 |
| 10-19 | 216 |
| 20 or more (F) | 104 |

The set follows the corpus, so it is concentrated: Vayu Turbo 169, Blackwing 160, Hero Frog 83, then 18 other archetypes
with 1 to 18 lists each. Twins are mostly `g_vayu_turbo_ddev08` (147), `g_blackwing` (136) and `g_hero_frog` (85).

The env plays a copy of each list with Main and Extra after the Gottoms rule and no Side, since a single duel never reads
the Side. 27 lists lose XX-Saber Gottoms from the Extra.

## Matrix

Every list X meets every training list O, in both seats, in three arms that share each cell's decks and seeds:

| Arm | The network plays | The heuristic plays | Measures |
| --- | --- | --- | --- |
| Pilot | X | O | playing a list it never trained on |
| Face | O | X | facing a list it never trained on |
| Mirror | (the heuristic plays both) | X and O | how strong the list is, by the heuristic's play |

| Block | Lists X | Seeds | Games per cell |
| --- | --- | --- | ---: |
| Known | the 21 training lists | 88180000 + 32 × (21 i + j) | 32 |
| Held-out | the 542 held-out lists | 88130000 + 4 × (21 i + j) | 4 |

`i` is X's place in sorted order and `j` is O's. Both seats use the same seeds. Pilot and face run for each optimizer and
the mirror runs once. That is 165,522 cells and 834,960 games, about 5.5 hours on 15 workers. No seed overlaps the
corpus soak's, which `selftest` checks against the corpus summary.

Every 128th cell is replayed at 4 env threads, and any mismatch stops the run. Work is saved in chunks of about 9,400
games, so a paused run resumes where it stopped.

## Rules

All must hold. Rates are per optimizer, then averaged. Draws are not wins. The lower bound is the 95% Wilson bound over
that optimizer's games. "Both seats" pools the optimizers.

1. **A1, pilot.** On all 542 lists, and again on F: the mean is at least 55%, every optimizer's lower bound is above 50%,
   and each seat is at least 50%.
2. **A2, face.** The same bar for the face arm, on all lists and on F.
3. **A3, gap.** For each held-out list X with twin T, and each optimizer, the network's edge over the heuristic on X minus
   its edge on T:

   gap = (pilot on X − mirror on X) − (pilot on T − mirror on T), each against all 21 training lists in both seats.

   T's two rates come from the known block. The list's gap is the mean over optimizers. The mean gap over lists has a 95%
   bootstrap lower bound above −10 points, on all lists and on F. The bootstrap resamples lists, 10,000 times, seeded
   88810001. The mirror terms take out how strong each list is, as far as the heuristic can tell, so a weaker list
   doesn't count as a failure to generalize.
4. **A4, integrity.** Void games are reported by reason and don't count. Any block, arm and optimizer above 1% void fails.
   So does a missing chunk, a chunk whose jobs differ, a replay mismatch, or a held-out list with no game in some arm.

F is gated only if it holds at least 10 lists. It holds 104.

**Verdict:** `STAGE_A_MET` if every rule holds, otherwise `NOT_MET`. The seeds are consumed either way. A failure is not
retried on them.

**Reported, not gated:**

- each list's pilot, face and mirror rates and its gap;
- the gap without the mirror terms (pilot on X minus pilot on T);
- the gap by distance band, and pilot rate and gap by archetype;
- the pilot rate with each archetype weighted equally;
- the known block per training list, the mirror's seat-0 rate, draws, guard fires and void reasons.

One list's 168 games per optimizer don't support a claim about that list.

## Search check, reported only

Run after the verdict. M2's search at its frozen setting (64 rollouts, depth 48, 4 candidates, z 1.0, binary
`167be8aa`) plays a held-out list against the same network without search. `random.Random(88810002)` draws 60
held-out lists and then 4 training lists for each. Each pair plays one seed from 88800000 in both seats, the network
rotating by pair, and each search game is paired with the plain game on the same seed and decks. That's 480 search
games and 480 plain games.

The search binary loads exactly 21 lists, so the games run in 11 groups. Each group holds some held-out lists, the
training lists they meet, and other training lists to make 21. The groups are frozen in the manifest.

Reported: search and plain scores, their paired difference with a bootstrap interval over pairs, by seat, and on F.
This is not gated. M2's own result stands on the training lists.

## Stage B outline

To be fixed in its own protocol before Stage B training starts:

- **Code list v2.** The whole legal pool, all database tokens and the same closure as v1. Its first 509 lines are v1's,
  so v1 checkpoints can load with rows added.
- **Unknown identity.** One reserved row stands for a visible card whose identity the network doesn't know. A constant
  mask sends every card no training list plays to that row, including v1's Side-only cards. The native forward pass does
  the same. Training replaces known identities with it at a fixed rate, so the network learns to play from printed
  features.
- **Training.** The same trainer and budget as M1's round 2560 on the 21 lists, with three optimizers.
- **Held-out set.** Legal corpus lists not in training with at least one Main or Extra card that no training list plays,
  and no Main or Extra crash. The same arms and rules as Stage A, with the v2 networks' own known block.
- **Known crash.** Neo-Spacian Dark Panther is the lead suspect in the corpus soak's one crash, and one player activated it
  52 times in 60 decisions. It needs a rules check and a root cause before Stage B plays its 7 lists.

## What a pass would not claim

- Strength against people. The opponent is the same weak heuristic that M1 measured against.
- Coverage of the format. The corpus is placed lists from one site's events, and only lists built from trained cards
  enter Stage A.
- Anything about a single list or matchup.
- Strength with siding or in matches. These are single game-1 duels.

## Commands

```bash
P=/home/mada/ygo-agent/.venv/bin/python; S=tools/edison/heldout/stage_a.py
$P $S selftest
$P $S select
$P $S livecheck --workers 3
$P $S freeze
$P $S play --workers 15
$P $S analyze
$P $S search --workers 15
```
