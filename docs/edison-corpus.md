# Edison M3 deck corpus

Prepared November 9, 2026, on branch `m3-corpus` from `edison` at `6752ce9`. This is the pool that M3's held-out split will be drawn from.
The split and the test are not part of this work. **No network, learner, search or strength evaluation ran on any corpus list, and no win rate was computed.**

**Result: 1,324 distinct lists from 1,384 published placements at 175 events. 1,323 pass production legality. 619 fit code list v1 in Main and Extra,
and 601 of those are not training lists. The crash soak played 254,016 duels of the legal lists: 1 crashed and none printed a diagnostic.** The crash is
a segmentation fault in the production module, reproducible only some of the time, and found so far only in one pairing of a Hero Frog list with a
Neo-Spacian list. Neo-Spacian Dark Panther is the lead card, not a confirmed cause. It is in 7 lists, none inside v1.

Evidence is in [reports/corpus-2026-11-09](../tests/edison/reports/corpus-2026-11-09/summary.json). Lists are in
[assets/edison/corpus](../assets/edison/corpus/manifest.json). Tools are in [tools/edison/corpus](../tools/edison/corpus/).

## What was fetched

Only `https://formatlibrary.com/api/` was read: `events/gallery/edison`, `events/<abbr>` and `decks/<id>`. Requests were at least 2 seconds apart,
with User-Agent `edison-research-client`. Every response is cached under `build/edison-corpus/raw/`, which is untracked, so nothing was fetched twice.

| Item | Count |
| --- | ---: |
| Events in the gallery | 176 |
| Events excluded | 1 |
| Events used, each with published top decks | 175 |
| Top-deck placements listed | 1,385 |
| Placements whose deck could not be fetched | 1 |
| Placements in the corpus | 1,384 |
| Distinct lists | 1,324 |
| Lists that appear more than once | 46, covering 60 extra placements |

- 91 responses came from the pool audit's cache of November 4: the gallery, 15 events and 75 decks, on the same routes. The other 1,469 were fetched on
  November 10 UTC. `seeded.json` and `fetch_log.jsonl` in the raw cache record which is which.
- Events publish from 1 to 24 top decks, most often 8 or 16. These are placed lists, not whole fields.
- Each deck's `ydk` field agrees with its card arrays by card name in all 1,384. The arrays use artwork ids, such as 81439174 for Foolish Burial,
  where the `ydk` uses the passcode 81439173. The `ydk` is what the corpus uses.

### Exclusions

| Level | Item | Reason |
| --- | --- | --- |
| Event | UTWT23, Ultimate Time Wizard Tournament 2023, 271 players | Konami Time Wizard event: modern mechanics, not traditional Edison. Not fetched |
| Deck | 67937, DDEV09 | HTTP 500 on two attempts |

No other event had an unclear ruleset by the rule I used. That rule reads only the gallery: the community, the event name and the format name. The API has no
ruleset field, and I did not read any event's own rules.

**SJC 2010 is kept, with its own label.** SJCED10 is a Konami event, but it is the April 2010 tournament itself, played under that day's rules. It is not a
Time Wizard event. Its 16 lists carry the ruleset `2010 official event (Konami SJC, April 2010), rules as judged then`. The other 1,368 placements carry
`Community traditional Edison`. One training list, quickdraw_plant, comes from SJCED10. If you want community events only, drop rows whose appearances
have that ruleset.

## Files

- `assets/edison/corpus/manifest.json`: one row per distinct list, all appearances, exclusions, inputs and field definitions.
- `assets/edison/corpus/ydk/fl<deck id>.ydk`: one file per distinct list. The id is the lowest training deck id among its appearances, else the lowest deck id.
- **Card lines are unedited and in source order**, leading zeros included, trimmed only of surrounding spaces. A check confirms every file's card and
  section lines equal the source's. Only the header changed. The source's first line reads `created by ...` and can hold a name, so it is replaced by
  provenance lines: source, event, placement, archetype, ruleset. The gauntlet's `build_gauntlet.py` wrote cards as integers, so its files drop leading
  zeros. The cards are the same.
- **No player name, handle, Discord id or user id is committed.** `privacy_check.py` collected 2,152 such strings from the raw responses and searched all
  1,337 committed files. The only matches are four that are also card, archetype, community or event words: "Jinzo" (Jinzo Machina), "Rose" and "Soul"
  (card names), and "beastmode" (a community and its Beastmode Circuit Series).

Each row has the archetype label as the source gives it (`deckTypeName`) and a normalized label. The normalized label is the source label casefolded,
with `&` as `and`, punctuation dropped and words joined by `_`. Nothing is merged by meaning, so `diva_hero` and `diva_hero_beat` stay apart. One list
carries two labels across its appearances, Lightsworn and Twilight. A list takes the label of the appearance its id names, and the manifest keeps all labels.

## Counts by archetype

98 labels. "Inside v1" means production legal and every Main and Extra card in code list v1 after the Gottoms rule. "Held-out" means not a training list.

| Archetype | Lists | Legal | Inside v1 | In training | Held-out, inside v1 | Of those, no untrained card |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| blackwing | 255 | 255 | 177 | 1 | 176 | 160 |
| vayu_turbo | 240 | 240 | 193 | 2 | 191 | 178 |
| hero_frog | 106 | 106 | 93 | 1 | 92 | 83 |
| gladiator_beast | 78 | 78 | 19 | 1 | 18 | 18 |
| lightsworn | 50 | 50 | 11 | 1 | 10 | 7 |
| frog_monarch | 49 | 49 | 14 | 1 | 13 | 10 |
| dragon_turbo | 44 | 44 | 4 | 1 | 3 | 2 |
| diva_hero | 41 | 40 | 17 | 1 | 16 | 16 |
| diva_zombie | 38 | 38 | 0 | 0 | 0 | 0 |
| hero_beat | 35 | 35 | 6 | 1 | 5 | 4 |
| diva_hero_beat | 29 | 29 | 12 | 1 | 11 | 11 |
| frog_otk | 29 | 29 | 17 | 1 | 16 | 16 |
| machina_control | 29 | 29 | 15 | 1 | 14 | 12 |
| quickdraw_plant | 27 | 27 | 6 | 1 | 5 | 5 |
| gigavise | 21 | 21 | 16 | 1 | 15 | 15 |
| fairy | 17 | 17 | 0 | 0 | 0 | 0 |
| diva_frog | 13 | 13 | 5 | 1 | 4 | 3 |
| diva_hero_zombie | 13 | 13 | 0 | 0 | 0 | 0 |
| lightsworn_fairy | 13 | 13 | 1 | 1 | 0 | 0 |
| dragon_beat | 12 | 12 | 0 | 0 | 0 | 0 |
| zombie_teledad | 12 | 12 | 1 | 1 | 0 | 0 |
| machina_gadget | 10 | 10 | 1 | 0 | 1 | 1 |
| cat_control | 9 | 9 | 0 | 0 | 0 | 0 |
| jinzo_machina | 6 | 6 | 2 | 0 | 2 | 2 |
| vayu_control | 4 | 4 | 3 | 0 | 3 | 3 |
| twilight | 4 | 4 | 2 | 0 | 2 | 1 |
| frog_control | 3 | 3 | 3 | 0 | 3 | 3 |
| quickdraw_frog | 1 | 1 | 1 | 0 | 1 | 1 |
| 70 other labels | 136 | 136 | 0 | 0 | 0 | 0 |
| **All** | **1,324** | **1,323** | **619** | **18** | **601** | **551** |

Counts are of distinct lists, each labelled by the appearance its id names. The table includes every label with 9 or more lists or any held-out list inside v1.

**Held-out candidates that today's networks can play are concentrated.** Vayu Turbo, Blackwing and Hero Frog hold 459 of the 601. Seven archetypes with
9 or more lists have no held-out list inside v1: Diva Zombie, Fairy, Diva Hero Zombie, Dragon Beat and Cat Control have none inside v1 at all, and
Lightsworn Fairy and Zombie TeleDAD have only their training list. Code list v1 was built from 21 lists, so this is expected.

## Counts by event date

| Year | Placements | Distinct lists, by first appearance | Held-out, inside v1 |
| --- | ---: | ---: | ---: |
| 2010 | 16 | 16 | 1 |
| 2020 | 9 | 9 | 2 |
| 2021 | 28 | 28 | 3 |
| 2022 | 188 | 184 | 49 |
| 2023 | 320 | 310 | 125 |
| 2024 | 286 | 271 | 142 |
| 2025 | 321 | 300 | 177 |
| 2026 | 216 | 206 | 102 |

## Legality

Each file ran alone through the unmodified production `init_module`, with the child program `validate_gauntlet.py` uses, the certified module and the
audit's full code list. The full code list was regenerated in this worktree by `build_codelist.py`: 4,035 lines, sha256 `4d52f62e`. The main checkout's copy
is from before hardening and lacks the 13 added cards.

**1,323 of 1,324 lists are legal.** The one failure is fl27828, Diva Hero from EWCQ04, with two Reinforcement of the Army against a limit of 1. Its file stays
unedited. No list has a card outside the legal pool.

The path rejects what it should. Five planted variants of a real list failed with the expected message: a Forbidden card, 39 Main Deck cards, four copies,
an Extra Deck monster in the Main Deck, and a card outside the pool (Tour Guide From the Underworld).

## Code list v1 coverage

The env stores only Main and Extra. A Main card outside the code list crashes at reset in `card_reader_callback`, as the negative control below shows. An Extra
card outside it fails `init_module` in `sort_extra_deck` (`[c_get_card] Card not found`). `preload_deck` in `ygopro.h` has no caller.

| Measure | Lists |
| --- | ---: |
| Inside v1, Main and Extra | 619 |
| Of those, inside only after the Gottoms rule removes XX-Saber Gottoms | 31 |
| Inside v1 in every zone, nothing removed | 380 |
| Inside v1 in Main, Extra and Side, after the Gottoms rule | 395 |
| Held-out and inside v1 | 601 |

Most common cards outside v1, by number of lists:

| Card | Lists, any zone | Lists, Main or Extra |
| --- | ---: | ---: |
| Mind Crush | 170 | 10 |
| Skill Drain | 132 | 25 |
| Spirit Reaper | 129 | 127 |
| Cold Wave | 125 | 92 |
| Revived King Ha Des | 80 | 80 |
| XX-Saber Gottoms | 76 | 76, removed by the Gottoms rule |
| Zombie Master | 74 | 74 |
| Pyramid Turtle | 69 | 69 |
| Doomkaiser Dragon | 68 | 68 |
| Karma Cut | 63 | 11 |
| Seven Tools of the Bandit | 57 | 23 |
| Mystic Tomato | 56 | 56 |
| Arcanite Magician | 55 | 55 |
| Dark Bribe | 50 | 36 |
| Avenging Knight Parshath | 49 | 49 |

Some legal lists miss v1 by a single Main or Extra card. The most common such cards are Spirit Reaper (38 lists), Jar of Greed (29), Seven Tools of the
Bandit (21), Cold Wave (20) and Ojama Knight (17). Any change to the code list needs new checkpoints, so this is only a guide for a later code list.

## Untrained cards and nearest training list

Training lists are the 21 that `decks.py` `registry()` builds: 3 fixtures and 18 gauntlet lists, with the Gottoms rule. Both fields below use the corpus
list after the same rule.

- `untrained_main_extra`: Main and Extra cards that appear in no training list's Main or Extra. Code list v1's tokens and closure cards count as trained.
  A v1 card seen only in a training Side counts as untrained.
- `nearest_training`: the training list with the fewest Main and Extra card differences, counted with multiplicity, and that distance. Ties go to the lowest
  sorted name.

**In training:** 18 rows, one per gauntlet deck id. No corpus list is identical to a fixture. The training list vayu_turbo_ddev08 (60919) was also
placed under 4 other deck ids with the same Main, Extra and Side, at BEAK09, DDEV08, IMNCS01 and GOPPO04. Those are appearances of its row.

| Lists inside v1 with no untrained Main or Extra card | Distance 0 | 1-9 | 10+ | All |
| --- | ---: | ---: | ---: | ---: |
| All | 27 | 222 | 320 | 569 |
| Not in training | 9 | 222 | 320 | 551 |

The 9 held-out lists at distance 0 are Vayu Turbo lists with a training list's exact Main and Extra and a different Side: 8 match vayu_turbo_ddev08 and
1 matches vayu_turbo. They test siding at most. Among held-out lists the median distance is 18 and the largest is 87.

By archetype, lists inside v1 with no untrained card, by distance:

| Archetype | 0 | 1-9 | 10+ |
| --- | ---: | ---: | ---: |
| vayu_turbo | 11 | 121 | 48 |
| blackwing | 1 | 62 | 98 |
| hero_frog | 1 | 0 | 83 |
| gladiator_beast | 1 | 13 | 5 |
| diva_hero | 1 | 0 | 16 |
| frog_otk | 1 | 12 | 4 |
| gigavise | 1 | 3 | 12 |
| machina_control | 1 | 0 | 12 |
| diva_hero_beat | 1 | 2 | 9 |
| frog_monarch | 1 | 1 | 9 |
| lightsworn | 1 | 4 | 3 |
| quickdraw_plant | 1 | 0 | 5 |
| hero_beat | 1 | 0 | 4 |
| diva_frog | 1 | 0 | 3 |
| dragon_turbo | 1 | 2 | 0 |
| vayu_control | 0 | 2 | 1 |
| frog_control | 0 | 0 | 3 |
| jinzo_machina | 0 | 0 | 2 |
| lightsworn_fairy, zombie_teledad | 1 each | 0 | 0 |
| machina_gadget, quickdraw_frog, twilight | 0 | 0 | 1 each |

Of the 601 held-out lists inside v1, 551 have no untrained card, 49 have one and 1 has two. The commonest untrained cards there are My Body as a Shield
(15 lists), Blackwing - Elphin the Raven (13), Dark Dust Spirit (8), Nobleman of Extermination (5) and Snipe Hunter (4). Each sits only in a training Side.

## Crash soak

### Method

- **Module:** the certified production module, copied from the main checkout and checked against sha256 `b8fcd2bc...`. It was not rebuilt.
- **Lists:** all 1,323 legal lists, sorted by id. Each plays the next 16 in that cycle, 6 duels per pair, so each list is in 192 duels per stage.
- **Play:** each pair's 6 duels cover both seat orders with three policies: uniform random, the audit's eager rule (80% prefer a Summon or
  activation), and the frozen rule-based Heuristic from `ygoai/edison_policy.py`, which has no weights. No network played.
- **Stages:** Main and Extra, then Side-in. Side-in moves the whole Side in, as the audit did. A Side card whose zone is empty stays in the Side;
  one list has no Extra Deck.
- **Checks:** the audit worker's observation contract, including the candidate-row and finish and cancel checks that hardening added. Every decision
  is flushed to disk, so an abort leaves the last card chosen. Each stderr line is tied to the duel that printed it.
- **Workers:** 12, with `OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1`.

**Why not the audit module.** `tools/edison/pool_audit/soak.py` defaults to a separate module that `build_audit_module.py` compiles from this checkout's
`ygopro.h`, with a chain and Summon logger and its own message handler. It links ygopro-core variant `bedb42dd` (early trigger only), while production
links `5fe29f6a` (`edison_hardening = "r2"`). So it would play without the hardening rule fixes, and its handler would replace the production one
that prints diagnostics. I did not build it. The cost is that this soak has no chain log, so card coverage below counts choices, not activations seen
by the core.

### Results

| Stage | Seeds | Duels | Decisions | Crashes | Diagnostics | Contract violations | Other non-ok | Wall time |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Main and Extra | 88000000-88127007 | 127,008 | 25,986,082 | 0 | 0 | 0 | 0 | 31 min |
| Whole Side moved in | 88200000-88327007 | 127,008 | 28,039,373 | 1 | 0 | 0 | 0 | 35 min |

The largest offered set was 75 options, and the most a state repeated in one duel was 136 times. No duel was truncated or timed out.

**Card coverage.** Of 583 distinct Main and Extra cards in legal lists, 557 were chosen in an action row (Summon, Set, activate, attack or change
position) and 500 were chosen to activate. With Sides in, 626 of 653 were chosen and 556 activated. 20 of the 26 never chosen in the first stage are
Fusion Monsters, which a Fusion Spell summons through a selection prompt this count does not see. The lists are in `summary.json`.

### Crash classes by card

| Class | Duels | Where | Cards |
| --- | ---: | --- | --- |
| Segmentation fault in the production module | 1 of 254,016 | Side-in, seed 88264285, fl41606 (Hero Frog) against fl41081 (Neo-Spacian Diva Hero), uniform play | Lead: Neo-Spacian Dark Panther. Not confirmed |

No other class appeared: no C++ exception, no Lua or core diagnostic, no unhandled prompt.

**The recorded signature is wrong.** `crashes.json` records this crash as `exit44330098` with a Gym warning as its text. A loop variable in `soak.py`
overwrote the exit status, and the signature fell back to a benign line. Both are fixed now; later probes record `exit-11`. The kernel log gives the
true cause: SIGSEGV in the module at file offset `0x327f5b`, at the minute the batch log stopped.

**What the fault is.** The module is stripped, so I read the machine code. The faulting instruction follows a list's next pointer while freeing its
objects, and the surrounding code matches Lua's `luaC_freeallobjects` during `lua_close`. Production closes a duel's Lua state in `_duel_end` when
the duel is won. This crash came on the duel's last decision, a lethal attack. So memory was damaged earlier in the duel and found at cleanup.

**It depends on the game path, and shows only some of the time.**

- The duel replays with an identical decision trace every time.
- Run alone, one at a time, it crashed in 2 of 15 runs. With 12 runs in parallel it crashed in 7 of 96 alone and 6 of 96 inside its batch.
- The rest of its batch, without it, crashed in 0 of 192 runs.
- One crashing replay died at decision 124 in libc instead, just after Neo-Spacian Dark Panther chose Treeborn Frog as its target.

**Why Neo-Spacian Dark Panther.**

- The same two lists, on fresh seeds, crashed once more in 6,000 Main and Extra duels, at the same offset, and 0 times in 6,000 Side-in duels.
- All 6 crash duels used Dark Panther's copy effect, 2 to 74 times.
- Played 24,000 times each, the original pair crashed 4 times, all SIGSEGV in the module: 3 at the same offset and 1 at
  `0x32569e`. The same pair with fl41081's 3 Dark Panther replaced by Gene-Warped Warwolf crashed 0
  times. If the rates were equal, a 4 to 0 split would happen 1 time in 16, so this is a lead and not proof.
- Generated decks built around Dark Panther, Treeborn Frog, Elemental HERO Ocean, Armory Arm and Future Fusion crashed 0 times in 3,000 duels each.
  So Dark Panther alone does not do it.

In one crash a player activated Dark Panther 52 times in 60 consecutive decisions. A once-per-turn effect should not allow that unless each
activation gains a fresh copy of the effect. That deserves a rules check.

**Exposure.** Dark Panther is in 7 corpus lists: fl2515, fl3547, fl29186, fl41081, fl41096 and fl56136 in the Main Deck, and fl28689 in the Side.
None is a training list or inside code list v1. The report's per-crash card, Elemental HERO Gaia, is only the last card chosen.

### Controls

| Run | Seeds | Duels | Expected | Observed |
| --- | --- | ---: | --- | --- |
| Code list missing Pyramid Turtle | 88990000-88990011 | 12 | Every duel aborts | 12 aborts, `[card_reader_callback] Card not found` |
| Planted Lua error in Mystical Space Typhoon | 88991000-88991023 | 24 | Diagnostics, tied to duels | 9 diagnostic lines, each with its duel, in all three policies |
| Pilots | 88900000-88900023, 88901000-88904011, 88902000-88902035 | 3,072 | Clean | Clean |

Probe seeds: the same pair 88500000-88515999, generated decks 88600000-88614999 (and 88690000-88690002 to debug the tool), with and without Dark
Panther 88700000-88753999. The Side-in pilot reused 36 seeds inside the throughput pilot's range, with other lists. Replays reuse the seeds they replay. Nothing else overlaps.

### Time and what was skipped

The two stages took 66 minutes. Probes, replays, pilots and controls took about 25 more, for about 1.5 hours of the 3-hour allowance.

- I did not build or run the audit module, for the reasons above.
- I ran no per-card dedicated stage for every card. The audit's confirmed crash cards were covered by hardening's soaks, and this soak plays real lists.
- I did not find the root cause of the crash. A symbolized or sanitizer build would likely locate it, and that needs a rebuild, which this task ruled out.

## What this does not show

- **Placements are one result per list.** Nothing here ranks decks or archetypes. Each event publishes only its top placements, 1 to 24 of them.
- **Rulesets come from the community and event name only.** I did not read event rules, and early community events may have differed in rulings.
- **Archetype labels are the source's.** Similar lists can carry different labels, and labels were not checked against card content.
- **The soak shows that lists run, not that rules are right.** Random and rule-based play reach many paths, but crashes behind rare prompts can hide.
- **Side-in moves the whole Side at once**, as the audit did. It is a stress test, not a siding plan.
- **One deck could not be fetched** (67937, HTTP 500 twice), and 91 responses date from November 4. An event that published more decks since then is not reflected.

## Reproduce

```bash
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
W=<worktree>; P=/home/mada/ygo-agent/.venv/bin/python; T=$W/tools/edison/corpus
cp <main>/ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so $W/ygoenv/ygoenv/ygopro/   # sha256 b8fcd2bc...
$P $W/tools/edison/pool_audit/build_codelist.py
$P -I $T/fetch_formatlibrary.py --seed-cache <main>/build/edison-pool-audit-2026-11-04/web/formatlibrary
$P $T/build_corpus.py --workers 12
S=$W/build/edison-corpus/soak
$P $T/soak.py --stage main --seed 88000000 --output $S/main --workers 12 --neighbours 16 --per-pair 6
$P $T/soak.py --stage sidein --seed 88200000 --output $S/sidein --workers 12 --neighbours 16 --per-pair 6
$P $T/probe_targets.py --pair A.ydk B.ydk --per-target 24000 --seed 88700000 --output $S/probe-pair-A   # see notes.json for each probe
$P $T/report.py --soak $S/main $S/sidein --controls <pilot, control and pair-probe dirs> --probes <probe dirs> --extra $T/notes.json
$P -I $T/privacy_check.py $W/assets/edison/corpus $W/tests/edison/reports/corpus-2026-11-09 $W/docs/edison-corpus.md $W/tools/edison/corpus
```

`build_corpus.py` refuses to run unless the module hash is the certified one. It makes no web request.
