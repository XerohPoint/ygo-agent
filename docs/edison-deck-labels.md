# Edison Deck option labels

Prepared November 4, 2026, on `edison` at `0027cc9`, in an isolated worktree, and revised after an independent review of `045c1d0`.
Nothing is merged or pushed. Current evidence is in [reports/deck-labels-2026-11-04/review](../tests/edison/reports/deck-labels-2026-11-04/review/).
The folder above it holds the first round's.

**Result: at a Deck prompt every option now shows the card the engine offered, its row shows the same card, no further Deck card is
revealed, and what a policy sees does not depend on Deck order in any scenario tested.** With the real round 1280 networks, the chosen
label matched the summoned card on 1,384 of 1,384 Deck prompts. The old logic, on the same seeds, got 45 of 1,372 (3.3%), against about
21% by chance. Rule outcomes did not change: 3,376 label-blind soak duels match the old logic in length, winner, reason, prompts and
chosen actions. A differential compared 1.28 million options with the engine's own prompts: 42,248 mismatches on the old logic, 0 on this
build. **Certification is 585 PASS, 0 FAIL. Old checkpoints still need retraining.**

## The cause and the change

For the selecting player's own Deck, the core sends 0, 1, 2 in prompt order in place of a sequence, to hide real positions
(`card::get_select_sequence`). `ygopro.h` used that number as a real position, so option k took its card id and row from the card at
Deck position k, and `card_hidden` unhid that card.

The change is `prompt_spec` in `ygopro.h`. Everything downstream works from the one spec string it returns, so it corrects the option's id
and row, `card_hidden`, `decision_.candidates`, `selected` and `mandatory`, `selection_`, `h_actions_` and lone options. The response is
still the option's index in the prompt. No shape, core patch, script, lock or training code changed.

| Candidate | Bound to |
| --- | --- |
| Own Deck | A Deck card of the prompt's code. Among copies: one with the prompt's face state, then one this player has already been shown. Tribute and Sum carry no position, so face state decides nothing there |
| Opposing Deck, revealed to this player | Its real sequence, as the row shows the card anyway |
| Opposing Deck, hidden | The next blank row of that Deck, whatever its real sequence |

**Real positions stay out of the observation.** Deck rows carry no position, and visible rows lead the Deck in code order. Among copies of
a code, rows follow binding order (`deck_bind_rank_`), so the pairing of options with rows says nothing about which copy sits higher.
Option order is unchanged, because it is the response index.

**Prompt order does not reflect Deck order, other than among copies of one code.** `card_operation_sort` orders own Deck candidates by card
type, Level and code, and only breaks a tie on sequence. Over the soak, 90,184 of 180,500 pairs of different codes (49.96%) had the higher
card first, and all 11,451 same-code pairs did. Those copies show identical labels, so nothing is exposed. Face-up Deck cards and a
reversed Deck's top card go first, and both are public.

## What the review found

I checked each point before changing anything. All five held.

| Point | Finding | Evidence on `045c1d0` | Now |
| --- | --- | --- | --- |
| A | Binding took the first copy, so with a copy visible through `revealed_` it unhid another, and bound face-up candidates to face-down copies. The audit skipped revealed rows first, so it counted 0 | 086K: row 3 visible with 2 candidates. 086L: `face_bad`. 087E: extra rows | Binding as above. The audit charges per code the larger of the candidate and reveal counts, and compares face state |
| B | A hidden opposing Deck option's row showed its real position | 086I: rows 61, 59, 50 for sequences 20, 18, 9. 087D: `actions_` and `selection_` differ | Bound to the next blank row. The audit counts any other pointer |
| C | `selected` and `mandatory` were never compared with the prompt, and history accepted any recent identical copy | A mutant keeping masked specs for selected cards fails 086F with "A selected card shares a row with an option" | Compared with prompt codes, included in the shared-row check, exact rows asserted. History must show the pick as a new newest entry |
| D | `prompt_audit.py` returned 0 on failure and on no data | See below | Nonzero on any mismatch, differing traces, or coverage below a stated minimum |
| E | `oppo_info=True` leaks positions | Reading the code, and the 087 probe relies on it | Documented below |

**A and B are not reached by the pool in the soak.** Binding from `045c1d0` shows 0 mismatches over 3,376 duels with this stronger audit, and
its traces match this build's exactly. By source reading A is reachable in the pool: Dark Tinker and Maharaghi call `ConfirmCards` on a
Deck card, and with the card left on top, a later Chain Link's Deck prompt sees the reveal with no move between. Fixtures use planted scripts
because they reach this every time. I found no pool script for B, by pattern search only.

## `oppo_info=True` must not be used for Edison play

`_set_obs_mask` and `_set_obs_g_cards` keep physical Deck order, so that mode's rows, pointers and mask expose real Deck positions. It also
shows hidden cards by design. This is a leak and it stays in the code. Edison evaluation and training use `oppo_info=False`, and only
`scripts/battle.py` exposes the option, with False as its default. Test 087's probe reads Deck order through this mode, and only to set up
its lists.

## Audit

| Path | Deck cards here? | Found | Action |
| --- | --- | --- | --- |
| `MSG_SELECT_CARD`, plain and verbose | Yes, masked | The defect | Bound |
| `MSG_SELECT_UNSELECT_CARD`, both lists | Yes, one mask counter across both lists | The defect | Bound, with one binder per prompt |
| `decision_.candidates`, `selected`, `mandatory`; `_set_obs_decision`; `selection_` | Inherit the spec | Flags pointed at wrong rows: 989 of 11,116 selected cards on the old logic | Corrected by the binding |
| `h_actions_` | Inherits `cid_` | Wrong id in history | Corrected by the binding |
| **Lone options**, `spec_to_card_id` and `get_card` | Yes | A lone candidate is answered with no observation, and history recorded the card at position k: 805 of 854 own Deck lone options | Corrected by the binding |
| `card_hidden` | Keyed by real spec | Unhid non-candidates: 22,636 rows over 5,375 prompts | Corrected by the binding |
| `MSG_SELECT_TRIBUTE`, `MSG_SELECT_SUM` | The core can mask them | No pool card reaches them with Deck cards, and 0 in the soak. Their verbose paths also called `get_card` with the mask | Bound and made safe. No fixture, since nothing reaches them |
| `MSG_SELECT_COUNTER` | Field cards only | No decision is made | Unchanged |
| `MSG_SORT_CARD` | Real sequence | Answered automatically outside verbose mode, with no observation | Unchanged |
| `MSG_CONFIRM_CARDS`, `revealed_`, `reveal_key` | Real sequence | Correct. Cleared on moves and shuffles | Unchanged |
| `MSG_CONFIRM_DECKTOP` | Real sequence | Skipped outside verbose mode, so reveals of the top of a Deck never reach an observation. See open points | Unchanged, flagged |
| `MSG_SELECT_CHAIN`, `MSG_SELECT_EFFECTYN`, idle and battle lists | Real location | Correct. Own Deck rows here are visible and in code order, and 0 Deck options in the soak | Unchanged |
| `MSG_SELECT_POSITION` | No row, code used directly | Correct | Unchanged, used as the independent check below |
| `PreserveSelectDeckSequence` | Real sequence | Three scripts use it, none in the pool | Bound by code |

## The prompt differential

`prompt_audit.h` compares each option with the card code in the engine's own prompt. It is test-only: it runs only if `EDISON_PROMPT_AUDIT`
names a directory, and reads what the environment already parsed and wrote. With the audit added, the old logic matches the certified
`67423a99`, and the `045c1d0` binding matches a true `045c1d0` build, in every observation field on 9,009 and 8,764 decisions. On this build,
audit on and off match on 24 duels. It covers `MSG_SELECT_CARD`, `MSG_SELECT_UNSELECT_CARD`, `MSG_SELECT_TRIBUTE`, `MSG_SELECT_SUM`,
`MSG_SELECT_CHAIN`, `MSG_SELECT_EFFECTYN`, and the idle and battle lists. `MSG_SELECT_POSITION`, `MSG_SELECT_COUNTER` and `MSG_SORT_CARD` also
carry codes, but offer no card row to compare.

For each option it asserts that the id equals the prompt's code, the row shows that code, the zone, controller and face state match, and no
two different cards of a selection share a row. Selected and mandatory cards get the same checks, plus their flags. It also records lone
options, hidden opposing pointers, and own Deck rows the prompt does not account for. Face-down and private opposing cards are exempt while
hidden, and any that show an identity with no reveal are counted.

Fresh seeds 80210000-80220999: all 21 lists, the 21 lists with whole Sides moved in, the 114 confirmed crash cards, and uniform decks.

| On 3,376 duels, 577,610 decisions | Old logic | `045c1d0` binding | This build |
| --- | ---: | ---: | ---: |
| Label mismatches | 35,079 | 0 | **0** |
| Row mismatches | 35,079 | 0 | **0** |
| Lone option mismatches | 805 | 0 | **0** |
| Selected or mandatory mismatches, of 11,116 | 989 | 0 | **0** |
| Own Deck rows shown beyond the prompt | 22,636 | 0 | **0** |
| Zone, face, shared row, private card, hidden pointer | 0 | 0 | 0 |
| Mismatch records in total | 42,248 | 0 | **0** |

**No mismatch appeared outside the own Deck.** The prompts carried 1,276,811 options, plus 50,710 lone options. Every mismatch was at
`select_card|deck|own` or `select_unselect_card|deck|own`, which also holds the selected-card ones. Full counts by message, location and side are
in [differential-new.json](../tests/edison/reports/deck-labels-2026-11-04/review/differential-new.json).

| Message, this build | Decisions | Options | Hidden, exempt | Lone options |
| --- | ---: | ---: | ---: | ---: |
| `select_card`: deck, own | 5,108 | 34,769 | 0 | 809 |
| `select_card`: all | 53,340 | 175,804 | 10,697 | 31,930 |
| `select_unselect_card`: deck, own | 510 | 2,576 | 0 | 45 |
| `select_unselect_card`: all | 2,551 | 7,869 | 17 | 1,543 |
| `select_tribute` | 8,218 | 19,184 | 10 | 3,832 |
| `select_sum` | 239 | 470 | 0 | 1,801 |
| `select_chain` | 39,110 | 42,876 | 0 | 11,478 |
| `select_effectyn` | 8,869 | 8,869 | 0 | 0 |
| `select_idlecmd` | 380,478 | 911,476 | 0 | 0 |
| `select_battlecmd` | 73,860 | 110,263 | 0 | 126 |

### The audit commands now fail closed

| Command, on the first round's evidence | Exit |
| --- | ---: |
| `summarize`, old build | 1: 41,438 mismatch records |
| `summarize`, new build | 0 |
| `summarize`, empty or missing directory | 1 |
| `summarize --min-duels 100000` | 1 |
| `compare-soak` | 0 |
| `compare-soak`, empty | 1 |
| `compare_traces.py` | 0 |

This round's exit codes are in [exit-codes.json](../tests/edison/reports/deck-labels-2026-11-04/review/exit-codes.json). `soak.py --prompt-audit` and
`deck_label_check.py` follow the same rule: the old logic exits 1 on all four soak stages and on the label check. 086M tests these verdicts, including
differing traces and a comparison that reached no Deck prompt.

## Hidden Deck order: the metamorphic test

Test 087 is the direct test of position independence. Two duels share one seed, so one random stream deals by one permutation of list positions.
A probe with 40 distinct cards shows which list positions land in each hand and in the top 8 of each Deck. The two duels keep those positions and
reverse the other 27 or so, so public state and hands match while the Deck beneath is in another order. Every first-turn decision must give
byte-identical `cards_`, `global_`, `actions_`, `h_actions_`, `decision_` and `selection_`, and the differential must show different true sequences at
a prompt, or the test proves nothing. Two seeds per scenario:

| Case | Scenario | Own / opposing Deck prompts | Prompts with different true sequences | This build | `045c1d0` binding | Old logic |
| --- | --- | ---: | ---: | --- | --- | --- |
| 087A | Substitoad, then Swap Frog's Deck-or-field prompt | 4 / 0 | 8 | PASS | PASS | FAIL |
| 087B | Two picks from the Deck | 4 / 0 | 4 | PASS | PASS | FAIL |
| 087C | Unselect list from the Deck | 4 / 0 | 6 | PASS | PASS | FAIL |
| 087D | Selecting from the opponent's Deck, planted | 0 / 2 | 4 | PASS | **FAIL** | FAIL |
| 087E | A copy already revealed, planted | 2 / 0 | 4 | PASS | **FAIL** | FAIL |

So for these scenarios, observation bytes do not depend on Deck order. The limit is the first turn, since the next draw exposes a different order through
ordinary play. See open points.

## Fixtures

Test 086 plays seeded duels through production until it reaches each prompt, and reads expected identities from the prompt's codes. Every case also requires
visible Deck rows to be exactly the candidates, selected cards and allowed reveals, in code order and ahead of the others, and other Deck rows to be blank.
Every prompt in those duels also passes the differential.

| Case | Asserts | `045c1d0` binding | Old logic |
| --- | --- | --- | --- |
| 086A Substitoad | Every option is the Frog offered, and the pick is the newest new history entry | PASS | FAIL |
| 086B Swap Frog | Masked Deck options and real field options in one prompt: each shows its card and zone | PASS | FAIL |
| 086C Duplicates | Copies of one code take distinct rows, ascending | PASS | FAIL |
| 086D Non-candidate | Only candidates are visible | PASS | FAIL |
| 086E Thunder Dragon | The first of two picks is flagged on the exact row showing it | PASS | FAIL |
| 086F Advanced Ritual Art | Both lists of an unselect prompt from the Deck map to the exact distinct rows | PASS, **FAIL on the selected mutant** | FAIL |
| 086G Lone candidate | A lone option is the newest history entry, as the true card | PASS | FAIL |
| 086H Arsenal Robber | The opponent selecting from their own Deck sees their own candidates | PASS | FAIL |
| 086I Planted script | Selecting from the opponent's Deck: blank options on the first blank rows, nothing revealed, and the candidate goes to the Graveyard | **FAIL** | FAIL |
| 086K Planted reveal | A revealed copy binds first, so no extra copy shows | **FAIL** | FAIL |
| 086L Planted `ReverseInDeck` | A face-up candidate binds to a face-up copy | **FAIL** | FAIL |
| 086J Verbose parser | The verbose and plain parsers give byte-identical observations | PASS | PASS |
| 086M Audit verdicts | Bad and empty reports are rejected | PASS | PASS |

Totals: this build 13 PASS. `045c1d0` binding 10 PASS, 3 FAIL. Old logic 2 PASS, 11 FAIL. The selected mutant 12 PASS, 1 FAIL. 087 on that mutant passes all
5, so 086F is what catches it.

The fixtures need the audit's prompt data, so controls are built with this audit. Old logic is `prompt_spec` forced to the old lookup (`b1ac8aca...`), and the `045c1d0`
binding (`2b2686ab...`) is that commit's header plus this audit. Neither is committed. Their equivalence to the real builds is stated above.

**082D stays as re-pinned in the first round.** It passes here with no change. The re-pin needs review: its `interactions` counts record the labels shown, in 6 of 8 games, and
steps, winner and win reason are identical in all 8. `test_learner.py` needed no change.

## Rule 5 and rule outcomes

| Check | Result |
| --- | --- |
| Certification traces | All 567 cases that predate this work identical to `0027cc9`, including 245 `CONTROL` cases |
| Conformance traces | All 114 tests' files identical, and `results.json` identical |
| `edison_rules`, `edison_conformance` | Byte-identical to before. They do not compile `ygopro.h` |
| Soak duels, old logic and this build | 3,376 of 3,376 same status, length, winner, reason, prompt counts and chosen actions |
| Soak duels, `045c1d0` binding and this build | The same, and all 3,376 label traces identical |
| Fields against the certified module, 15,489 decisions over 64 duels, uniform policy | Global, non-Deck rows and non-Deck options never differ. Deck rows and options differ only at own Deck prompts (244 decisions). History differs afterward. Selection flags did not differ |
| Fields against the `045c1d0` binding, 16,758 decisions | No field differs. A and B are not reached here |

The soak's uniform and eager policies never read a label, which is what makes this comparison hold.

## Label accuracy with the real networks

`tools/edison/learner/deck_label_check.py` plays the three round 1280 checkpoints greedily as Frog Monarch against the heuristic, seeds 80300000-80301535. The
position prompt reads the summoned card from its own code, so this does not depend on the audit.

| Substitoad Deck prompts, 1,536 games | Old logic | This build |
| --- | ---: | ---: |
| Chosen label matches the summoned card | 45 of 1,372 (3.28%) | **1,384 of 1,384 (100%)** |
| Chance | 21.4% | 20.5% |
| Labels that are Frogs | 24.0% | **100%** |
| Differential mismatches, this run's every prompt | 39,184 | **0** |

`frog_line.py`'s count reads lower by definition: it takes the last card prompt before the position prompt, which is not always a Deck prompt. By that count this
build scores 95.81% (1,395 of 1,456), and the 72 extra prompts account for all 61 misses. Win rates were not collected as a result, because these networks trained on wrong labels.

## Gates

All on this module, `9c3edea3...`.

| Gate | Result |
| --- | --- |
| Certification | **585 PASS, 0 FAIL**: 513 + 10 + 2 + 4 + 3 + 4 + 8 + 4 + 19 + 13 + 5; identical replay traces |
| Conformance | 114 tests, 149 cases: 139 pass, 10 fail as recorded, 0 setup failures, 149 controls clean, no replay differences |
| Remaining failures | Unchanged: D6 261, 262B; D9 206; D11 216, 218, 219A, 219B, 219E, 223; D12 291 |
| `test_learner.py` | **13 of 13 pass, unchanged.** The identical-games check passes on 16 games. Its seeds are its own pinned development seeds |

Fresh soak seeds, with the differential on and failing the stage on any mismatch:

| Stage | Seeds | Duels | Decisions | Crashes | Diagnostics | Contract violations |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 21 lists, Main and Extra | 80210000-503 | 504 | 112,443 | 0 | 0 | 0 |
| 21 lists, whole Side moved in | 80211000-503 | 504 | 106,184 | 0 | 0 | 0 |
| 114 confirmed crash cards | 80212000-3367 | 1,368 | 202,226 | 0 | 0 | 0 |
| Uniform decks, whole pool | 80220000-999 | 1,000 | 156,757 | 0 | 0 | 0 |

Other seeds: 80230000-80233000 for field comparison, 80240000 for the no-effect check, 80300000 for networks, 80810000 for 086, 80820000 and 80821000 for 087. `soak.py`'s
guard accepts the 80000000 block.

## Hashes

| Item | Before any fix | `045c1d0` | This commit |
| --- | --- | --- | --- |
| Production module, built and imported | `67423a99...` | `b0f7f47f...` | `9c3edea3...`, reproduced by two forced rebuilds |
| `edison_rules` | `6df1bd58...` | Same | Same |
| `edison_conformance` | `ac41e4d3...` | Same | Same |
| Core package and locks | `r2` | Unchanged | Unchanged |

Certification fingerprints are at 109 sources. New since `045c1d0`: `production_deck_order_tests.py`. See
[hashes.json](../tests/edison/reports/deck-labels-2026-11-04/review/hashes.json).

**A build trap I hit.** After restoring a header with `cp`, a plain `xmake b` kept the previous binary: it reported success in 0.2 s and still held the mutant (`10723c32...`).
`run.py`'s timestamp check would have accepted it. I caught it from the hash. After swapping headers, use `xmake b -r` and read the hash.

## What a trained model would notice

**No checkpoint is invalidated by shape.** `actions_`, `cards_`, `h_actions_`, `decision_` and `selection_` keep their shapes. But:

- **Own Deck prompts mean something now.** The label is the true card. Networks trained on noise there, so what they learned about those options does not apply, and the effect on
  their play is not measured.
- **Visible Deck rows change.** They used to expose some hidden cards at the masked positions, and now only candidates show. Any policy that benefited from that exposure loses it.
- **History holds true cards** for own Deck picks, including lone options.
- **Seeded games differ once a policy reads labels.** On 160 seeded heuristic duels in the first round, 17 stay identical. The other 143 first differ at a select-card prompt, with the
  same number of options and differences only in rows and labels. After that, 82 change length and 11 change winner. This commit adds no difference to those 3,376 soak duels.

All training and evaluation results to date belong to the defective interface. No training or win-rate study ran here.

## Not verified, and open points

- **Position independence past the first turn, and beyond these scenarios.** 087 covers five scenarios, two seeds and one alternative order each, with seat 0 first to act. Later turns diverge through
  draws, so a same-state comparison needs a way to fix the draw. Prompts other than these, such as a Deck card in a chain list, are covered by reading the code.
- **Selecting from the opponent's Deck stays blind.** Options show no identity although the engine offered one. Showing identities would change the contract that opposing Deck cards stay hidden
  (`observation_visibility.h`, asserted in `rules.cpp`), so it needs the owner. Whether a pool script does this was checked by pattern search and 0 prompts in the soak, not proven.
- **Chain and yes-no prompts keep real specs.** If an opposing hidden Deck card ever appeared in one, its pointer would show position. None appears in the soak, and a card's own effect would not normally
  offer that. Only the card-selection prompts go through `prompt_spec`.
- **`MSG_CONFIRM_DECKTOP` is skipped outside verbose mode**, so cards publicly revealed from the top of a Deck never reach an observation. This hides public information and leaks nothing. It predates this work.
- **A revealed copy may stand in for a candidate.** In 086K the option binds to the copy already shown, though the engine's candidate is another copy of the code. Rows look the same and the response is the index, so
  nothing observable differs. Copies whose in-Deck statistics differ would break this assumption, as they would the first round's. Both soak policies and 087 saw none.
- **Bindings with no card to exercise them:** Deck cards in `MSG_SELECT_TRIBUTE` and `MSG_SELECT_SUM`, `PreserveSelectDeckSequence`, and face-up Deck cards under a reversed Deck at a prompt. They are covered by source reading only.
- **Pool reachability of A** is by source reading. The soak did not reach it, and no fixture uses a pool card for it.
- **What the differential checks.** Identity, zone, controller and face state, not a row's other columns. Soak policies never read labels. The network run covers a label-reading policy only for Frog Monarch and its opponents.
- **`verbose` output.** `show_decision` now prints a bound Deck spec, which is a real position of a same-code copy or a blank opposing row. Prompt lines still print the prompt's number. Verbose is for reading, and no policy sees it.
- **Tools not rebuilt:** `tools/edison/forward_model` and `pool_audit/build_audit_module.py` compile their own builds and have not picked this up. They already lag, as the trigger-order report notes.
- **`ygopro0` and `edopro` environments** were not audited. Edison does not use them.
- **Not checked:** whether the old networks gained anything from the exposed rows, and their strength on this build.

## Reproduce

```bash
export PATH="$HOME/.local/bin:$PATH" OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
W=$PWD   # this checkout. Use -P, or xmake walks up to a parent checkout.
xmake f -P "$W" -o "$W/build" -m release -y
xmake b -r -P "$W" -j 6 ygopro_ygoenv
xmake b -r -P "$W" -j 6 edison_rules   # It does not depend on ygopro.h, but run.py checks timestamps.
python tests/edison/run.py --output build/labels/certification
python tests/edison/production_deck_label_tests.py --so CONTROL_MODULE  # Negative controls
python tests/edison/production_deck_order_tests.py --so CONTROL_MODULE
python tools/edison/pool_audit/build_codelist.py
python tools/edison/hardening/soak.py --stage a2 --per-pair 8 --seed 80210000 --output build/labels/new-soak/lists --prompt-audit
python tests/edison/prompt_audit.py summarize build/labels/new-soak/lists/prompt-audit
python tests/edison/prompt_audit.py compare-soak OLD_SOAK NEW_SOAK
python tests/edison/prompt_audit.py compare-fields OLD_MODULE NEW_MODULE --seed 80230000
python tools/edison/learner/deck_label_check.py --runs RUNS_DIR --seed 80300000 --output labels.json
```

The other soak stages use the seeds above, with `--targets tests/edison/reports/deck-labels-2026-11-04/crash-targets.json --per-target 12` for the crash cards. To build the old-logic control, make `prompt_spec`
return `ls_to_spec(loc, seq, pos, controller != player)` at its top. For the `045c1d0` control, take that commit's `prompt_spec` and binder, and keep this audit.
