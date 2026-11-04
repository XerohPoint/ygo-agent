# Edison Deck option labels

Prepared November 4, 2026, on `edison` at `0027cc9`, in an isolated worktree. Nothing is merged or pushed.

**Result: at a Deck prompt every option now shows the card the engine offered, its row shows the same card, and no other Deck
card is revealed.** On 1,536 games with the real round 1280 networks, the chosen label matched the summoned card on 1,356 of 1,356
Deck prompts. The old build, on the same seeds, got 34 of 1,217 (2.8%), against 20.5% by chance. Rule outcomes did not change: 3,376
label-blind soak duels match the old build in length, winner, reason, prompts and chosen actions. A new differential compared
1.29 million options with the engine's own prompts. It found 41,438 mismatches on the old build and 0 on the new one, all of them at
own Deck prompts. **Certification is 577 PASS, 0 FAIL. Old checkpoints still need retraining.** Evidence is in
[reports/deck-labels-2026-11-04](../tests/edison/reports/deck-labels-2026-11-04/).

## The cause and the change

For the selecting player's own Deck, the core sends 0, 1, 2 in prompt order in place of a sequence, to hide real positions
(`card::get_select_sequence`). `ygopro.h` used that number as a real position, so option k took its card id and row from the card at
Deck position k, and `card_hidden` unhid that card.

The change is `prompt_spec` in `ygopro.h`. For an own Deck candidate it ignores the masked number and binds the option by the prompt's
card code: the n-th candidate of a code takes the n-th Deck card of that code, so duplicates get distinct rows. Everything downstream
already works from that one spec string, so one change corrects the option's id and row, `card_hidden`, `decision_.candidates`,
`selected` and `mandatory`, `selection_`, `h_actions_` and lone options. The response is still the option's index in the prompt.
No shape, core patch, script, lock or training code changed.

**Real positions stay out of the observation.** Deck rows carry no position and visible rows lead the Deck in code order. Same-code
rows look identical, and copies pair with rows in one fixed order, so the pairing cannot say which copy sits higher. Option order is
unchanged, because it is the response index.

**Prompt order does not reflect Deck order, other than among copies of one code.** `card_operation_sort` orders own Deck candidates by
card type, Level and code, and only breaks a tie on sequence. Over the soak, 89,701 of 179,280 pairs of different codes (50.03%) had
the higher card first, and all 11,524 same-code pairs did. Those copies show identical labels, so nothing is exposed. Face-up Deck cards
and a reversed Deck's top card go first, and both are public.

## Audit

| Path | Deck cards here? | Found | Action |
| --- | --- | --- | --- |
| `MSG_SELECT_CARD`, plain and verbose | Yes, masked | The defect | Bound |
| `MSG_SELECT_UNSELECT_CARD`, both lists | Yes, one mask counter across both lists | The defect | Bound, with one binder per prompt |
| `decision_.candidates`, `selected`, `mandatory`; `_set_obs_decision`; `selection_` | Inherit the spec | Flags pointed at wrong rows | Corrected by the binding |
| `h_actions_` | Inherits `cid_` | Wrong id in history | Corrected by the binding |
| **Lone options**, `spec_to_card_id` and `get_card` near line 3022 | Yes | **Beyond the brief.** A lone candidate is answered with no observation, and history recorded the card at position k: 781 mismatches of 825 own Deck lone options | Corrected by the binding |
| `card_hidden` | Keyed by real spec | Unhid non-candidates: 22,522 rows over 5,396 prompts | Corrected by the binding |
| `MSG_SELECT_TRIBUTE`, `MSG_SELECT_SUM` | The core can mask them | No pool card reaches them with Deck cards, and 0 in the soak. Their verbose paths also called `get_card` with the mask | Bound and made safe. No fixture, since nothing reaches them |
| `MSG_SELECT_COUNTER` | Field cards only | No decision is made | Unchanged |
| `MSG_SORT_CARD` | Real sequence | Answered automatically outside verbose mode, with no observation | Unchanged |
| `MSG_CONFIRM_CARDS`, `revealed_`, `reveal_key` | Real sequence | Correct. Cleared on moves and shuffles | Unchanged |
| `MSG_CONFIRM_DECKTOP` | Real sequence | Skipped outside verbose mode, so reveals of the top of a Deck never reach an observation. See open points | Unchanged, flagged |
| `MSG_SELECT_CHAIN`, `MSG_SELECT_EFFECTYN`, idle and battle lists | Real location | Correct, and 0 Deck options in the soak | Unchanged |
| `MSG_SELECT_POSITION` | No row, code used directly | Correct | Unchanged, used as the independent check below |
| `PreserveSelectDeckSequence` | Real sequence | Three scripts use it, none in the pool. Binding by code still applies | Bound |
| `oppo_info=True`, `_set_obs_mask` | Same keys | Deck options correct on 12 duels | Corrected by the binding |
| The opponent's Deck, selecting player is not the controller | Real sequence, row hidden | No pool script selects from it, and 0 in the soak. See open points | Unchanged, with a guard fixture |

## The prompt differential

`prompt_audit.h` compares each option with the card code in the engine's own prompt. It is test-only: it runs only if
`EDISON_PROMPT_AUDIT` names a directory, and reads what the environment already parsed and wrote. The module carrying it matches the
certified `67423a99` byte for byte in whole observation traces, on 12 duels with the audit on and off. On the fixed build it matches
itself on 24. [no-effect-certified-module.json](../tests/edison/reports/deck-labels-2026-11-04/no-effect-certified-module.json),
[no-effect-new.json](../tests/edison/reports/deck-labels-2026-11-04/no-effect-new.json).

For each option it asserts: the id equals the prompt's code, the row shows that code, the row's zone and controller match, and no two
options of a card selection share a row. It also records lone options and own Deck rows the prompt does not account for. Face-down and
private opposing cards are exempt while hidden, and the check counts any that show an identity with no reveal. It covers
`MSG_SELECT_CARD`, `MSG_SELECT_UNSELECT_CARD`, `MSG_SELECT_TRIBUTE`, `MSG_SELECT_SUM`, `MSG_SELECT_CHAIN`, `MSG_SELECT_EFFECTYN`, and the
idle and battle lists. `MSG_SELECT_POSITION`, `MSG_SELECT_COUNTER` and `MSG_SORT_CARD` also carry codes, but offer no card row to compare.

Fresh seeds 80010000-80020999: all 21 lists, the 21 lists with whole Sides moved in, the 114 confirmed crash cards, and uniform decks.

| On 3,376 duels, 582,254 decisions | Old build | New build |
| --- | ---: | ---: |
| Label mismatches | 35,261 | **0** |
| Row mismatches | 35,261 | **0** |
| Lone option mismatches | 781 | **0** |
| Zone mismatches, shared rows, private cards shown | 0 | 0 |
| Own Deck rows shown beyond the prompt | 22,522 | **0** |
| Mismatch records in total | 41,438 | **0** |

**No mismatch appeared outside the own Deck on either build.** The prompts below carried 1,287,635 options, plus 50,944 lone options,
and every mismatch on the old build was at `select_card|deck|own` or `select_unselect_card|deck|own`. All counts are in
[differential-new.json](../tests/edison/reports/deck-labels-2026-11-04/differential-new.json) and
[differential-old.json](../tests/edison/reports/deck-labels-2026-11-04/differential-old.json).

| Message | Location, side | Decisions | Options | Hidden, exempt | Lone options |
| --- | --- | ---: | ---: | ---: | ---: |
| `select_card` | **deck, own** | 5,210 | 35,100 | 0 | 792 |
| | extra, own | 806 | 2,627 | 0 | 214 |
| | grave, own / opponent | 5,534 / 1,321 | 28,166 / 11,900 | 0 | 1,609 / 274 |
| | hand, own / opponent | 3,071 / 440 | 8,159 / 1,509 | 0 / 91 | 2,787 / 120 |
| | mzone, own / opponent | 8,690 / 23,133 | 18,498 / 61,234 | 0 / 5,016 | 7,650 / 17,356 |
| | szone, own / opponent | 2,182 / 3,044 | 4,261 / 7,032 | 0 / 5,446 | 368 / 817 |
| | removed, own / opponent | 288 / 110 | 954 / 289 | 0 | 102 / 14 |
| `select_unselect_card` | **deck, own** | 431 | 2,501 | 0 | 33 |
| | grave, hand | 531, 628 | 2,015, 1,408 | 0 | 240, 774 |
| | mzone, own / opponent | 859 / 77 | 1,850 / 113 | 0 / 5 | 452 / 46 |
| | szone, own / opponent | 26 / 1 | 68 / 2 | 0 | 10 / 12 |
| `select_tribute` | mzone, own / opponent | 8,136 / 32 | 19,005 / 32 | 0 / 7 | 3,890 / 44 |
| `select_sum` | mzone, own; hand, own | 226; 0 | 431; 0 | 0 | 1,854; 2 |
| `select_chain` | all zones | 39,284 | 42,913 | 0 | 11,360 |
| `select_effectyn` | all zones | 8,998 | 8,998 | 0 | 0 |
| `select_idlecmd` | all zones | 384,900 | 918,456 | 0 | 0 |
| `select_battlecmd` | all zones | 73,562 | 110,114 | 0 | 124 |

A decision counts once for each kind of option it shows, so the rows add up to more than the 447,577 decisions listing cards.

## Fixtures

Test 086 plays seeded duels through production until it reaches each prompt, and reads expected identities from the prompt's codes.
Every case also requires that visible Deck rows are exactly the candidates, in code order and ahead of the others, and that other Deck
rows are blank. Every option on a duel's prompts also passes the differential.

| Case | Asserts |
| --- | --- |
| 086A Substitoad | Every option is the Frog offered, and the pick is in history |
| 086B Swap Frog | Masked Deck options and real field options in one prompt: each shows its card and zone |
| 086C Duplicates | Copies of one code take distinct rows, ascending |
| 086D Non-candidate | Only candidates are visible |
| 086E Thunder Dragon | The first of two picks stays flagged, on a row showing it |
| 086F Advanced Ritual Art | Both lists of an unselect prompt from the Deck map to distinct rows |
| 086G Lone candidate | A lone option records the true card |
| 086H Arsenal Robber | The opponent selecting from their own Deck sees their own candidates |
| 086I Planted script | Selecting from the opponent's Deck: options stay blank on hidden rows and nothing is revealed. **The script is planted**, as no pool card does this |
| 086J Verbose parser | The verbose and plain parsers give byte-identical observations over 707 decisions |

### Negative control

| Run | Result |
| --- | --- |
| New fixtures, old logic with the audit (`ddcd756e...`) | 2 PASS, 8 FAIL. A to H fail on the defect. **086I and 086J pass on both builds**, being guards for behavior meant to stay the same |
| Old fixtures, new build | 1 FAIL of 567: 082D, explained below |

The fixtures need the audit's prompt data, so the old build here is the old logic plus the audit. That module's observations match the
certified module, as stated above. See [negative-control.json](../tests/edison/reports/deck-labels-2026-11-04/negative-control.json).

### The one changed pin: 082D

082D compares seeded frozen-policy games with a saved baseline, and it failed on `interactions`, which counts the labels each policy
was shown. Over all 8 games, steps, winner and win reason are identical. Only interaction counts changed, in 6 games. `first_divergence.py`
finds the first difference in each of those at a select-card prompt whose offered labels are the Deck label change: for example, the old
build showed Enemy Controller and Mystical Space Typhoon where the new one shows Substitoad twice. I regenerated
[frozen-policy-baseline.json](../tests/edison/reports/deck-labels-2026-11-04/frozen-policy-baseline.json) with `pin_frozen_policy.py`
and left the hardening baseline in place. **Please review this change.** No other fixture expectation changed. `test_learner.py` needed
no change.

## Rule 5 and rule outcomes

| Check | Result |
| --- | --- |
| Certification traces | All 567 earlier cases identical, including 245 `CONTROL` cases. Only the 10 new cases differ |
| Conformance traces | All 114 tests' files identical, and `results.json` identical |
| `edison_rules`, `edison_conformance` | Byte-identical to before. They do not compile `ygopro.h` |
| Soak duels on both builds | 3,376 of 3,376 same status, length, winner, reason, prompt counts and chosen actions. [soak-comparison.json](../tests/edison/reports/deck-labels-2026-11-04/soak-comparison.json) |
| Observation fields, 16,328 decisions over 64 duels, uniform policy | Global, non-Deck rows and non-Deck options never differ. Deck rows and options differ only at own Deck prompts. History differs afterward, as it records the pick. Selection flags did not differ on these duels. [field-comparison.json](../tests/edison/reports/deck-labels-2026-11-04/field-comparison.json) |

The soak's uniform and eager policies never read a label, which is what makes this comparison hold. Trace comparison is in
[trace-comparison.json](../tests/edison/reports/deck-labels-2026-11-04/trace-comparison.json).

## Label accuracy with the real networks

`tools/edison/learner/deck_label_check.py` plays the three round 1280 checkpoints greedily as Frog Monarch against the heuristic, seeds
80100000-80101535. The position prompt reads the summoned card from its own code, so this does not depend on the audit.

| Substitoad Deck prompts, 1,536 games | Old build | New build |
| --- | ---: | ---: |
| Chosen label matches the summoned card | 34 of 1,217 (2.79%) | **1,356 of 1,356 (100%)** |
| Chance | 20.5% | 20.4% |
| Labels that are Frogs | 25.1% | **100%** |
| Differential mismatches, this run's every prompt | 39,086 | **0** |

**`frog_line.py`'s count reads lower, and that is its definition.** It takes the last card prompt before the position prompt, which is
not always a Deck prompt. By that count the new build scores 96.12% (1,364 of 1,419). The 63 extra prompts account for all 55 misses.
I did not change it. Win rates were not collected as a result, because these networks trained on wrong labels.
[labels-new.json](../tests/edison/reports/deck-labels-2026-11-04/labels-new.json),
[labels-old.json](../tests/edison/reports/deck-labels-2026-11-04/labels-old.json).

## Gates

| Gate | Result |
| --- | --- |
| Baseline before the change | 567 PASS; `edison_rules` `6df1bd58...`, production `67423a99...`, as recorded |
| Certification | **577 PASS, 0 FAIL**: 513 + 10 + 2 + 4 + 3 + 4 + 8 + 4 + 19 + 10; identical replay traces |
| Conformance | 114 tests, 149 cases: 139 pass, 10 fail as recorded, 0 setup failures, 149 controls clean, no replay differences |
| Remaining failures | Unchanged: D6 261, 262B; D9 206; D11 216, 218, 219A, 219B, 219E, 223; D12 291 |
| `test_learner.py` | **13 of 13 pass, unchanged.** The identical-games check passes on 16 games. Its seeds are its own pinned development seeds |

Fresh soak seeds, final build, with the differential on:

| Stage | Seeds | Duels | Decisions | Crashes | Diagnostics | Contract violations |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 21 lists, Main and Extra | 80010000-503 | 504 | 113,362 | 0 | 0 | 0 |
| 21 lists, whole Side moved in | 80011000-503 | 504 | 110,840 | 0 | 0 | 0 |
| 114 confirmed crash cards | 80012000-3367 | 1,368 | 200,867 | 0 | 0 | 0 |
| Uniform decks, whole pool | 80020000-999 | 1,000 | 157,185 | 0 | 0 | 0 |

`soak.py`'s seed guard now also accepts the 80000000 block. Other seeds used: 80030000 for field comparison, 80040000 for `oppo_info`,
80050000 for divergence, 80100000 for networks, 80800000 for fixtures, 80900000 for the no-effect check.

## Hashes

| Item | Before | After |
| --- | --- | --- |
| Production module, built and imported | `67423a99...` | `b0f7f47f...`, reproduced by a forced rebuild |
| `edison_rules` | `6df1bd58...` | `6df1bd58...`, byte-identical |
| `edison_conformance` | `ac41e4d3...` | `ac41e4d3...`, byte-identical |
| Core package and locks | `r2` | Unchanged |

Certification fingerprints go from 105 to 108 sources: new `prompt_audit.h`, `production_deck_label_tests.py` and `prompt_audit.py`.
Changed: `ygopro.h`, `run.py`, `production_context_tests.py`, `soak_worker.py`. See
[hashes.json](../tests/edison/reports/deck-labels-2026-11-04/hashes.json).

## What a trained model would notice

**No checkpoint is invalidated by shape.** `actions_`, `cards_`, `h_actions_`, `decision_` and `selection_` keep their shapes. But:

- **Own Deck prompts mean something now.** The label is the true card. Networks trained on noise there, so what they learned about
  those options does not apply, and the effect on their play is not measured.
- **Visible Deck rows change.** They used to expose some hidden cards at the masked positions, and now only candidates show. Any policy
  that benefited from that exposure loses it.
- **History holds true cards** for own Deck picks, including lone options.
- **Seeded games differ once a policy reads labels.** On 160 seeded heuristic duels, 17 stay identical. The other 143 first differ at a
  select-card prompt, with the same number of options and differences only in rows and labels. After that, 82 change length and 11
  change winner. [first-divergence-summary.json](../tests/edison/reports/deck-labels-2026-11-04/first-divergence-summary.json).

All training and evaluation results to date belong to the defective interface. No training or win-rate study ran here.

## Not verified, and open points

- **Selecting from the opponent's Deck is blind, and its row order exposes position.** I found no pool script that does this, by a search
  of the scripts and by 0 such prompts in the soak. If one is ever added: the option shows no identity although the engine offered one,
  and because hidden opposing rows keep real order, the row pointer gives its real Deck position. Showing identities would change the
  contract that opposing Deck cards stay hidden (`observation_visibility.h`, asserted in `rules.cpp`), so I left it and guarded it with 086I.
- **`MSG_CONFIRM_DECKTOP` is skipped outside verbose mode**, so cards publicly revealed from the top of a Deck never reach an observation.
  This hides public information and leaks nothing. It predates this work.
- **Bindings with no card to exercise them:** Deck cards in `MSG_SELECT_TRIBUTE` and `MSG_SELECT_SUM`, `PreserveSelectDeckSequence`, and
  face-up Deck cards under a reversed Deck at a prompt. They are covered by source reading only.
- **Position independence is structural.** Fixtures assert rows lead the Deck in code order and copies pair with rows in fixed order, and
  the prompt order figure is measured. I did not build two duels with one state and different Deck orders and compare observations.
- **What the differential checks.** Identity, zone and controller, not a row's other columns. A policy's own Deck rows take statistics from
  the bound copy, which assumes Deck copies of a code have equal statistics. Soak policies never read labels. The network run covers a
  label-reading policy only for Frog Monarch and its opponents.
- **`oppo_info=True` is not used by production.** On 12 duels its Deck options are correct, and it shows 77 option rows with id 0 whose row
  shows an opposing face-down card, because that mode publishes all rows. I treated that as its design, and it is not changed.
- **`verbose` output.** `show_decision` now prints a bound Deck spec, which is a real position of a same-code copy. Prompt lines still print the
  masked number. Verbose is for reading, no policy sees it, and 086J covers only 707 decisions of the parser.
- **Tools not rebuilt:** `tools/edison/forward_model` and `pool_audit/build_audit_module.py` compile their own builds and have not picked this up.
  They already lag, as the trigger-order report notes.
- **`ygopro0` and `edopro` environments** were not audited. Edison does not use them.
- **Not checked:** whether the old networks gained anything from the exposed rows, and their strength on this build.

## Reproduce

```bash
export PATH="$HOME/.local/bin:$PATH" OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
W=$PWD   # this checkout. Use -P, or xmake walks up to a parent checkout.
xmake f -P "$W" -o "$W/build" -m release -y
xmake b -P "$W" -j 6 ygopro_ygoenv
xmake b -r -P "$W" -j 6 edison_rules   # It does not depend on ygopro.h, but run.py checks timestamps.
python tests/edison/run.py --output build/deck-labels/final-certification
python tests/edison/production_deck_label_tests.py --so OLD_MODULE      # Negative control
python tools/edison/pool_audit/build_codelist.py
python tools/edison/hardening/soak.py --stage a2 --per-pair 8 --seed 80010000 --output build/deck-labels/new-soak/lists --prompt-audit
python tests/edison/prompt_audit.py summarize build/deck-labels/new-soak/lists/prompt-audit
python tests/edison/prompt_audit.py compare-soak OLD_SOAK NEW_SOAK
python tests/edison/prompt_audit.py compare-fields OLD_MODULE NEW_MODULE --seed 80030000
python tools/edison/learner/deck_label_check.py --runs RUNS_DIR --output labels.json
```

The other soak stages use the seeds above, with `--targets tests/edison/reports/deck-labels-2026-11-04/crash-targets.json --per-target 12`
for the crash cards. Commit `3eeb789` is the old logic plus the audit, for building `OLD_MODULE`; its hash may shift if rebased.
