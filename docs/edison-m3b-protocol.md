# M3 Stage B protocol: lists with cards no training list plays

Written November 12, 2026. **No network has played a corpus list on code list v2, and no Stage B network exists yet.**
This file and [`stage_b.py`](../tools/edison/heldout/stage_b.py), its exact form, are fixed and hashed into
[`m3b-manifest.json`](../tools/edison/heldout/m3b-manifest.json) before the dropout pilot and before any training.
Simulator: certified module `d1f1f701` (ygopro-core hardening r5).

[Stage A](edison-m3-protocol.md) asked whether the M1 networks play new builds of cards they trained on, and
[they do](edison-heldout.md). Stage B asks the other half of M3's exit: **do networks still perform on lists that hold
cards no training list plays?** M3's exit needs both.

**What the verdict means.** `STAGE_B_MET` says Stage A's bar holds on these lists: the networks beat the heuristic
with them, and their edge over it is about what it is on the nearest training list. It does not say the networks
use an unknown card well. A network that treated every unknown card as a blank could pass, and so could one no
better than M1's networks. Three readings and a usage table, fixed here and reported beside the verdict, speak to that.

## What a Stage B network is given

- **Code list v2** (`codes_v2.txt`, sha256 `071a484d`). The whole legal pool, all database tokens and the same closure
  as v1: 4,042 ids. Its first 509 lines are v1's, so a v1 card keeps its id.
- **The identity map** (`identity_v2.json`, sha256 `800fb1da`). Each id reads one row of the network's identity table.
  - A card is **known** if a training list plays it, or an artwork of it, in its Main or Extra Deck. All artworks read one row.
  - A token is known if a known card's script names it, or it has that token's name (the other Sheep and Fluff Tokens).
  - Every other id reads the one **unknown row**: Side-only cards, closure cards, other tokens and the rest of the pool.

  247 ids are known and read 225 rows. The map is applied inside the network call, so no caller can skip it.
- **Printed features only for an unknown card.** Attribute, Type, Level, ATK, DEF and type bits for a monster. A Spell or
  Trap shows only its kind. An action's effect field is an ordinal, so the network cannot tell what an unknown effect does.
- **Wider action tables.** The pool raises prompts the training lists never do: declaring a card name (Mind Crush) and
  declaring Life Points. Their values overflowed three v1 tables, so v2 tables hold every value the env can emit. The
  rows for values no training list raises stay as initialized. The soak met these prompts in 21 lists.
- **The native forward pass** reads the same map from the exported net file and has the same wider tables.

Stage A counted v1's closure cards and all tokens as trained. Stage B does not: Elemental HERO Bubbleman, which one
Stage A build played, is unknown here. XX-Saber Gottoms, which v1 could not hold, now plays and is unknown.

## Networks

- **Stage B networks.** The M1 trainer, settings and budget: 2,560 rounds of 96 duels on the 21 training lists,
  optimizers 1702, 1703 and 1704, on M1's training seeds (base 81000000), so the runs differ from M1's only in what
  the network is given. Training adds **identity dropout**: in some games, some known cards are hidden behind the
  unknown row for the whole game, for both seats, in card rows, offered actions and own history. The games and cards
  are a function of the game's seed. The setting comes from [the pilot](#the-dropout-pilot). `checkpoint-2560` of all
  three counts, with no selection.
- **Baseline networks.** M1's `checkpoint-2560` networks loaded into v2's table with no retraining. The unknown row is
  the mean of their known rows, and their new action-table rows are zero. They are not quite M1 as played before: both
  Foolish Burial artworks read one row, and tokens of one name read one row. An independent reviewer compared one
  network before and after on 2,756 training-list decisions: outputs were identical on the 2,392 with no merged card,
  and 20 greedy choices changed among the other 364.

All play greedily with the repetition guard, against the frozen rule-based heuristic in `ygoai/edison_policy.py`.

## Held-out set

From the [corpus](edison-corpus.md) of 1,324 distinct placed lists, a list is held out if:

- it passes production legality;
- it is not a training list;
- at least one Main or Extra card reads the unknown row;
- it had no void cell in [the soak](#checks-before-freezing).

A list plays as published, with no card removed and no Side. Lists with the same Main and Extra are one **build**,
played once under the lowest-numbered list. `stage_b.py select` writes
[`stage-b-lists.json`](../tools/edison/heldout/stage-b-lists.json).

| Excluded | Lists |
| --- | ---: |
| Every Main and Extra card is known (Stage A's question) | 525 |
| The same Main and Extra as a lower-numbered held-out list | 33 |
| A training list | 18 |
| Not production legal | 1 |

That leaves **747 builds** from 780 lists, in 95 archetypes. They hold 356 unknown cards, counting artworks as one.

The **heavy set H** is the 206 builds with 6 or more unknown copies in the Main Deck. Without it, a pass could rest on
builds that barely meet an unknown card. Extra Deck copies don't count toward H, because a player need never summon them.

| Unknown Main Deck copies | Builds |
| --- | ---: |
| None: unknown cards in the Extra Deck only | 66 |
| 1-2 | 319 |
| 3-5 | 156 |
| 6-10 | 128 |
| 11 or more | 78 |

In 319 builds the unknown cards are all monsters, in 144 all Spells and Traps, and in 284 both. H is led by Diva
Zombie (25 builds), Fairy (17) and Gladiator Beast (15). Twin and distance are Stage A's: the training list at the
smallest Main and Extra distance.

25 of the builds were Stage A builds too: 24 played there without XX-Saber Gottoms, and one holds Bubbleman. Their
Stage A results were known when these rules were written.

## Matrix

Every build X meets every training list O, in both seats. All arms of a cell share its decks and seeds.

| Arm | Blocks | Who plays X's side | Measures |
| --- | --- | --- | --- |
| Pilot | both | a Stage B network pilots X against the heuristic on O | playing the list |
| Face | both | a Stage B network pilots O against the heuristic on X | facing the list |
| Mirror | both | the heuristic plays both | the list's own strength |
| Base | both | a baseline network, as in pilot | what retraining bought |
| Mask 2, 5, 10, all | known | a Stage B network, as in pilot, with that many of X's identities hidden | what hiding a trained card costs |

| Block | Builds X | Seeds | Games per cell |
| --- | --- | --- | ---: |
| Known | the 21 training lists | 90200000 + 32 × (21 i + j) | 32 |
| Held-out | the 747 held-out builds | 90000000 + 4 × (21 i + j) | 4 |

`i` is X's place in sorted order and `j` is O's. Pilot, face, base and mask run for each optimizer and the mirror runs
once. That is 333,144 cells and 1,875,888 games.

**A mask arm** hides identities of the piloted training list for the whole game, wherever they appear. Each game
orders the list's identity rows from its seed; a dose of 2, 5 or 10 hides the first that many, and "all" hides every
one (21 to 47 rows). Every optimizer and seat of a cell hides the same rows. The comparison with the pilot arm is on
the same decks and seeds, so no difference in build strength enters.

**Running** is Stage A's: every 127th cell is replayed at 4 env threads and a mismatch stops the run; each start and
result is journaled; a result is never replaced; a stored chunk must equal its journal and its planned jobs. A worker
also reports the rows it was asked to hide, and a chunk whose report differs from the job fails its check. Results
sit in a directory named after the seal, so cells played on other checkpoints are never read as these.

**Provenance** has two steps, because the networks do not exist when the rules are fixed.

1. `freeze` runs on a clean commit and writes the manifest: this file, the runner, the trainer and interface code, both
   code lists, the identity map, the lists, the soak summary, the decks, the module, the database, the Lua scripts, the
   Python packages the env and heuristic load, the interpreter and library versions of the actors and the learner, the
   M1 checkpoints, the build directory, the job digests and the training commands. The manifest is committed alone.
   The pilot and training refuse to run anywhere else.
2. `seal` writes [`m3b-checkpoints.json`](../tools/edison/heldout/m3b-checkpoints.json): the pilot's result and pick,
   the hash of each round 2560 checkpoint, and the log of every pilot, training and seal attempt. It refuses a run
   whose settings, source files, module or library versions are not the manifest's. It is committed alone on the
   manifest. `play` and `analyze` refuse to run anywhere else. A job names its players by label, so the job digest is
   fixed in step 1 and the seal only binds labels to checkpoints.

## Rules

All must hold. Rates are per optimizer, then averaged. Draws are not wins. "Both seats" pools the optimizers.
**Intervals** are Stage A's two-stage bootstrap, 10,000 draws from seed 90810001: builds, then each drawn build's 42
cells, with each training list's 42 cells resampled once per draw. A draw with no game for a rate the gap needs counts
that build at the lowest gap. The 21 training lists and the three training runs are held fixed in every interval.

1. **B1, pilot.** On all 747 builds, and again on H: the mean is at least 55%, every optimizer's 2.5th percentile is
   above 50%, and each seat is at least 50%.
2. **B2, face.** The same bar for the face arm, on all builds and on H.
3. **B3, gap.** As Stage A's A3, on all builds and on H: the mean over builds of
   (pilot on X − mirror on X) − (pilot on twin − mirror on twin) has a 2.5th percentile above −10 points.
4. **B4, integrity.** As Stage A's A4: no arm above 1% void; every build has completed games in every arm, optimizer
   and seat of its block; counting void and interrupted games as losses, the pilot and face means stay at 55%; no
   missing or failed chunk and no replay difference.

H is gated only if it holds at least 10 builds. **Verdict:** `STAGE_B_MET` if every rule holds, otherwise `NOT_MET`.
The seeds are consumed either way.

**What these rules cannot see.** B1 and B2 measure against a weak heuristic, and Stage A scored 80.6% there. B3's
mirror terms rest on the same heuristic, which activates only cards on its own list, so it underuses a build's unknown
cards. A network that also ignores them shows a gap near zero. The rules therefore test that play does not fall apart
around unknown cards, not that the cards are played.

## Readings

Fixed here, reported beside the verdict, and not part of it.

- **Retraining**, on all builds and on H: `HELPED` if pilot minus base is at least 3 points and its 2.5th percentile
  is above 0, otherwise `NOT_SHOWN`. This is the one comparison that shows whether the unknown row and dropout matter
  on cards the networks never met.
- **Known cards**: `KEPT` if, on the known block, the Stage B networks' pilot rate is at most 5 points below the
  baseline's on the same cells, otherwise `LOST`. It has no interval for the spread between training runs, and M1's
  three runs spanned 5 points, so a `LOST` near the bar may be noise.
- **Hidden identities**: `SMALL_COST` if, on the known block, mask 5 minus pilot has a 2.5th percentile above
  −10 points, otherwise `LARGE_COST`. Five is the 75th percentile of distinct unknown cards per held-out build. This is
  an easy test. The hidden cards are training cards: dropout showed the network their printed features behind the
  unknown row, a hidden monster can often be told from them, and the pilot picks its setting on this very measure.
  A `LARGE_COST` says the mechanism failed. A `SMALL_COST` says little about a card never met. The interval resamples
  cells within the 21 lists.
- **Usage.** For the counted seat in each arm: among decisions that offered an option on an unknown card, how often
  one was chosen, by kind of action (activate, Summon, Set, attack), beside the same for known cards. A network that
  treats unknown cards as blanks shows here. The mirror arm gives the heuristic's rates.

**Also reported:** the dose curve with intervals and per training list; each build's rates and gaps; pilot, face,
base, mirror and gap by unknown Main Deck copies, by distance, by archetype, by whether the unknown cards are
monsters, Spells and Traps or both, and for each unknown card in 10 or more builds; the known block per training
list; the gap without mirror terms; draws, guard fires, void reasons, interrupted attempts and how many decisions
needed the wider tables.

One build's 168 games per optimizer don't support a claim about that build, and a cut by one unknown card mixes it
with the rest of each build.

## The dropout pilot

Run after the freeze, at the manifest's commit, on training lists only.

| Setting | Games left alone | Hiding rate in the others |
| --- | ---: | --- |
| S0 | all | none |
| S1 | 50% | uniform between 0 and 50% of known cards |
| S2 | 25% | uniform between 0 and 100% |

Each setting trains one network for 640 rounds with optimizer 1705, on the training seeds. Each network then pilots
every training list against the next four in a fixed cycle, both seats, 8 seeds per pair from 89300000, with 0, 5
and all of its list's identities hidden: 1,344 games per setting and dose.

- A dropout setting is **admissible** if, with nothing hidden, it is at most 4 points below S0.
- Among admissible settings, the higher mean of the dose 5 and dose "all" rates is picked. Within 2 points, S1 is.
- With no admissible setting, or more than 1% void in any setting and dose, there is no pick and no training under
  this protocol. That result is reported as it stands.
- A cell that was interrupted and run again counts, since a cell is a function of its seeds and networks. The number
  is reported.
- The pilot runs once. Its journal is named after the networks it played, a result is never overwritten, and every
  attempt is logged and carried into the seal.

One network per setting cannot separate small differences: M1's three optimizers spanned 5 points. The pilot is there
to catch a setting that breaks training and to choose between a mild and a strong one when they clearly differ.

## Search check

Reported only, after the verdict, as in Stage A: M2's search at its frozen setting, on the r5 binary, piloting 60 drawn
held-out builds against the same Stage B network without search. The search needs its own changes to load code list v2,
so its code is frozen in its own manifest before its first game. Its seeds are 90300000 onward.

## What a pass would not claim

- That the networks use an unknown card well, or better than M1's networks would. See the readings.
- Strength against people. The opponent is the same weak heuristic.
- Coverage of the format. The builds are placed lists from one site's events.
- Anything about a single build, card or matchup.
- Strength with siding or in matches. These are single game-1 duels with no Side.
- Anything about search. Search is checked separately and only reported.

## Checks before freezing

- **The identity differential** ([`identity_check.py`](../tools/edison/heldout/identity_check.py)) compares what reaches
  the network with a second computation of the map, written separately from the deck files, the database and the scripts.
  Its [summary](../tests/edison/reports/m3-stage-b-2026-11-12/identity/summary.json) holds every number below.
  - **Against the games, not the rule.** In 1,680 duels between the training lists, of random and rule-based play, none
    of 17.3 million visible identities read the unknown row, and all 225 known rows were seen. An earlier map failed
    this: it sent the second Fluff Token and three Sheep Tokens, whose ids scripts compute, to the unknown row.
  - On 18,371 decisions from lists with untrained cards, every identity in card rows, offered actions and own history
    was the expected row, and nothing else changed. The unknown row was read 113,468 times.
  - Six planted faults were each caught: no map, the map skipped at each of the three sites, a known card sent to the
    unknown row, and an unknown card left on its own row.
  - **On code list v1 the same check fails,** as it must: Side-only cards such as Nobleman of Extermination read rows
    that nothing trained, 5,713 times in 16,558 decisions.
  - `actor.py` itself, two ways. Rollouts with dropout and with forced hidden ids equal the plain rollouts read
    through each game's map, on 3,096 decisions. And on 3,977 decisions by a network whose choices depend on identity,
    the value and log-probability the actor stored are what the network gives on the stored batch, to 3e-6 and 2e-5;
    swapping the unknown row out moves the value on 3,789 of the 3,948 decisions that read it.
  - The native pass matches the Python network within the M2 gates on all 18,371 decisions, with no greedy
    difference. Skipping the map at any one site fails the gates. Ten wrong files or expectations refuse to load,
    among them a file whose only fault is another card's row under the right parameters and code list.
  - 27 unit checks of the map functions and the loaders pass.
- **Code list v1 is unchanged.** `test_learner.py` passes all 12 checks with the rollout and parameter hashes recorded
  on November 11.
- **The soak.** All 780 candidate lists through `actor.py`'s own audit on the certified module: 12,480 duels and
  2.6 million decisions of random and rule-based play, with no void cell.
  [Summary](../tools/edison/heldout/stage-b-soak.json). No network played.
- **`stage_b.py selftest`** plants each failure the rules, readings and result checks must catch, in made-up results
  from the real job builder.
- **`stage_b.py livecheck`** loads all 747 decks in every worker and plays training lists only.
- **A rehearsal** ran every step from `freeze` to `analyze` in a throwaway clone, with tiny settings and development seeds.

**Independent review.** Two Claude Opus reviewers read commits `f9f880a` and `9e8edd9`: one the interface code and the
differential, one this protocol and its runner. Neither found a defect that the planned runs would reach.

| Finding | Class | Change |
| --- | --- | --- |
| A pass did not need the networks to use an unknown card, and could be reached by networks no better than M1's | Design | The verdict's meaning is stated at the top; retraining, known cards and hidden identities are readings beside it; a usage table is added |
| The known-card rule could fail from the spread between three training runs | Design | It is a reading, and the text says a `LOST` near its bar may be noise |
| The hidden-identity rule tests training cards, on the measure the pilot picks by | Design | It is a reading, and the text says what a `SMALL_COST` does not show |
| 27 heavy builds were heavy only through Extra Deck copies | Design | H counts Main Deck copies: 206 builds |
| The map was applied by callers, and seven other tools would have skipped it on a v2 network | Latent | The map is applied inside the network call |
| The actor check used a network that ignores identity, so it could not see a network fed unmapped ids | Test | The stored value and log-probability are recomputed from the stored batch |
| The differential's "covered" rows came from the rule that built the map | Test | The training-list check above compares the map with the games |
| One interrupted pilot cell left the pilot with no pick for good | Reachable | An interrupted cell that was run again counts, and is reported |
| The pilot could be run again with no record, and its journal was not tied to its networks | Reachable | Attempts are logged and sealed; the journal is named after the networks; a result is never overwritten |
| `seal` checked little of a training run, and results were not tied to a seal | Test | It checks settings, source files, module and versions; results sit under the seal's hash |
| The selftest could not tell dose 5 from dose 10, or a lower bound from a mean | Test | Both are planted, with an empty mask cell, label binding and the worker job |
| Mask doses were drawn separately | Design | A larger dose hides all a smaller one does |
| `seal` named `checkpoint-3` for the trainer's `checkpoint-0003` (found by the rehearsal) | Reachable | Fixed; 2,560 rounds would have matched by luck |
| Numbers with no committed evidence; 357 unknown cards counted two artworks of one | Wording | The summaries are committed; artworks count once: 356 |

**Sol (gpt-6.1-sol)** then reviewed the interface code and the differential at `9764ca9`, after those fixes. It found no
defect the planned runs would reach. On its own games it confirmed that a cell hides the same identities and gives the
same traces and rollouts at 1 and at 4 env threads, that 670 stored decisions reproduce through training minibatches,
that the native pass matches on 865 fresh observations, and that code list v1's parameters and outputs equal those
before the change. Its findings:

| Finding | Class | Change |
| --- | --- | --- |
| A native caller could load a file with another identity map, since naming the map was optional | Latent | A file with an identity map loads only for a caller that names that map's hash |
| `identity_check.py inputs`, run alone, exited 0 with violations | Test | It exits 1, and a mutant or the v1 control must be asked for |
| The differential's v1 control never evaluates a v1 network | Test | No change: `test_learner.py` covers v1, and its rollout and parameter hashes match November 11's |

## Commands

```bash
P=/home/mada/ygo-agent/.venv/bin/python; S=tools/edison/heldout/stage_b.py
$P tools/edison/heldout/identity_check.py all --output <fresh dir>
$P $S soak --workers 14
$P $S select
$P $S selftest
$P $S livecheck --workers 3
$P $S freeze         # on a clean commit; then commit tools/edison/heldout/m3b-manifest.json alone
$P $S pilot-train    # three runs side by side, about 1.5 hours
$P $S pilot --workers 14
$P $S train          # three runs side by side, about 6 hours
$P $S seal           # then commit tools/edison/heldout/m3b-checkpoints.json alone
$P $S play --workers 15
$P $S analyze
```
