# Edison simulator hardening

Prepared November 4, 2026, on `edison` at `e4d382c`, in an isolated worktree. Nothing is committed.
Bounded certification is not exhaustive rules correctness. See [What remains untested](#what-remains-untested).

**Result: failures are now visible, the known interface crashes are gone from every soak, and the 13 omitted
cards are legal.** Certification is 567 PASS, 0 FAIL. Conformance is 142 cases: 129 pass and 13 fail as recorded.

Two findings differ from the conformance ranking, and D4 needs your decision:

- **D3 is not a simulator defect.** Fixtures 423 and 424 activated Call of the Haunted alone in the Draw Phase, so
  the Summon was Chain Link 1. With the fixtures corrected, both pass with no code change.
- **D4 was not changed, because the written rulings conflict.** See [D4](#d4-not-changed-your-decision).

**Update.** The owner has since ruled on D1 and D4. See [the trigger-order report](edison-trigger-order.md), which
supersedes the D1, D4 and known-failure statements here and the binary hashes below.

| Item | Status |
| --- | --- |
| A1 message handler | Done. Planted load, syntax and in-play errors all fail `run_worker()` |
| A2 sample across 21 lists | Done. 252 duels, 0 diagnostics |
| D2 Graveyard Ignition | Fixed |
| D4 Ignition after declined trigger | **Not changed**: sources conflict |
| D8 LP costs reaching 0 | Fixed, including all three maintenance cases |
| D10 0 ATK battle | Fixed |
| D5 Caius burn | Fixed |
| D3 Torrential and Bottomless | **No defect**: fixtures corrected |
| D7 end-of-turn discard triggers | Fixed. Fixture 281 also corrected |
| D6 returned triggers | **Not attempted** |
| C unhandled prompts | Done. No crash in 5,376 final-build duels. Two rare Lua errors found |
| D legality | Done. All 18 lists pass production legality |

## Failures are visible now

`init_module` installs a message handler. Each core or Lua diagnostic becomes one stderr line beginning
`[ygopro-core] Error:`, which `run_worker()`'s existing marker check rejects. Two silent paths in the environment
that kept running after a lookup failure now also print `Spec not found`, which the same check rejects.

Test 084 runs through `run_worker()` with an overlay script directory. A clean overlay completes. A Lua error at
card load, a syntax error, and a Lua error during play each reach stderr and raise `Native diagnostic detected`.
My first version planted the error in Kalut, which has a historical override, so the file never ran and the test
failed. The test now uses cards with no override.

## Rules fixes

All fixes apply only under Rule 1. Core changes are in the new `edison-hardening.patch`. Conformance moved from 108
of 138 passing to 129 of 142, with four new neighbor fixtures. Of the 138 original Rule 5 controls, the only three
whose output changed are 281, 423 and 424, whose fixtures I corrected. See
[conformance-before-after.json](../tests/edison/reports/hardening-2026-11-04/conformance-before-after.json).

| Defect | Change | Fixtures now passing | Neighbors checked |
| --- | --- | --- | --- |
| D2 | Summon-window Ignition block accepts a monster's effect in the hand, Graveyard or banished, not only on the field | 231, 238, 460A, 460B | New 460C declining passes to the opponent; 460D hand Ignition; 460E Black Garden still removes priority; 460F a Spell's Ignition-like effect stays out; 232, 233, 234, 236 |
| D8 | `check_lp_cost` requires cost below LP | 271A-C, 272, 273, 274 | Each activation case has a control one LP above the cost |
| D10 | Equal 0 ATK in Attack Position destroys both | 296 | 297: 0 ATK into 0 DEF destroys neither |
| D5 | Caius override checks `GetLeaveFieldDest` before banishing | 441A, 447 | 415, 441B, 441C, 446; existing Caius fixtures |
| D7 | Discarded cards' optional triggers are dropped. In that Chain only negating Spell Speed 2 and any Spell Speed 3 effects are offered | 281, 282 | 283 Doomcaliber still chains; 284 no window without a trigger |

Sources: D2 Rule 6 and the Summons page; D8 Rule 10; D10 Rule 13; D5 the Caius ruling "DO NOT inflict damage if the
DARK Monster is banished by a Condition instead"; D7 Rule 11.

Limits of these fixes:

- **D2 goes beyond its fixtures.** The fixtures cover the Graveyard. I also allowed the hand and banished zones,
  because the rule says Ignition Effects without limiting location. 460D tests the hand. The banished zone has no
  fixture. I excluded Spell and Trap effects because the rulings call them "Ignition-like", which is my reading.
- **D7's filter stays on for the rest of the End Phase**, using the core's existing `hand_adjusted` flag.
- **Passing fixtures stayed in the conformance suite.** They use a different harness and executable, so moving them
  is not straightforward. Nothing was promoted except the new production tests below.

### D3: fixture error, no defect

`p.chain(call_haunted)` takes the first offer of that card, which came in the Draw Phase. The trace showed Call as
Chain Link 1, so offering Bottomless afterward was correct. I changed 423, 424 and 425 to chain Call only in response
to their first card, and added a setup check that Call is Chain Link 2. All three then pass under both rules.

The core already moves these events to `used_event` when the next link starts (`break_effect` in `solve_chain`).
This also settles conformance decision 2, about fixing D3 under Rule 5: there is nothing to fix.

The same hazard could affect other fixtures using `p.chain()` on a Set free-chain card. I did not audit them.

### D4: not changed, your decision

The conformance report calls this a defect from Rule 6 Example 3, whose text is not in any local capture. Two
individual rulings saved by the pool audit point the other way:

- **Swap Frog**: "This effect can be activated with Ignition Effect Priority in response to this card's Summon if the
  (1) optional Trigger Effect isn't activated."
- **Alien Dog**: "If you wish to use this effect, you CANNOT use Ignition Effect Priority in response to the Summon",
  which implies the choice exists when the effect is not used.

Current behavior matches both rulings. Fixture 235A stays recorded as failing. I did not fetch the rule page, given
the no-downloads constraint. **Decision needed:** read Example 3. If it covers only an accepted trigger, 235A is wrong
and should change. If it covers a declined trigger, it conflicts with those two rulings, as D1's sources conflict.

### D6: not attempted

The source says only "suggest" and "we can surmise" (ambiguity A8). The fix would also need a rule separating Dandylion,
which should activate, from Aslla Piscu, which should not (263, passing). I judged a core change there too risky for that
evidence.

### Fixture 082D re-pinned

The D2 fix made certification test 082D fail. It compares seeded frozen-policy games against the November 2 archive,
and the new offer changes those games. Per your instruction, I stopped and compared against the written source: Rule 6 and the
Summons page support the fix, and 082D is a seeded pin with no rules content.

[first-divergence-summary.json](../tests/edison/reports/hardening-2026-11-04/first-divergence-summary.json) compares the pre-fix
module with the final one on 52 fixture duels. 48 have identical full state hashes. In the other 4, the first
difference is Plaguespreader Zombie newly offered in the Summon window. On that evidence I replaced the baseline with
[frozen-policy-baseline.json](../tests/edison/reports/hardening-2026-11-04/frozen-policy-baseline.json), generated by
`tools/edison/hardening/pin_frozen_policy.py`. **Please review this change.** Test 075's override inventory also gained Caius.

## Prompts

Every class in the audit's table is handled. Production regressions cover all of them except counters on more than 2
cards, unweighted `min != max` Tributes and description id 10. Only the soak could reach those, and I did not confirm
that it did: the audit attributed the last two to no specific card.

| Class | Handling | Test |
| --- | --- | --- |
| `toss_coin`, `toss_dice` | Public result, consumed | 085A, 085B (Snipe Hunter) |
| Spirit return (1104) and other description ids | 123 ids appended after existing ones, so existing values keep their numbers | 085C (Dark Dust Spirit), 085D (Susa Soldier) |
| `field_disabled`, two zones | Consumed; zones picked one decision at a time, each excluded from the next | 085E |
| Declare a card | See below | 085F, 085G, 085Q, 085R |
| Call a coin (60) | Description id | 085H |
| `announce_race` | New message id 17; Type in the number column | 085I |
| Declare a kind (70) | Description id | 085J |
| Number over 12 | Low byte in column 9, high byte in column 10 | 085K (Mausoleum) |
| Double Tribute, `min != max` | Sets that reach the minimum with no spare card; unweighted prompts use the existing selection | 085L |
| `deck_top`, `reverse_deck` | Consumed | 085M |
| Two Attributes | One decision at a time | 085N |
| `cancel_target` | Consumed | 085O |
| 1213, 1214 | Description ids | 085P |
| Counters on more than 2 cards | Taken in listed order, with no decision, as the 2-card case already was | Soak only |
| Others seen in core output | `swap_grave_deck`, `hand_res`, `match_kill`, `confirm_extratop`, `show_hint` consumed | Soak only, where reached |

Each test case also runs the heuristic, context and saved learner policies on every decision, and each must return a
legal row. 085S loads all 18 lists through production.

### Declare a card: action shape

Listing every name would pass the action bound, since the pool closure holds about 4,000 cards. **Shape:** at most 16
rows per decision, which is the smallest capacity in use.

- Up to 16 candidates: one row per card, identical to the old rows.
- Above that: rows are pages. A page row shows its first card, and its size in the number column. A card row has 0 there.
- About 4,000 names take three decisions. Choosing a page calls no core function.
- Candidates are the cards in the code list and legal pool that pass the card's opcodes, ordered by kind and then name.

The list depends only on the code list and the card's opcodes, so it holds no hidden information. 085R shows the same
first page across different decks and seeds. The opcode check ports the core's unexported `is_declarable`. A
disagreement returns `MSG_RETRY`, which aborts loudly, and none occurred.

This keeps every legal name reachable and every row within bounds. The cost is that alphabetical pages mean little to a
policy, so a learner may want a better encoding later. With a small code list, such as the 67-card agent list, the
offered names reveal which cards exist in that list.

### Soak results

Seeds are in the new 74000000 block. A search of `docs`, `tools` and `tests/edison/*.py` found no earlier use, only card
codes that happen to start with those digits. Final-build runs:

| Stage | Seeds | Duels | Decisions | Crashes | Diagnostics | Contract violations | Pre-fix crashes |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| 21 lists, Main and Extra | 74010000-503 | 504 | 114,755 | 0 | 0 | 0 | 0 of 1,296 (18 lists) |
| 21 lists, whole Side moved in | 74011000-503 | 504 | 109,745 | 0 | 0 | 0 | 120 of 1,296 (18 lists) |
| 114 confirmed crash cards | 74012000-3367 | 1,368 | 198,514 | 0 | 0 | 0 | 342 of 2,666 (dedicated stages) |
| Uniform decks, whole pool | 74020000-22999 | 3,000 | 465,291 | 0 | 1 | 0 | 1,519 of 3,000 |

Largest offered set: 63. The worker now also checks the candidate-row and `context[8..9]` contracts the audit's port
omitted. Earlier development runs: A2 at 74000000-251, targets at 74001000-1683, uniform at 74002000-2999, which found the
second diagnostic below. The pre-fix figures come from the audit and used different seeds, so they are not paired.

### Remaining crash classes and diagnostics

**No crash class was observed.** These still abort or fail closed if reached, and none was seen in any soak:
Select Card or Tribute with minimum 0, Select Sum modes other than 0 or more than 2 required cards, an unknown
effect-yes-no description, and `reload_field` or `tag_swap` messages. Rock-paper-scissors is unhandled, but its only
user found, Transmission Gear, is outside the pool.

**The handler found two real Lua errors**, each once in 4,000 uniform duels. Both are upstream scripts calling a nil
function, and neither card is in a gauntlet list. I did not fix or suppress them, so a duel that reaches one fails its
worker:

| Card | Line | Error |
| --- | --- | --- |
| Pyro Clock of Destiny | `c1082946.lua:24` | `attempt to call a nil value (local 'op')` |
| Destiny HERO - Diamond Dude | `c13093792.lua:65` | `attempt to call a nil value (local 'fop')`. Big Evolution Pill is a likely trigger, since it has no operation, but that is unconfirmed |

## Legality

The 13 cards are in `assets/edison/legality.json` with a set and date in `additions`, and the notes describe the pool
authority. The header is regenerated and agrees. The restriction list is unchanged at 135. Pool entries are now 3,672.
085S confirms `gigavise.ydk`, `machina_control.ydk` and the other 16 pass unedited through production `init_module`.

The set and date for each card come from the YGOPRODeck listing the audit saved. Format Library's per-card dates, from your
earlier check, were not archived, and I did not refetch them. Only Gottoms is proven in real lists. Reasons a card might still be excluded
were not checked, so the authority still awaits the owner's review.

## What a trained model would notice

**No checkpoint is invalidated by shape.** `actions_`, `cards_`, `decision_` and `selection_` keep their shapes, and
`ygoai/` is unchanged. 48 of 52 fixture duels above have identical full state hashes, and test 083 passes. Saved models load and
play, but they learned a different game in these ways:

- **New rows:** a monster's Graveyard or hand Ignition Effect in the Summon window. The turn player gets a chain prompt
  where the game previously moved on. In fixture decks only Plaguespreader was affected, in 4 of 52 duels.
- **Removed rows:** costs equal to current LP. Maintenance at exact LP now self-destructs instead of losing the duel.
- **Outcomes:** 0 ATK into 0 ATK destroys both. Caius deals no burn on Condition banishes. Discarded cards' optional
  triggers stop at the end-of-turn discard.
- **New values in existing columns:** message id 17, effect ids 91-213, and column 10 in number and declaration rows.
  `edison_policy.py` caps message features at 16, so 17 shares that slot.
- **Decisions without a matching core prompt:** declarations, multi-zone and multi-value prompts take several steps.

Fixture win rates from earlier studies are not comparable to this simulator. The audit's full code list also gained 14
lines, and because ids are line numbers, any saved work keyed to that list shifts. `agent_codes.txt` is unchanged.

## Certification and reproduction

| Check | Result |
| --- | --- |
| Baseline in this worktree | 544 PASS; production module hash equals main's `c5ecba9c...` |
| Final certification | **567 PASS, 0 FAIL**: 513 + 10 + 2 + 4 + 3 + 4 + 8, plus new 084 (4) and 085 (19); identical replay traces |
| Test 083 | 8 PASS, identical replay |
| Conformance | 113 tests, 142 cases, 129 pass, 13 fail as recorded, 0 setup failures, 142 controls clean, no replay differences |
| Fingerprints | 105 sources, up from 102 |
| Binaries | `edison_rules` `d7a6be97...`; production `038e6dd1...`; core `e8978744...` |

Known failures remaining: D1 241, 242; D4 235A; D6 261, 262B; D9 206; D11 216, 218, 219A, 219B, 219E, 223; D12 291.

```bash
export PATH="$HOME/.local/bin:$PATH" OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
W=$PWD   # this checkout. Use -P, or xmake walks up to a parent checkout.
xmake f -P "$W" -o "$W/build" -m release -y
xmake b -P "$W" -j 6 ygopro_ygoenv
xmake b -P "$W" -j 6 edison_rules
(cd tests/edison/conformance && xmake f -P . -o ../../../build/edison-conformance -m release -y && xmake b -P . -j 6)
python tests/edison/run.py --output build/hardening/final-certification
python tests/edison/run.py --test 083 --output build/hardening/final-test-083
python tests/edison/conformance/run_conformance.py --output build/hardening/final-conformance
python tools/edison/pool_audit/build_codelist.py
python tools/edison/hardening/soak.py --stage a2 --per-pair 8 --seed 74010000 --output build/hardening/final-lists
```

A worktree also needs `scripts/script` linked to the scripts checkout, and `assets/locale/en` copied from the main
checkout. Both are ignored by git. Use main's `.venv` Python; the tests put this checkout's `ygoenv` first on the path.
After a core patch change, bump `edison_hardening` and rebuild with `xmake b -r`. An ordinary build does not relink.

### Core package

The package hash covers configs, not patch contents, so changing `early-trigger.patch` in place could have overwritten the
pinned variant. Instead, the new `edison_hardening` string config installs side by side, and its value must be bumped whenever
`edison-hardening.patch` changes. `run.py` checks the patch hash against the lock.

| Variant | Library sha256 | State |
| --- | --- | --- |
| `bedb42dd...`, pinned | `def13f39...` | Byte-identical; `references.txt` also unchanged |
| `aa797b55...`, `edison_hardening = "r1"` | `e8978744...` | New, reproduced by a clean reinstall |
| `5fe29f6a...`, `edison_hardening = "r2"` | `e3adf73d...` | See [the trigger-order report](edison-trigger-order.md) |
| `792b98d4...`, `edison_hardening = "r3"` | `984301c4...` | See [the Dark Panther report](edison-dark-panther.md) |
| `0548bcc8...`, `edison_hardening = "r4"` | `5ac42d87...` | Never merged: it crashed copied Doom Lord returns. See [the Gold Sarcophagus report](edison-gold-sarcophagus.md) |
| `5334b050...`, `edison_hardening = "r5"` | `4fd0ffdc...` | Current. See [the Gold Sarcophagus report](edison-gold-sarcophagus.md) |

Main's `.xmake` and `build/linux` file digest is unchanged. Evidence:
[before](../tests/edison/reports/hardening-2026-11-04/baseline-before.txt),
[after](../tests/edison/reports/hardening-2026-11-04/baseline-after.txt).

**The search spike and pool audit tools still link `bedb42dd`**, so they do not get these rule fixes.
`tools/edison/forward_model/build.sh` was left unchanged, with a note in its report. `build_audit_module.py` has the same pin.

## Mapping for separate commits

Nothing is committed. To land these separately:

| Change | Files |
| --- | --- |
| A | `ygopro.h` handler and `Spec not found` lines; `agent_match.py` `script_root`; `production_diagnostic_tests.py`; `run.py` |
| Core packaging | `repo/packages/y/ygopro-core/xmake.lua`, `xmake.lua`, `tests/edison/conformance/xmake.lua`, `update_locks.py` |
| D2, D8, D10, D7 | `edison-hardening.patch`; `known_failures.json`; 460C-F in `ruling_tests.h`, `cards.h`; 281 in `rule_tests.h`; `production_context_tests.py` and `pin_frozen_policy.py` |
| D5 | `historical_scripts.h`; `script_consistency_tests.h` |
| D3 | 423-425 in `ruling_tests.h`; `known_failures.json` |
| C | Remaining `ygopro.h`; `production_prompt_tests.py`; `tools/edison/hardening/soak.py`, `scan_prompts.py`; `soak_worker.py` |
| D | `legality.json`, `edison_legality_data.h`, lock files, `add_pool_cards.py`, 085S |

## What remains untested

- **Rule correctness of the new prompts.** Tests show prompts complete and contracts hold. They do not check that
  Mausoleum, Susa Soldier or any declaration card behaves correctly in a full ruling sense.
- **Rule 6, 10, 11 and 13 page text.** I relied on the fixtures' and matrix's descriptions. Only individual ruling pages were
  available locally. This is why D4 stays open.
- **The conformance matrix** still records November 4 statuses. Rebuilding needs page captures this worktree lacks.
- **D2 from the banished zone, and Ignition-like Spell effects beyond Blaze Accelerator.**
- **Information-only prompts** are proven only by the card activating and the duel finishing, plus the audit's record
  that they used to crash.
- **Counter removal stays automatic**, so that player choice is not offered.
- **The same fixture flaw as D3** may exist in other conformance fixtures, and any of them could be wrong in either direction.
- **Trained-policy strength after these changes.** No training ran, as instructed.
- **Most of the pool's rulings.** Certification and conformance stay bounded samples.
