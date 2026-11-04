# Edison rules conformance

Measured November 4, 2026, on `edison` at `5b2b169`. Target ruleset: traditional Edison as written at
edisonformat.com, not Konami's 2025 Time Wizard variant.

**Status update.** This report records the state before fixes. [The hardening report](edison-hardening.md) fixes D2, D5, D7,
D8 and D10, shows D3 was a fixture error rather than a defect, and leaves D4 open because individual rulings conflict with it.
`known_failures.json` is the current record. The matrix files were not rebuilt. [The trigger-order report](edison-trigger-order.md)
records the owner's rulings on D1 and D4: D1 is fixed, and D4 was a fixture error, so A1 and decision 1 below are settled.

**Result: the simulator disagrees with the written specification in 12 distinct ways, measured by 30 failing
fixtures. No simulator behavior was changed.** Two of the 12 most affect play: Graveyard Ignition Effects are
missing from the Summon window (D2), and Trigger ordering across SEGOC steps contradicts the source (D1). D1 is
a decision for you rather than a plain bug, because the sources conflict and six existing fixture groups assert
the opposite. See [Decisions needed](#decisions-needed).

Certification is unaffected: 544 PASS, 0 FAIL, with fingerprints, binaries and traces identical before and
after this work.

**Supervisor review.** `run_conformance.py` was rerun and reproduced 138 cases, 108 passing, 30 failing as recorded, with
138 clean controls. The certification binary hash matches its recorded value. Defect causes and fix sizes stay estimates
until fixes land. The extracted rulings text behind the matrix is kept in the ignored `build/` directory rather than
committed. Regenerating it needs the page captures this study made. Check the matrix's short per-card expectations before
any public push. D1's conflict also appears on [edisonformat.net's SEGOC page](https://edisonformat.net/rules/compendium/SEGOC-And-Early-Trigger).

| Category | Items | PASS | FAIL | UNTESTED | NOT-TESTABLE-YET |
| --- | ---: | ---: | ---: | ---: | ---: |
| Rule differences and advanced rule pages | 85 | 57 | 21 | 7 | 0 |
| The 35 errata cards, by sub-claim | 49 | 47 | 0 | 1 | 1 |
| Per-card rulings, top metagame cards | 294 | 63 | 3 | 145 | 83 |

The errata row is the strongest result: every testable errata claim but one now passes. Per-card rulings are
the weakest: about one in five of the bounded set was tested. See [Coverage limits](#coverage-limits).

Matrix: [conformance-matrix.json](../tests/edison/reports/rules-conformance-2026-11-04/conformance-matrix.json)
and [conformance-matrix.md](../tests/edison/reports/rules-conformance-2026-11-04/conformance-matrix.md).

## Defects, ranked by how much they matter

"Decks" counts how many of 75 sampled Format Library decks run a card. Those counts are measured. **The
"Impact" column is my judgment of the format, not a measurement.** Causes are likely, read from code, and
unconfirmed because nothing was fixed. Every defect is a difference under Rule 1 only unless noted.

| # | Defect | Fixtures | Impact (judgment) | Likely cause | Fix size |
| --- | --- | --- | --- | --- | --- |
| D2 | Graveyard Ignition Effects are not offered in the Summon window | 231, 238, 460A, 460B | **Medium-high.** A legal action is missing on most Summons by decks with Plaguespreader 34/75 or Vayu 28/75, so the action mask is wrong often. The strategic effect is smaller: priority only escapes D.D. Crow (66/75) outright when the banish is a cost, as for Malicious (1/75). Crow can still chain to Plaguespreader or Vayu | Core: `processor.cpp` Obsolete ignition ruling requires `current.location == LOCATION_MZONE` | Small core patch |
| D1 | SEGOC exception applied across steps, not within one | 241, 242 | **Medium-high, if the source is right.** Caius 34/75 with Brain Control 60/75 and Soul Exchange 11/75 makes stolen-Tribute lines common, though the trigger needs a mandatory-trigger victim (Sangan 5/75) | Core: `early-trigger.patch` groups by chronology first, then SEGOC | Medium core change, plus changing six fixture groups |
| D3 | Torrential and Bottomless are offered after a Chain Link 2+ Special Summon | 423, 424 | **Medium.** Torrential 64/75, Bottomless 45/75. Needs a Summon in a higher link, such as Call of the Haunted 10/75 | Core: `delayed_activate_event` keeps the event. Also true under Rule 5 | Medium core change |
| D4 | An Ignition Effect is offered after declining a Summon trigger | 235A | **Low-medium.** Armageddon Knight 25/75, Stratos 11/75; declining is uncommon | Core: same ignition block has no check for a declined trigger | Small core patch |
| D5 | Caius inflicts damage when a Condition banishes the target | 441A, 447 | **Low-medium.** Caius 34/75 with Plaguespreader 34/75 or Battle Fader 10/75. 1000 LP when it occurs | Lua: `c9748752.lua:35` checks only the final location | Small Rule 1 override |
| D6 | Triggers do not activate after returning to hand or Deck | 261, 262B | **Low.** Dandylion 11/75, Monster Reincarnation 6/75, Necroface 0/75 | Core: `check_nonpublic_trigger`, private-location handling | Medium core change |
| D7 | End-of-turn discard triggers follow modern rules | 281, 282 | **Low.** Needs seven or more cards in hand and a suitable card | Core: `processor.cpp` End Phase discard then runs an ordinary point event | Medium core change |
| D8 | LP costs and maintenance can reduce LP to 0 | 271A-C, 272-274 | **Low in play, but it affects training.** An agent can lose by paying. Maintenance cards are rare (0-1/75) | Core: `field.cpp:2330` `val <= lp`; should be `<` for Rule 1 | One-line core patch; the maintenance cases are unconfirmed until tried |
| D9 | Unlisted Unions replace effect destruction | 206 | **Low.** Sampled decks run only listed Unions (Gearframe 6/75); the cards tested are 0/75 | Lua: `utility.lua:309` in `EnableUnionAttribute` | Small; needs ambiguity A3 settled |
| D10 | Two 0 ATK Attack Position monsters survive battle | 296 | **Low.** Attacking 0 ATK into 0 ATK is rare | Core: `calculate_battle_damage`, `processor.cpp:3377` | Small core patch |
| D11 | Trap Monster edge cases | 216, 218, 219A, 219B, 219E, 223 | **Negligible today.** Apophis, Zoma, Metal Reflect Slime are all 0/75 | Mixed: Fake Trap `c3027001.lua:14` requires `LOCATION_SZONE`; My Body override `cfilter`; Major Riot `c9074847.lua:14`; stolen cases possibly `trap_monster_adjust`, unconfirmed | Small per card; the stolen cases are unknown |
| D12 | Voluntary infinite loops are not illegal | 291 | **Negligible.** Pole Position 0/75. The engine stays stable and destroys it (292, 293) | Missing mechanism: legality would need a lookahead | Large; I would not fix this |

### Minimal reproductions

Each is a function in `tests/edison/conformance/`, run with `--test NNN`.

- **D2**: P0 Normal Summons Bazoo with Plaguespreader in GY and P1 holding D.D. Crow. The effect is offered only
  at the next idle prompt, not in the Summon window. Test 460B is the same with Vayu. A monster on the field
  works (232), and the card is legal afterwards, so the fixture is sound.
- **D1**: P0 activates Soul Exchange on P1's Sangan, then Tributes it for Caius. Observed CL1 Sangan, CL2 Caius.
  The source's Example 2 states CL1 Caius, CL2 Sangan. Test 242 uses Dark World Dealings, Watapon and Broww.
- **D3**: P0 activates Reinforcement of the Army, chains Call of the Haunted. Bazoo is revived at CL2, then CL1
  resolves. Torrential and Bottomless are offered. 425 passes, so the case where CL1 itself Summons is correct.
- **D4**: P0 has Bazoo on the field and Normal Summons Armageddon Knight, declining its trigger. Bazoo is
  offered as Chain Link 1. Accepting the trigger is correct (235B).
- **D5**: Plaguespreader Summoned by its own effect is targeted by Caius: 1000 damage. Test 447 uses Battle Fader.
- **D6**: Monster Reincarnation discards Dandylion and returns it. Dandylion does not activate from hand.
- **D7**: P0 ends the turn with 8 cards and discards White Stone of Legend. Super Rejuvenation is offered. Test
  282 discards Peten and is offered its optional trigger.
- **D8**: P0 at exactly 800 LP is offered Brain Control, and at 801 too. Mirror Wall at 2000 LP pays and loses.
- **D9**: X-Head Cannon equipped with Y-Dragon Head survives Smashing Ground.
- **D10**: Battle Fader attacks Battle Fader in Attack Position; neither is destroyed.
- **D11**: See the fixture names in `known_failures.json`.
- **D12**: X-Head Cannon can be Normal Summoned into the source's Example 1.

## Decisions needed

1. **D1, chronology versus steps.** `edisonformat.com` (the source named for this work) limits the exception to
   "the same step" and gives Example 2. `edisonformat.net/rules/compendium/SEGOC-And-Early-Trigger`, which the
   existing fixtures cite, says early trigger "does override regular segoc" and gives no cross-step example.
   Fixtures 004C, 005, 006, 017, 018 and 033 assert the `.net` reading, and all pass. Fixing D1 changes their
   expectations, which this pass was told not to do. I marked D1 FAIL because `.com` is the stated specification
   and its example is explicit. What players actually do is unverified, so ask the community before changing it.
2. **Whether to fix D3 for Rule 5 as well.** It reproduces under Rule 5. A Rule-1-only fix preserves comparison
   behavior, as the standing rules require.
3. **Whether to promote passing fixtures into certification.** 108 fixtures pass here but are outside the gate
   on purpose. Promoting them would change the baseline count, so that is your call.

## Source ambiguities

- **A1**: SEGOC across steps. See above.
- **A2**: The Solemn Judgment ruling list contradicts itself: "Negate the Normal Summon from Ultimate Offering's
  effect that resolved as Chain Link 1", then "DO NOT negate the Normal Summon from Ultimate Offering". The errata
  page and the Summons page both say it cannot. The simulator follows those (069), and I treated that as correct.
- **A3**: Rule 3 says the "would be destroyed" replacement appears only on six Unions, but not whether others keep
  a battle-only replacement. My memory of old printings is that they do; that is unverified, so 206 asserts only
  effect destruction. The database holds modern text and cannot settle this.
- **A4**: Rule 2's Village example says the player cannot activate Umi "unless they control a Spellcaster". The
  card restricts on your own Spellcasters in a way that does not obviously match. Only the case with no
  Spellcaster was tested (202).
- **A5**: "My Body can chain to face-up Royal Oppression". The Stardust list separates Oppression negating a
  Summon from Oppression destroying a card on the field, but the My Body list does not. Existing 067 treats a
  pending Summon as unavailable. I left that standing.
- **A6**: D.D. Survivor's errata bullets about attack targets look copied from Cyber Phoenix and Jade Knight.
  It has no such trigger. Marked NOT-TESTABLE-YET (E07.b).
- **A7**: The errata page cites a "most recent text" policy, while its listed behaviors differ from modern text.
  The listed behaviors were treated as authoritative, as instructed.
- **A8**: Rule 9's source says its findings "suggest" this and "we can surmise". D6 therefore rests on inference
  from one 2009 document, and manual play may differ.
- **Manual play generally**: Format Library, Really Big Edison and Dueling Book players may play differently
  from these written sources. I checked none of that. Rule 12 in particular would depend on players or judges.

## Coverage limits

**Rule differences**: 7 of 85 are untested, listed in the matrix. Most are low-value or share a mechanism with a
fixture that already fails.

**Per-card rulings**, bounded by deck count:

- The source pages hold about 9,800 rulings across about 2,160 cards, by my parser's count. I selected the
  top 45 cards by deck count; 38 matched a section, giving 294 rulings.
- 66 of those 294 have a fixture. 145 are untested claims. 83 were filed as descriptive by a **keyword heuristic
  I did not review line by line**, so some may in fact be testable.
- Seven top cards had no ruling section my parser could match: Heavy Storm, D.D. Crow, Mystical Space Typhoon,
  Allure of Darkness, Lightning Vortex, Flamvell Uruquizas and Blackwing - Silverwind. Silverwind is misspelled
  on the page. I did not check the others further.
- Below rank 45, nothing was attempted. That excludes most Gladiator Beast and Lightsworn cards, although both
  are major deck types. End-of-Battle-Phase tag-outs are entirely untested and would be my next priority.

## How the failures stay out of the gate

Fixtures live in `tests/edison/conformance/`, in their own executable and their own XMake project.

- `tests/edison/*.cpp` does not match the subdirectory, so `edison_rules` does not link them.
- `run.py` fingerprints only the top level of `tests/edison`, so these files change no fingerprint.
- Failures are recorded in `known_failures.json`. `run_conformance.py` exits 0 only if results match that record,
  and exits 1 on a new failure, a recorded failure that now passes, a setup failure, a hang, a crash or a replay
  difference.
- Fixtures separate `[RULE]` (the simulator disagrees) from `[SETUP]` (the fixture did not reach its checkpoint).
  Only `[RULE]` is evidence about the simulator. The final run has no `[SETUP]` failures.

Each fixture also runs under Rule 5, which records whether Rule 5 meets the same expectation. Rule 5 is a
comparison here, not a requirement, so it does not fail on a difference.

## Run it

```bash
# Build. Run from this directory with -P . : without it, xmake walks up to a parent
# checkout and rewrites that checkout's configuration.
cd tests/edison/conformance
xmake f -P . -o ../../../build/edison-conformance -y
xmake b -P . -j 3
cd ../../..

# All conformance fixtures, isolated, each twice
python tests/edison/conformance/run_conformance.py

# One fixture and its Rule 5 comparison, through the certification runner
python tests/edison/run.py \
  --binary build/edison-conformance/linux/x86_64/release/edison_conformance --test 460 \
  --output build/edison-rules-conformance-2026-11-04/focused-460

# Rebuild the matrix
python tests/edison/conformance/build_matrix.py \
  --certification build/edison-rules-conformance-2026-11-04/final-certification/report.json

# Certification
python tests/edison/run.py --output build/edison-rules-conformance-2026-11-04/final-certification
```

A failing fixture makes `run.py` exit 1 by design, as with `--test 241`. Numbers: 2xx rule differences, 3xx
errata gaps, 4xx per-card rulings. Certification ends at 083.

## Evidence

| Check | Result |
| --- | --- |
| Certification before this work | 544 PASS (513 + 10 + 2 + 4 + 3 + 4 + 8), identical replay traces |
| Certification after this work | 544 PASS, identical replay traces |
| Before versus after | All 102 source fingerprints, both binaries, and the traces are identical |
| Binary | `edison_rules` `9aa81bea...`; production module `c5ecba9c...`; both built in the worktree |
| Conformance run | 138 fixtures: 108 pass, 30 fail as recorded, 0 setup failures, 138 Rule 5 comparisons |
| Replay | Every fixture number ran twice with identical traces; no hang or crash |
| Tracked files changed | None. Added: `tests/edison/conformance/`, this report, and the report directory |

These binary hashes differ from the older archived ones. They come from this worktree's build, which the
fingerprint and hash comparison above covers only before and after this work.

## Weak existing fixtures

They pass and were not changed, but they prove less than their labels suggest. Found by a read-only audit agent;
I confirmed the first three in the code.

- **069B**: its "Mausoleum must consume normal summon" check is vacuous because the hand is already empty. 307
  now covers the Summon path.
- **067**: `rules.cpp:1016` hard-codes `historical=false` for `my_body_oppression`, so its Edison branches are dead.
- **072, 076**: every Swap Frog returns itself, so a same-copy repeat cannot be detected. 305 now covers this.
- **004A-C**: stop at chain confirmation, so they prove order only.
- **059, 073, 074**: the same body runs for the Rule 5 control, so they do not separate Edison from modern play.
- **073**: only one card is destroyed. 301 now covers "all cards".
- **047**: Treeborn is the only non-targeting case. 304A and 304B now add Rekindling and REDMD.

## What was not verified

- **Existing-fixture coverage** rests mainly on that audit agent's reading of the assertions. I read the
  fixtures behind Rule 1, 4 and 7 myself and spot-checked three claims. The rest I did not re-read.
- **Production path.** All fixtures use the native core. They share its scripts and patch, but none ran through
  the production Python module.
- **Fix sizes and causes** are estimates. No fix was attempted, so none is shown to work or to be safe.
- **Metagame sample.** The 75 decks are Format Library lists fetched by the pool-audit workstream, read from its
  build directory. They are skewed toward recent event lists. My own query to the deck-type endpoint timed out.
- **Sources not read:** the rulebook PDF and the linked external references. WebFetch was used once, for the
  `.net` SEGOC page; everything from `edisonformat.com` came from pages captured verbatim with curl and hashed
  in `source-snapshot.json`.
- **Decoder.** The conformance reader adds decoders for messages certification rejects. Rarely hit ones, such as
  counters, were not independently checked.

Fixture care matters here. During this work my own fixtures produced false results that looked like defects,
among them a Deck card that cannot be Special Summoned and a monster with more ATK than I assumed. They were
caught by adding positive controls, so a "not offered" result now generally requires proof that the card can be
offered in that fixture. Some remain weak, and the matrix says so: 219C and 257 cannot fully isolate their rule.
