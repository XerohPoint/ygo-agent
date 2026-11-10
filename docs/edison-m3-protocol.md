# M3 Stage A protocol: held-out builds

Written November 9, 2026, on branch `m3-corpus`, and revised the same day after Sol's review. **No network or search has played any corpus list.** This file and
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

542 lists pass. A single duel plays only Main and Extra, so lists that differ only in their Side are the same **build**.
The test plays each of the **488 distinct builds** once, under the lowest-numbered list that has it, and records the
others as variants. `stage_a.py select` recomputes code list coverage, untrained cards with their copies, the twin and
the distance from the deck files, and stops if the corpus manifest disagrees. Legality comes from the corpus's
production check and is checked again when every worker loads every deck through production `init_module`. The
selection is in [`stage-a-lists.json`](../tools/edison/heldout/stage-a-lists.json).

| Excluded | Lists |
| --- | ---: |
| A Main or Extra card outside code list v1 | 704 |
| A Main or Extra card no training list plays (it is in v1 only through a training Side) | 50 |
| A training list | 18 |
| A training list's exact Main and Extra, with a different Side | 9 |
| Not production legal | 1 |
| The same Main and Extra as a lower-numbered held-out list | 54 |

**Distance and twin.** A build's distance to a training list is the sum over passcodes of the difference in copies,
across Main and Extra. Swapping one card for another counts 2. Its **twin** is the training list at the smallest
distance, with ties going to the lowest sorted name. The **far set F** is builds at distance 20 or more, so at least
about 10 cards differ from every training list.

| Distance | Builds |
| --- | ---: |
| 1-9 | 182 |
| 10-19 | 204 |
| 20 or more (F) | 102 |

The set follows the corpus, so it is concentrated: Blackwing 148, Vayu Turbo 143, Hero Frog 80, then 18 other
archetypes with 1 to 16 builds each. Twins are mostly `g_blackwing` (125), `g_vayu_turbo_ddev08` (119) and
`g_hero_frog` (82). A pass is average performance on this subset of the corpus, not on the format.

The env plays a copy of each build with Main and Extra after the Gottoms rule and no Side. 27 of the 542 lists lose
XX-Saber Gottoms from the Extra.

## Matrix

Every build X meets every training list O, in both seats, in three arms that share each cell's decks and seeds:

| Arm | The network plays | The heuristic plays | Measures |
| --- | --- | --- | --- |
| Pilot | X | O | playing a build it never trained on |
| Face | O | X | facing a build it never trained on |
| Mirror | (the heuristic plays both) | X and O | how strong the build is, by the heuristic's play |

| Block | Builds X | Seeds | Games per cell |
| --- | --- | --- | ---: |
| Known | the 21 training lists | 88180000 + 32 × (21 i + j) | 32 |
| Held-out | the 488 held-out builds | 88130000 + 4 × (21 i + j) | 4 |

`i` is X's place in sorted order and `j` is O's. Both seats use the same seeds. Pilot and face run for each optimizer and
the mirror runs once. That is 149,646 cells and 771,456 games, about 5 hours on 15 workers. No seed overlaps the
corpus soak's, which `selftest` checks against the corpus summary.

**Running.** Every 127th cell is replayed at 4 env threads. That covers both blocks, every arm and both counted seats, and
any mismatch stops the run. Each cell's start and result are journaled as they happen, so a pause or crash of the runner
loses nothing finished. A result is never replaced, so a failure stays recorded. A cell or replay that was running when the runner
stopped is run again, and its cell's row records the interrupted attempt. Before a resume appends, a write cut short by
the interruption is trimmed back to the last whole record. Every stored chunk is checked against its planned jobs (each
row's job, decks, seats, policies and seeds, every duel's seed and winner, the exact set of replays, and the recorded
interruptions) and must equal what its journal holds.

**Provenance.** `freeze` runs on a clean commit and records it. The manifest is committed alone on top of that commit.
`play`, `analyze` and `search` refuse to run unless HEAD is that manifest commit and every input still matches it: this
file, the runner and its other code, the lists, the corpus manifest and summary, the decks, the checkpoints, the native
module, the database and the Lua scripts. `freeze` refuses once results exist.

## Rules

All must hold. Rates are per optimizer, then averaged. Draws are not wins. "Both seats" pools the optimizers.

**Intervals** resample in two stages, 10,000 times from seed 88810001: builds with replacement, then each drawn build's
42 cells, one per opponent and seat, with replacement. Each training list's 42 cells are also resampled once per draw,
and every build with that twin shares the draw. All arms of a cell share its seeds, so they move together. This keeps
the clustering by build and by matchup, and carries the known block's own noise into the gap. If a draw leaves a drawn
build, or its twin, with no game for a rate the gap needs, that build counts at the lowest possible gap, −2, in that
draw. It is never dropped. The number of such draws is reported.

1. **A1, pilot.** On all 488 builds, and again on F: the mean is at least 55%, every optimizer's 2.5th percentile is above
   50%, and each seat is at least 50%.
2. **A2, face.** The same bar for the face arm, on all builds and on F.
3. **A3, gap.** For each held-out build X with twin T, and each optimizer, the network's edge over the heuristic on X minus
   its edge on T:

   gap = (pilot on X − mirror on X) − (pilot on T − mirror on T), each against all 21 training lists in both seats.

   T's two rates come from the known block. The build's gap is the mean over optimizers. The mean gap over builds has a
   2.5th percentile above −10 points, on all builds and on F. The mirror terms take out how strong each build is, as far
   as the heuristic can tell, so a weaker build doesn't count as a failure to generalize. That correction depends on a
   weak policy: a build the heuristic plays badly can offset a real drop, so the gap without mirror terms is reported
   beside it, and A1 keeps an absolute floor.
4. **A4, integrity.** Void games are reported by reason and don't count. It fails if:
   - any block, arm and optimizer is above 1% void;
   - any build, arm, optimizer and seat has no completed game;
   - counting every void game, and every game in a cell with an interrupted first run or replay, as a loss for the counted
     side, the pilot or face mean falls below 55% on all builds or on F;
   - a chunk is missing or fails its checks, or a replay differs.

F is gated only if it holds at least 10 builds. It holds 102.

**Verdict:** `STAGE_A_MET` if every rule holds, otherwise `NOT_MET`. The seeds are consumed either way. A failure is not
retried on them.

**Reported, not gated:**

- each build's pilot, face and mirror rates, its gap and its gap without mirror terms, and the spread of both;
- the gap and pilot rate by distance band and by archetype;
- the pilot rate with each archetype weighted equally;
- the known block per training list, the mirror's seat-0 rate, draws, guard fires, void reasons and interrupted attempts.

One build's 168 games per optimizer don't support a claim about that build.

## Search check, reported only

Run after the verdict, which `search` checks. M2's search at its frozen setting (64 rollouts, depth 48, 4 candidates,
z 1.0, binary `167be8aa`) plays a held-out build against the same network without search. `random.Random(88810002)`
draws 60 held-out builds and then 4 training lists for each. Each pair plays one seed from 88800000 in both seats, the
network rotating by pair, and each search game is paired with the plain game on the same seed, decks and network.
That's 480 search games and 480 plain games.

The search binary loads exactly 21 lists, so the games run in 11 groups. Each group holds some held-out builds, the
training lists they meet, and other training lists to make 21. The groups are frozen in the manifest. The groups are
only a loading workaround; the search is still told the opposing list.

Reported: missing and void games in each arm with reasons; then, on the pairs where both games finished, search and
plain scores and their difference, for all pairs, by seat and on F, each with the number of comparisons and builds and
an interval that resamples held-out builds. An empty subset is reported as such. This is not gated, and it keeps M2's
limits: the opposing list is given, hidden cards are dealt uniformly, and every opponent is one of the same three networks.

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

## Review before freezing

Sol reviewed the corpus and the first draft of this protocol and runner at commit `c3cb65c`, without playing any corpus
list with a network or search. Its findings and what changed:

| Finding | Class | Change |
| --- | --- | --- |
| A build could lose every face game and still pass A4 | Reachable | A4 needs completed games for every build, arm, optimizer and seat |
| An interrupted chunk lost its first results, so a rerun could hide a failure | Reachable | Each start and result is journaled; a result is never replaced |
| A stored chunk was trusted by its header | Test | Every row, duel seed, winner and replay is checked against the planned jobs |
| The freeze left out this file, the corpus summary and the search code, and wasn't tied to a commit | Test | All are hashed; the manifest must sit alone on the commit it names, as in M2 |
| The corpus soak's Side-in did not play whole Sides | Reachable, corpus | The corpus document now says so |
| The search report hid exclusions and mixed populations | Reachable, reported only | One population per summary, with counts, reasons and intervals |
| The search pairing self-test could not fail | Test | It compares search and plain games directly, and a changed net fails it |
| The privacy check passed with a leaked name or no raw cache | Test | It fails on either, and on any person field in committed JSON |

Design changes from the same review: one entry per distinct build, not per list; intervals that resample builds and
matchups, with shared twins resampled too, in place of Wilson bounds over games; a worst-case void rule; and replays on
both counted seats. The search check now runs only after the verdict, and the selection check covers card copies.

Sol's second pass, at `50a784d`, confirmed those fixes and found three more, all fixed:

| Finding | Class | Change |
| --- | --- | --- |
| A resume after a cut journal write appended to the fragment, so the next read failed | Reachable | The cut tail is trimmed, or a whole last record gets its newline, before appending |
| An interrupted replay didn't count its cell in the worst case | Reachable | A cell's interruptions include its replay's, and the chunk records and checks both |
| A draw with no game for a build's rate dropped the build from that draw's gap | Reachable | It counts at the lowest possible gap, as above |

It also noted that a stored chunk could drop its interruption counts, and that an empty far set crashed the rules. A
chunk must now equal what its journal holds, and an empty set is handled.

## Commands

```bash
P=/home/mada/ygo-agent/.venv/bin/python; S=tools/edison/heldout/stage_a.py
$P $S selftest
$P $S select
$P $S livecheck --workers 3
$P $S freeze        # on a clean commit; then commit tools/edison/heldout/m3a-manifest.json alone
$P $S play --workers 15
$P $S analyze
$P $S search --workers 15
```
