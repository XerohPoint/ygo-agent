# Edison pool and metagame audit

Prepared November 4, 2026 at commit 4e3452c. This is M0 evidence for M3, M4 and M5.
No runtime source, existing deck, or lock file changed.

Random play shows that a card runs without crashing the interface. It does not show that
the card follows March 2010 rules, and a card can pass every check here and still be wrong.

**Status update.** This report records the state before fixes. [The hardening report](edison-hardening.md) adds the message
handler, handles the crash classes below and adds the 13 cards.

## Summary

- **Scripts are not the blocker.** After alias resolution, no legal card lacks a script, and
  all 3,759 accepted ids load with no Lua error.
- **The interface is the blocker.** Uniform random decks from the full pool crashed the
  environment in **1,519 of 3,000 duels (50.6%)**. Every crash is a thrown C++ exception that
  aborts the process. 114 cards are confirmed causes, across coin flips, dice, race
  declaration, card declaration, zone disabling, Spirit return prompts and others.
- **Production cannot see Lua errors.** `init_module` never calls `set_message_handler`, so the
  core discards them. The existing worker's diagnostic check cannot detect a failing card script.
- **The real metagame mostly runs.** All 18 gauntlet lists completed 1,296 of 1,296 random
  duels with their Main and Extra Decks (two with one illegal card removed in memory). Moving the whole Side in caused 120 crashes in
  1,296 duels, all from Dark Dust Spirit and Snipe Hunter.
- **The pool manifest has at least one real gap.** XX-Saber Gottoms is in 8 of 75 sampled
  tournament lists and in a listed legal set, but `legality.json` omits it, so production
  rejects 2 of the 18 lists. 12 more cards look like the same omission.
- **Side Decks are parsed, validated and then discarded.** Match play can be built without
  native changes, but `info:deck` exposes both deck ids to Python and must be kept from policies.

## Reproduce

The soak order matters, because each stage plans from the state left by earlier stages.

~~~bash
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
P=.venv/bin/python; T=tools/edison/pool_audit
$P $T/build_audit_module.py            # separate .so under build/; production untouched
$T/build_probe.sh                      # native per-card load probe
$P $T/build_codelist.py
(cd scripts && ../build/edison-pool-audit-2026-11-04/script_probe ../assets/locale/en/cards.cdb \
  ../build/edison-pool-audit-2026-11-04/codes_full.txt \
  <(cut -d' ' -f1 ../build/edison-pool-audit-2026-11-04/codes_pool_only.txt) \
  > ../build/edison-pool-audit-2026-11-04/script_probe.jsonl)
$P $T/static_features.py
$P $T/soak.py --plan $T/plans/s1.json --workers 6 --batch 30
$P $T/quarantine.py   # before each later plan; 128, then 131, then 134 cards
$P $T/soak.py --plan $T/plans/s2.json --workers 6 --batch 30 --resume
$P $T/soak.py --plan $T/plans/s3.json --workers 6 --batch 30 --resume
$P $T/build_gauntlet.py; $P $T/validate_gauntlet.py; $P $T/gauntlet_cards.py
$P $T/soak.py --plan $T/plans/g.json --workers 6 --batch 30 --resume
$P $T/equivalence.py
$P $T/soak.py --plan $T/plans/c.json --workers 6 --batch 30 --resume
$P $T/errata_scan.py; $P $T/rulings_extract.py; $P $T/rulings_crosscheck.py; $P $T/pool_crosscheck.py
$P $T/aggregate.py; $P $T/confirm_cards.py; $P $T/gauntlet_report.py; $P $T/report_tables.py
~~~

The plan files hold absolute paths for this checkout. Web fetches need network access and
cache under `build/edison-pool-audit-2026-11-04/web/`.

**Seeds used:** 71000000 to 71033757 for the soak, 71900000 to 71902399 for equivalence, and
71990000 for the negative control. All are in the block reserved for this audit.

## Part 1: pool coverage

### Scripts and aliases

Starting numbers checked out, but the "missing scripts" reading did not.

| Measure | Count |
| --- | ---: |
| Manifest entries / accepted ids (all in `cards.cdb`) | 3,659 / 3,759 |
| Entries with `c<manifest code>.lua` | 3,237 |
| Normal Monsters, which load no script (`is_load_script`) | 408 |
| Non-Normal entries with no file at the manifest code | 14 |
| **Non-Normal entries still missing after alias resolution** | **0** |
| Entries with a historical override in `historical_scripts.h` | 26 card scripts, plus a Union helper suffix |
| Forbidden entries, which production decks reject | 43 |

The 14 use an alternate-art id as manifest code, such as 18144507 (Harpie's Feather Duster)
and 80604092 (Ultimate Offering). The core loads the alias script for these, so **the base id
must also be in the code list** or `script_reader_callback` throws. The manifest does contain
every base id.

The native probe made a fresh duel for each of the 3,759 ids, including Forbidden cards: **0
Lua diagnostics and 0 card-metadata misses at load**. A planted runtime error was detected
by both the probe and the audit module, and a planted syntax error by the audit module.

### Why an audit module was needed

The production `.so` exports no core symbols and never installs a handler, so `handle_message`
reaches the core's no-op default. `build_audit_module.py` compiles a separate copy with two
additions: a handler, and a hook that logs chain and Summon messages before the production
handler consumes them. It does not modify `ygoenv/` or the production binaries.

Equivalence against the production `.so` on the same seeds:

| Set | Duels | Identical outcome and decision trace | Notes |
| --- | ---: | ---: | --- |
| Crash-prone cards blocked | 120 | 120 | 116 finished in both |
| Nothing blocked | 80 | 80 | 42 crashed in both, same message |

So these crashes are production behavior. Compiler flags differ (`-O2`, no LTO).

### Soak results

33,758 duels and 4,144,114 decisions, at most 6 worker processes. Decks were generated from
the full pool and passed the production validator in `init_module`; there were no rejections.

| Check | Result |
| --- | ---: |
| Finished naturally | 31,666 |
| Native crash (process abort) | 2,092 |
| Lua diagnostics during play | 0 |
| Action overflow (limit 256) | 0; largest offered set 58 |
| Truncation, decision limit, core timeout | 0 |
| Stuck prompts: identical state 100+ times in a duel | 0 |
| Observation contract violations | 0 |

The contract check ports most of the `agent_match.py` assertions, but not the
candidate-row and `context[8..9]` checks.

| Stages | Decks | Policy | Duels | Crashes |
| --- | --- | --- | ---: | ---: |
| s1 | Uniform, nothing blocked | Uniform | 3,000 | 1,519 |
| s2, s13 | Semi-themed, quarantine blocked | Eager | 5,000 | 47 |
| s3, s5 to s12 | Targeted at unexercised cards, quarantine blocked | Eager | 17,500 | 24 |
| s14 | Uniform, quarantine blocked | Uniform | 3,000 | 40 |
| s4, s15, s16 | One target at a time | Eager | 2,666 | 342 |
| g1 | Gauntlet Main and Extra | Half each | 1,296 | 0 |
| g2 | Gauntlet with Side moved in | Half each | 1,296 | 120 |

Quarantine only keeps a card out of later coverage decks so that others can play. Every
quarantined playable card also ran in a dedicated stage. The Forbidden ones cannot enter a production deck.

### Crash classes

Attribution comes from the last chain link and last chosen action, so one crash beside a card
proves little. A card counts as **confirmed** if its script has the matching feature, or if at
least a third of its dedicated duels crashed. The second rule rests on 4 to 12 duels per card.

| Failure | Duels | Confirmed cards | Examples |
| --- | ---: | ---: | --- |
| `Unknown message toss_coin` | 355 | 20 | Arcana Force monsters, Barrel Dragon, Light Barrier |
| `Unknown message toss_dice` | 295 | 17 | Skull Dice, Graceful Dice, Snipe Hunter |
| `cannot find id: 1104` (Spirit return) | 285 | 13 | Dark Dust Spirit, Susa Soldier, Asura Priest |
| `Unknown message field_disabled` | 183 | 9 | Dimensionhole, Embodiment of Apophis, Stronghold |
| `invalid format of opcodes` (declare a card) | 148 | 6 | Prohibition, Mind Crush, D.D. Designator |
| `cannot find id: 60` (call a coin) | 141 | 11 | Time Wizard, Goddess of Whim, Fairy Box |
| `Unknown message announce_race` | 130 | 5 | DNA Surgery, Array of Revealing Light |
| `cannot find id: 70` (declare a type) | 95 | 5 | Reversal Quiz, Vampire Lord |
| `Number N not implemented` (over 12) | 82 | 6 | Mausoleum of the Emperor, Psychic Commander, Star Blast |
| `weight not implemented` (double Tribute) | 74 | 6 | Kaiser Sea Horse, Gellenduo, Earth Effigy |
| `Card not found: 1` (declare a monster) | 55 | 2 | Question, Cloak and Dagger |
| `Unknown message deck_top` | 54 | 4 | Drill Bug, Grave Lure, Magic Drain |
| `Select place count 2` | 52 | 2 | Ground Collapse, Phantom Dragon |
| `Announce attrib count 2` | 40 | 3 | DNA Checkup, Earthshaker |
| `Unknown message cancel_target` | 27 | 1 | Seal of Wickedness |
| `Unknown message reverse_deck` | 27 | 1 | Convulsion of Nature |
| `cannot find id: 1213`, `1214` | 24 | 2 | Dark Spirit Art - Greed, Call of the Earthbound |
| `Select counter count` over 2 | 14 | 1 | Cloudian - Altus |
| `min(1) != max(n)` in tribute selection | 10 | 0 | Unattributed; Cloudian cards are the leading lead |
| `cannot find id: 10` | 1 | 0 | One duel; the only attribution is Savage Colosseum |

For the conformance workstream: **Susa Soldier and Mausoleum of the Emperor are on the
35-card errata list and both crash the interface**, so their listed behavior cannot be reached
in a production duel today. Mausoleum crashes when it prompts for 1000 or 2000.

### Who was exercised

| Kind | Entries | Forbidden | Chain seen | Summoned, no chain | Offered only | In deck only |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Effect Monster | 1,634 | 16 | 1,183 | 403 | 32 | 0 |
| Normal Monster | 408 | 0 | 0 | 408 | 0 | 0 |
| Ritual Monster | 26 | 0 | 11 | 15 | 0 | 0 |
| Fusion Monster | 123 | 1 | 57 | 57 | 6 | 2 |
| Synchro Monster | 55 | 1 | 49 | 5 | 0 | 0 |
| Spell | 781 | 19 | 727 | 9 | 26 | 0 |
| Trap | 632 | 6 | 587 | 7 | 31 | 1 |

- **In at least one deck:** 3,616 of 3,616 playable entries (100%).
- **Summoned or activated:** 3,518 of 3,616 playable (97.29%), or 96.15% of all 3,659.
- **Activated, among scripts with an activated effect:** 2,581 of 2,742 (94.13%).
- **Never activated:** 161 cards with an activated effect. Another 98 had no chain or Summon;
  47 of those are confirmed crash cards. The full list is in `unexercised.json.gz`.
- **Forbidden, never in a production duel:** 43, covered by the load probe only.

"Activated" means a `MSG_CHAINING` was seen. It says nothing about whether the effect resolved
correctly, and continuous effects never produce one.

### Limits on pool size

| Where | Limit | Effect beyond it |
| --- | --- | --- |
| `CardId` in `ygopro.h`, two bytes in observations | 65,535 ids | Far above the 4,021 tested; the module loads them in 0.13 s and 60 MB |
| Resource closure of the code list | Must include tokens and alias bases | Crash. A pool-only list crashed 70 of 400 duels, 61 on missing tokens. The audit list adds 238 tokens and 24 script-referenced cards |
| `ygoai/rl/jax/agent.py` default `n_embed` of 999 | ids over 999 | **Silent NaN**: `flax.linen.Embed` returned NaN for an out-of-range id here |
| `ygoai/utils.py` `load_embeddings` | `pad_to=999`, and `int(line)` per line | Assertion past 999; cannot parse the `code has_script` format at all |
| `max_options` default 16 | Production passes 256 | Observed need 58. Listing every card name in a declare prompt would pass 256, so fixing declaration needs a different action shape |
| `agent_match.py`, `smoke.py`, RL tools | `PROFILE`/`DECKS` hard-coded | No code-list or deck-directory argument; card ids are line numbers, so inserting mid-list breaks saved identity |
| `ygoai/edison_policy.py` | Scores named cards | Any other card gets generic scoring, and those features feed the learner |
| `generate_legality.py`, `edison_legality_data.h` | Compiled pool | Adding a card needs regeneration, a rebuild and new fingerprints |
| `MSG_*` handling and prompt limits | See crash classes | Hard crash, not a soft failure |

`max_cards` (160 rows) and `uint8` row indices held across all duels.

### Pool manifest cross-check

Compared with one dated YGOPRODeck TCG listing to April 24, 2010, and the
[Edison legal sets page](https://www.edisonformat.com/legal-sets.html):

- **13 cards in the database, in a listed legal set, but absent from the manifest:** XX-Saber
  Gottoms, Discord (ANPR); Chain Material, Metabo Globster, Mutant Mindmaster (PTDN); Psychic
  Tuning, Ido the Supreme Magical Force (CRMS); Synchro Control, Tuner's Scheme (ABPF); Gladiator
  Beast's Battle Manica (GLAS); Froggy Forcefield (LODT); Shiba-Warrior Taro (YAP1); Scrubbed Raid (DP09).
- **Only Gottoms is proven in real lists** (8 of the 75 Format Library decks sampled). The
  manifest follows YGOPRODeck's Edison filter, which omits it. Supervisor check: Format Library's
  card API gives all 13 a TCG date between December 2007 and February 2010, before its April 24,
  2010 format date, which supports treating them as manifest omissions. Reasons a card might still
  be excluded were not checked.
- **Duel Terminal cards are probably correctly out.** 27 more cards dated January 2010 are in
  DT01, which is not on the legal sets page. Tyler the Great Warrior, The Wicked Eraser and The
  Wicked Avatar are also absent and unclassified.
- **Four manifest cards have no dated TCG listing there:** Orichalcos Shunoros, Exodius the
  Ultimate Forbidden Lord, Genesis Dragon, Darkness Neosphere. Not investigated.
- **Dewloren** is on the March 2010 list but absent from the pool; its TCG date in the listing is
  May 2010, so this looks correct.

## Part 2: historical correctness risk beyond the 35 listed cards

The reference is the [Edison rulings](https://www.edisonformat.com/rulings), with the
[relevant errata page](https://www.edisonformat.com/rulings/relevant-errata) ahead of memory
of printings. Those 35 cards belong to the conformance workstream and are listed only as
dependencies below.

Method: errata commits in the scripts repository, hard once-per-turn patterns, "You can only"
database text, and a text-signal comparison of each gauntlet card's ruling section against its
script. **None of this is exhaustive**; each item says what was read.

| Priority | Card | Decks | Difference | Basis | Handled |
| ---: | --- | ---: | --- | --- | --- |
| 1 | Ancient Fairy Dragon | 3 Extra | Script has a name-wide hard once per turn on each effect, destroys all Field Zone cards, and limits the search to a different name. The rulings give plain once per turn, destroy a Field Spell, then add any Field Spell | Commit 9150b7cbf (2022) read; [rulings A-C](https://www.edisonformat.com/rulings/individual-rulings-a-c) | No |
| 2 | Night Assailant | 1 | 2022 script adds a control check at resolution. Excluding itself matches the rulings, so only the edge case differs | Commit 34d176927 read; [rulings L-O](https://www.edisonformat.com/rulings/individual-rulings-l-o) | No |
| 3 | Machina Fortress | 3 copies, 1 deck | A 2016 commit added `EFFECT_NECRO_VALLEY` guards. The Edison Necrovalley override uses a different code, so the guard looks inert; not tested in a duel | Commit 0c3fd9973 read | Indirectly |

**Checked, no difference found** at the level read (effect type, target flag, and limit against
the ruling): Dark Armed Dragon, Stardust Dragon, Ally of Justice Catastor, Elemental HERO
Stratos, Thought Ruler Archfiend, Tytannial, Gladiator Beast Bestiari, Battle Fader, Instant
Fusion (the ruling includes its one-per-turn condition), Drill Warrior, Red Dragon Archfiend.

**Named in the brief:** Sangan is on the 35 list with an override. Witch of the Black Forest
has a hard once-per-turn modern script but is Forbidden, so no legal duel can reach it.
Dark Armed Dragon has no difference found, as above.

**Rest of the pool, unreviewed nominations.** 119 entries carry a signal: 35 hard once-per-turn
scripts, 83 "You can only" texts, 28 errata commits (`errata_signals.json.gz`). Outside the
gauntlet these include Cyber Blader, Rare Metal Dragon, Judgment of the Pharaoh, Twin-Headed
Behemoth, Allure Queen and Ring of Destruction. **M4 can pick any of these**, so the deck builder
should treat them as unverified. Errata that changed a script with no matching commit message
or text pattern would not appear here at all.

**Gauntlet dependence on the 35 list:** Brionac and Goyo Guardian are in nearly every Extra Deck,
Brain Control in 14 lists, Armory Arm in 11, Dark End Dragon in 9 and Soul Exchange in 6. Dark
End Dragon and Soul Exchange have no script override, though a core patch or helper may already
give the listed behavior, so that is for conformance to decide. Per-deck lists are in `gauntlet.json`.

## Part 3: metagame gauntlet

Lists are under `assets/edison/gauntlet_candidates/`, unedited from each deck's `ydk` field,
with provenance in `manifest.json`. Priority follows the replay-share study, which is a selected
sample and not a true metagame share.

**Replay share covered: 90.66%** (17 archetypes), or **85.93%** counting only lists production
accepts today. Vayu Turbo, with no current fixture, has two lists.

| Deck | Share | Event | Date | Place / players | API source | Production legal | Exercised | Main, crash-free | Side-in crashes |
| --- | ---: | --- | --- | ---: | --- | --- | ---: | ---: | ---: |
| blackwing | 22.67% | EWCQ13 | 2026-06-21 | 1 / 126 | [78809](https://formatlibrary.com/api/decks/78809) | Yes | 42/42 | 144/144 | 6 |
| vayu_turbo | 22.38% | BEAK12 | 2026-09-05 | 2 / 157 | [84297](https://formatlibrary.com/api/decks/84297) | Yes | 52/52 | 144/144 | 7 |
| vayu_turbo_ddev08 | (same) | DDEV08 | 2025-07-19 | 1 / 215 | [60919](https://formatlibrary.com/api/decks/60919) | Yes | 48/48 | 144/144 | 10 |
| hero_frog | 9.33% | EWCQ12 | 2026-03-15 | 2 / 133 | [73078](https://formatlibrary.com/api/decks/73078) | Yes | 51/51 | 144/144 | 35 |
| gladiator_beast | 6.24% | EWC2025 | 2025-12-14 | 1 / 93 | [68372](https://formatlibrary.com/api/decks/68372) | Yes | 46/46 | 144/144 | 9 |
| frog_monarch | 4.66% | EWCQ13 | 2026-06-21 | 3 / 126 | [78922](https://formatlibrary.com/api/decks/78922) | Yes | 45/45 | 144/144 | 40 |
| dragon_turbo | 4.09% | PBR58 | 2026-09-21 | 3 / 60 | [85607](https://formatlibrary.com/api/decks/85607) | Yes | 42/42 | 144/144 | 4 |
| lightsworn | 3.16% | EWCQ14 | 2026-09-13 | 4 / 142 | [85060](https://formatlibrary.com/api/decks/85060) | Yes | 53/53 | 144/144 | 5 |
| frog_otk | 3.08% | EWCQ14 | 2026-09-13 | 1 / 142 | [84951](https://formatlibrary.com/api/decks/84951) | Yes | 42/42 | 144/144 | 9 |
| diva_hero_beat | 2.65% | EWCQ12 | 2026-03-15 | 4 / 133 | [73097](https://formatlibrary.com/api/decks/73097) | Yes | 47/47 | 144/144 | 3 |
| gigavise | 2.58% | BEAK12 | 2026-09-05 | 1 / 157 | [84319](https://formatlibrary.com/api/decks/84319) | **No: Gottoms** | 44/44 | 144/144 | 7 |
| machina_control | 2.15% | EWCQ13 | 2026-06-21 | 9 / 126 | [78816](https://formatlibrary.com/api/decks/78816) | **No: Gottoms** | 41/41 | 144/144 | 2 |
| diva_hero | 1.72% | EWCQ14 | 2026-09-13 | 9 / 142 | [85032](https://formatlibrary.com/api/decks/85032) | Yes | 52/52 | 144/144 | 8 |
| zombie_teledad | 1.43% | BEAK11 | 2026-05-07 | 5 / 165 | [75594](https://formatlibrary.com/api/decks/75594) | Yes | 51/51 | 144/144 | 10 |
| diva_frog | 1.43% | BEAK11 | 2026-05-07 | 2 / 165 | [75612](https://formatlibrary.com/api/decks/75612) | Yes | 46/46 | 144/144 | 33 |
| hero_beat | 1.15% | BEAK04 | 2023-08-04 | 3 / 106 | [28337](https://formatlibrary.com/api/decks/28337) | Yes | 41/41 | 144/144 | 6 |
| lightsworn_fairy | 1.15% | IMNCS05 | 2026-09-20 | 1 / 86 | [85583](https://formatlibrary.com/api/decks/85583) | Yes | 58/58 | 144/144 | 40 |
| quickdraw_plant | 0.79% | SJCED10 | 2010-04-25 | 1 / 2,175 | [2981](https://formatlibrary.com/api/decks/2981) | Yes | 46/46 | 144/144 | 6 |

- **Legality:** each file ran alone through the unmodified production `init_module`, including
  the 15-card Side. Sizes are all 40 to 42 Main, 15 Extra, 15 Side. The two rejected lists stay
  unedited; for the coverage runs only, Gottoms was removed in memory.
- **Exercised** means the card was summoned or activated somewhere in the soak. It is not a
  correctness check.
- **Side-in crashes** count any duel where the deck took part, so a deck also absorbs its
  opponent's crash. The causes are Dark Dust Spirit (87 duels; in hero_frog, frog_monarch and
  diva_frog Sides) and Snipe Hunter (33 duels; in the lightsworn_fairy Side).
- **Missing scripts:** none.

### Source trust

| Tier | Lists | Why |
| --- | --- | --- |
| High | Events of 100+ players on Format Library, Deck Devastators | Organizer-published lists with placement and a Challonge link |
| Medium | gladiator_beast (93), lightsworn_fairy (86), dragon_turbo (60) | Same source, smaller events |
| Medium, dated | hero_beat (2023) | No Hero Beat list in the recent events sampled; the metagame may have moved |
| Historical | quickdraw_plant (SJC 2010) | Jeff Jones's winning list as transcribed on Format Library, from an event actually played in 2010 |

**Ruleset labels.** Every list but SJC 2010 comes from community events (Format Library,
I-M-NERD, EdisonFormat.com), labelled traditional Edison per the supervisor's direction.
The only other Konami-community Edison event on the site, UTWT23 (Ultimate Time Wizard
Tournament 2023), was not used. I did not read each event's rules individually.

**How they were found.** The gallery pages render with JavaScript, so routes came from the
site's `main.js`: `/api/events/gallery/edison`, `/api/events/<abbr>`, `/api/decks/<id>`. Requests
were spaced apart. Dueling Book replay endpoints were not touched. These are placed lists from
selected events, not a tier ranking, and I did not check placements against Challonge.

Recent large fields agree on the top: Vayu Turbo 25, Blackwing 19, Lightsworn 12 of EWCQ14's
142; Blackwing 28, Vayu Turbo 23, Lightsworn 16 of BEAK12's 157. Lightsworn's event share is
well above its 3.16% replay share, another sign that replay share is only a guide.

## Part 4: match play and siding

### What exists

| Area | State |
| --- | --- |
| Side parsing | `read_stream` parses `!side`; `validate` applies size, pool and combined copy limits |
| Side retention | **Discarded.** `init_module` unpacks `side_deck`, validates it, and stores only Main and Extra |
| Side use | Nothing uses it; every fixture Side is empty |
| Match concept | None. Each reset is an independent duel from fixed `deck1`/`deck2` names |
| First player | Seat 0 always (`infos.turn_player = 0`; observed in test duels) |
| Results | `info:winner` is 0, 1 or 2, and -1 for truncated |
| Match-kill | Core message unhandled, but only Victory Dragon uses it, and it is Forbidden |

### Rules M5 must model

From [KDE-US Tournament Policy v2.5](https://img.yugioh-card.com/en/downloads/penalty_guide/YGOTCG_Tournament_Policy_v_2_5.pdf),
which is **a modern document, not a 2010 or community rule set**:

- A Match ends when a Duelist has won two Duels or lost two.
- Side exchange is 1-for-1, in full count of the opponent, and the Side stays the same size.
  Main and Extra can both exchange with the Side but not with each other.
- The loser of the previous Duel decides who goes first; after a draw, a random method picks.
- Three Duels without a result lead to a fourth, time permitting.

The owner should confirm the rules used on Format Library and Dueling Book. The Edison rules
pages I read link only to a 5D's rulebook PDF, which I did not read.

### Minimal changes for M5

1. **A Python match runner under `tools/edison/` is enough at first.** Calling `init_module` again
   between games registered a new deck and kept the old ones (0.11 s; tested). A new env per game
   costs about 1 ms plus 5 ms for reset, so post-side decks need no native change.
   It leaks script buffers on each call, so the eventual fix is a small `register_deck`.
2. **Who goes first:** pass the chosen first player's deck as `deck1` and map learner seat per
   game. Reward attribution must keep following absolute seat.
3. **Side swap helper:** enforce 1-for-1 by zone, then validate through production. Copy totals
   cannot change, but the zone type check still applies.
4. **Results:** define draw, truncation and crash handling. A failed game should void the whole
   match, matching the rule that incomplete games are not wins or losses.
5. **Seeds:** derive per match and game from a new block, so games do not repeat shuffles.
6. **Prerequisites:** fix Spirit return and dice, or Side play crashes on 4 lists; settle Gottoms.

A native change would invalidate fingerprints and require recertification, so it is worth
batching with the interface fixes.

### Privacy across games

- **`info:deck` carries both deck ids at game end** (seen in test as `[1, 0]`). A match runner
  that passes `info` to a policy would leak the opponent's deck and any siding plan. Strip it.
- **Memory must be built only from that seat's observations:** visible card rows and revealed
  identities. It must never use a deck name, YDK, or another seat's trace. In self-play each seat
  holds separate memory, cleared at match end.
- **Reveals are transient.** `revealed_` is cleared on many messages and at reset, and there is
  no persistent deck-top observation, so memory must record a reveal on the decision it is visible.
- **Opponent's Side is hidden, except its count,** which policy makes public. Main and Extra
  counts are already visible, so a size change after siding is legal information.
- **Advisor inputs** are the player's own list, what they saw, and the prior metagame. M5's inference
  should be testable by showing the result is unchanged when the unseen opponent cards change.
- **Card ids are line numbers**, so stored memory breaks if the code list changes between games.

## What was not verified

- **Rules correctness of any card.** Random play exercises code paths, with no oracle for results.
- **Reasoned rather than observed:** the attribution of crash causes rests on the last chain link
  or action. Cards marked confirmed rest on a static feature or a small dedicated sample.
  Crash classes with no confirmed card remain open.
- **Cards unreachable under random play.** 98 playable cards had no Summon or chain, so any
  crash or error behind those prompts is unknown. Crashing cards also hid what follows the crash.
- **Part 2 coverage.** Rulings sections were read for about 40 cards and compared by text signal
  for the rest. 42 of the 244 gauntlet cards had no section matched by my name search.
- **Side-in is a stress test**, moving the whole Side at once, not a realistic siding plan.
- **The 13 cards as false exclusions** are inferred from set listings. Only Gottoms was seen in a
  real list, and no card's print date was checked beyond one listing.
- **Tournament strength.** Placements are one result per list; nothing here ranks decks.
- **Time, overtime and judge procedures** are not modelled or researched for 2010.

## Evidence

Under [`tests/edison/reports/pool-audit-2026-11-04/`](../tests/edison/reports/pool-audit-2026-11-04/):

- `per-card.csv.gz`: every entry, with script, probe, soak and crash fields.
- `summary.json`: totals, stages and crash classes.
- `confirmed-crash-cards.json`, `failures.json.gz`: confirmed causes and all 2,092 crash records.
- `unexercised.json.gz`, `gauntlet.json`.
- `equivalence.json.gz`, `script_probe.jsonl.gz`, `static_features.json.gz`.
- `errata_signals.json.gz`, `rulings_leads.json.gz`, `pool_crosscheck.json.gz`.

Tools are in [`tools/edison/pool_audit/`](../tools/edison/pool_audit/). Raw downloads, the audit
module and stage deck lists stay in `build/edison-pool-audit-2026-11-04/`, which is ignored.
