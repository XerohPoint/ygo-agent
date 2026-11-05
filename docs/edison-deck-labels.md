# Edison Deck option labels

Prepared November 4, 2026, on `edison` at `0027cc9`, in an isolated worktree, and revised after independent reviews by Sol (`gpt-6.1-sol`). Merged to local `edison` and recertified there; not pushed.
Current evidence is in [reports/deck-labels-2026-11-04/review2](../tests/edison/reports/deck-labels-2026-11-04/review2/). The folders `review` and its parent hold earlier rounds.

**Result: at an own Deck prompt every option shows the card the engine offered, its row shows the same card, no further Deck card is revealed, and what a policy
sees does not depend on Deck order in any scenario tested. Prompts that cannot show a Deck card safely now void the game.** With the real round 1280 networks, the
chosen label matched the summoned card on 1,313 of 1,313 Deck prompts. The old logic, on the same seeds, got 47 of 1,258 (3.7%), against about 21% by chance. Rule
outcomes did not change: 3,376 label-blind soak duels match the old logic in length, winner, reason, prompts and chosen actions, and none voided. A differential
compared 1.27 million options with the engine's own prompts: 40,778 mismatches on the old logic, 0 on this build. **Certification is 593 PASS, 0 FAIL. One soak stage
reports a Lua error in Morale Boost, unrelated to this work. Old checkpoints still need retraining.**

## The cause and the change

For the selecting player's own Deck, the core sends 0, 1, 2 in prompt order in place of a sequence, to hide real positions (`card::get_select_sequence`). `ygopro.h` used
that number as a real position, so option k took its card id and row from the card at Deck position k, and `card_hidden` unhid that card.

The change is `prompt_spec` in `ygopro.h`, for `MSG_SELECT_CARD` and `MSG_SELECT_UNSELECT_CARD`. Everything downstream works from the one spec string it returns, so it
corrects the option's id and row, `card_hidden`, `decision_.candidates` and `selected`, `selection_`, `h_actions_` and lone options. The response is still the option's
index in the prompt. No shape, core patch, script, lock or training code changed.

An own Deck candidate binds to a Deck card of the prompt's code. Among copies it takes one with the prompt's face state, then one this player has already been shown.

**Real positions stay out of the observation.** Deck rows carry no position, and visible rows lead the Deck in code order. Among copies of a code, rows follow binding order
(`deck_bind_rank_`), so the pairing of options with rows says nothing about which copy sits higher. Option order is unchanged, because it is the response index.

**Prompt order does not reflect Deck order, other than among copies of one code.** `card_operation_sort` orders own Deck candidates by card type, Level and code, and only
breaks a tie on sequence. Over the soak, 87,927 of 176,776 pairs of different codes (49.74%) had the higher card first, and all 11,349 same-code pairs did. Those copies show
identical labels, so nothing is exposed.

## Prompts that now void

These throw, the worker aborts, and the actor returns `VOID` with no duels counted. No pool card is known to produce them.

| Prompt | Why it cannot show the card safely |
| --- | --- |
| An opposing Deck card in `MSG_SELECT_CARD`, `MSG_SELECT_UNSELECT_CARD`, `MSG_SELECT_TRIBUTE`, `MSG_SELECT_SUM`, `MSG_SELECT_CHAIN`, `MSG_SELECT_EFFECTYN` | Real sequence arrives with it, and the core sorts by it. The review showed option index leaking position when revealed and hidden candidates mix, and chain and yes-no rows showing it directly |
| An own Deck card in `MSG_SELECT_TRIBUTE`, `MSG_SELECT_SUM` | These prompts carry no position. The review reproduced a Sum option pointing at a face-up non-candidate |

Test 088 plants a script for each prompt on Heavy Storm and plays it through a real `actor.py` worker. Each result is `VOID`, `worker died or timed out (exit -6)`, no duels, and the log
names the prompt. A clean overlay completes 32 duels on the same cell. The throw is not swallowed and does not hang: it reaches `std::terminate` on the env thread. A cell voids whole, as
for other contract failures.

| Case | Prompt | This build | `5152981` |
| --- | --- | --- | --- |
| 088Z | Clean control | PASS, 32 duels | PASS |
| 088A | Opposing Deck, card prompt | VOID | FAIL: cell counted |
| 088B | Opposing Deck, one card revealed | VOID | FAIL: cell counted |
| 088C | Own and opposing Deck in one prompt | VOID | FAIL: cell counted |
| 088D | Opposing Deck, unselect prompt | VOID | FAIL: cell counted |
| 088E | Opposing Deck, Sum prompt | VOID | FAIL: aborts, with no marker |
| 088F | Own Deck, Sum prompt | VOID | FAIL: aborts, with no marker |
| 088G | Opposing Deck, chain prompt | VOID | FAIL: cell counted |
| 088H | Opposing Deck, effect yes-no prompt | VOID | FAIL: cell counted |

`MSG_SELECT_TRIBUTE` has no case. By reading the core, its candidates come from monsters on the field and in hand, so no script can put a Deck card there. Its throw is unexercised.

**On the pool: 0 such voids.** All 3,376 soak duels finished with 0 crashes. A pattern search of pool scripts agrees: Spell Chronicle, Drastic Drop Off and Inferno Reckless Summon have an opponent
selecting, but from banished cards, added cards and that player's own Deck. Crush Card Virus and Painful Choice are limit 0. Advanced Ritual Art's Deck materials use `SelectSubGroup`, which is the
unselect prompt, and Crop Circles' sum is over field cards. That search is a lead list and not a proof, so a card it missed would void its cell loudly.

## `oppo_info=True` must not be used for Edison play

`_set_obs_mask` and `_set_obs_g_cards` keep physical Deck order, so that mode's rows, pointers and mask expose real Deck positions. It also shows hidden cards by design. This is a leak and it stays
in the code. Edison evaluation and training use `oppo_info=False`, and only `scripts/battle.py` exposes the option, with False as its default. Test 087's probe reads Deck order through this mode, and
only to set up its lists.

## What the reviews found

Every point held when tested. I did not separately re-test the first item's index numbers, as those prompts now void.

| Round | Point | Finding | Now |
| --- | --- | --- | --- |
| 1 | A | Binding took the first copy, so with a copy visible through `revealed_` it unhid another, and bound face-up candidates to face-down copies | Binding as above |
| 1 | B | A hidden opposing Deck option's row showed its real position | Voids |
| 1 | C | `selected` and `mandatory` were never compared with the prompt | Compared, see 2.8 |
| 1 | D | `prompt_audit.py` returned 0 on failure and on no data | Fails closed |
| 1 | E | `oppo_info=True` leaks positions | Documented |
| 2 | 1-4 | Next-blank-row binding still leaked by option index, and through chain and yes-no prompts. Sum binding could point at a face-up non-candidate | Those prompts void. That binding, `audit_hidden_pointers`, Tribute and Sum binding and `prompt_card` are deleted |
| 2 | 5 | `FACE_DOWN_ROW` was wrong, so 086L's assertion could not fire | The check is in `check_decision` and compares with a blank row of the same observation. The face mutant fails on it: "A candidate binds to a copy with the other face state" |
| 2 | 6 | `verdict_summary` ignored counters C++ can bump with no record. `soak.py` called zero duels clean. `compare_traces.py` passed a new failing case. `deck_label_check.py` had its own weaker verdict | All fixed and in 086M |
| 2 | 7 | 087 read `core.select_cards` at any prompt, so stale lists counted as moved Deck prompts: 087A reported 8, and truly 4. A truncated run returned quietly | Read only at own Deck listing prompts. Truncation raises |
| 2 | 8 | `audit_selected` looked up the expected card through the bound spec, so a bad spec naming a candidate validated itself | Compared with the prompt's raw entries in prompt order, and a selected spec still on offer fails. The selected mutant now gives 473 `selected_mismatch` in the C++ differential over 504 duels |
| 2 | 9 | `max(candidates, reveals)` called a revealed face-up copy plus two face-down candidates extra | Counted per code and face state. Fixture 086N |

**One earlier statement was wrong.** I wrote that the pool does not reach point A. The stronger audit shows the `045c1d0` binding with 2 face mismatches on Parasite Paracide (seed 80423243), which shuffles
itself into a Deck face-up. So the face-state preference is needed on the pool. This build shows 0. The reveal half of A was still not reached in the soak.

## Audit

| Path | Deck cards here? | Found | Action |
| --- | --- | --- | --- |
| `MSG_SELECT_CARD`, plain and verbose | Own masked, opposing real | The defect | Own bound, opposing voids |
| `MSG_SELECT_UNSELECT_CARD`, both lists | Own masked, one counter across both lists | The defect | Own bound, opposing voids |
| `decision_.candidates`, `selected`; `_set_obs_decision`; `selection_` | Inherit the spec | Flags on wrong rows: 820 of 11,255 selected cards on the old logic | Corrected by the binding |
| `h_actions_` | Inherits `cid_` | Wrong id in history | Corrected by the binding |
| **Lone options**, `spec_to_card_id` and `get_card` | Yes | A lone candidate is answered with no observation, and history recorded the card at position k: 760 of 815 own Deck lone options | Corrected by the binding |
| `card_hidden` | Keyed by real spec | Unhid non-candidates: 21,768 rows over 5,221 prompts | Corrected by the binding |
| `MSG_SELECT_TRIBUTE`, `MSG_SELECT_SUM` | The core can mask them | No pool card reaches them with Deck cards, and 0 in the soak | Void |
| `MSG_SELECT_CHAIN`, `MSG_SELECT_EFFECTYN` | Real location | An own Deck option is correct and visible. An opposing one showed its row | Opposing voids |
| Idle and battle lists | Own cards, real location | Correct, 0 Deck options in the soak | Unchanged |
| `MSG_SELECT_COUNTER` | Field cards only | No decision is made | Unchanged |
| `MSG_SORT_CARD` | Real sequence | Answered automatically outside verbose mode, with no observation | Unchanged |
| `MSG_CONFIRM_CARDS`, `revealed_`, `reveal_key` | Real sequence | Correct. Cleared on moves and shuffles | Unchanged |
| `MSG_CONFIRM_DECKTOP` | Real sequence | Skipped outside verbose mode, so reveals of the top of a Deck never reach an observation. See open points | Unchanged, flagged |
| `MSG_SELECT_POSITION` | No row, code used directly | Correct | Unchanged, used as the independent check below |
| `PreserveSelectDeckSequence` | Real sequence | Three scripts use it, none in the pool | Bound by code |

## The prompt differential

`prompt_audit.h` compares each option with the card code in the engine's own prompt. It is test-only: it runs only if `EDISON_PROMPT_AUDIT` names a directory, and reads what the environment already
parsed and wrote. On this build, audit on and off match on 24 duels. The old-logic control matches certified `67423a99` in every observation field on 8,350 decisions.

For each option it asserts that the id equals the prompt's code, the row shows that code, the zone, controller and face state match, and no two different cards of a selection share a row. Selected
and mandatory cards are compared with the prompt's raw entries in prompt order, with their flags, and may not still be on offer. It also records lone options and own Deck rows the prompt does not
account for, per code and face state. Face-down and private opposing cards are exempt while hidden, and any that show an identity with no reveal are counted. `MSG_SELECT_POSITION`, `MSG_SELECT_COUNTER`
and `MSG_SORT_CARD` also carry codes, but offer no card row to compare.

Fresh seeds 80420000-80430999: all 21 lists, the 21 lists with whole Sides moved in, the 114 confirmed crash cards, and uniform decks.

| On 3,376 duels, 578,573 decisions | Old logic | `045c1d0` binding | This build |
| --- | ---: | ---: | ---: |
| Label mismatches | 33,977 | 0 | **0** |
| Row mismatches | 33,977 | 0 | **0** |
| Lone option mismatches | 760 | 0 | **0** |
| Selected or mandatory mismatches, of 11,255 | 820 | 0 | **0** |
| Face mismatches | 6 | 2 | **0** |
| Own Deck rows shown beyond the prompt | 21,768 | 0 | **0** |
| Zone, shared row, private card | 0 | 0 | 0 |
| Mismatch records in total | 40,778 | 2 | **0** |

**No mismatch appeared outside the own Deck.** The prompts carried 1,274,024 options, plus 50,443 lone options. Full counts by message, location and side are in
[differential-new.json](../tests/edison/reports/deck-labels-2026-11-04/review2/differential-new.json).

| Message, this build | Decisions | Options | Hidden, exempt | Lone options |
| --- | ---: | ---: | ---: | ---: |
| `select_card`: deck, own | 4,994 | 33,865 | 0 | 760 |
| `select_card`: all | 53,927 | 177,432 | 10,921 | 31,758 |
| `select_unselect_card`: deck, own | 473 | 2,429 | 0 | 55 |
| `select_unselect_card`: all | 2,534 | 7,735 | 12 | 1,598 |
| `select_tribute` | 8,240 | 18,880 | 10 | 3,953 |
| `select_sum` | 224 | 446 | 0 | 1,835 |
| `select_chain` | 39,395 | 43,075 | 0 | 11,187 |
| `select_effectyn` | 8,718 | 8,718 | 0 | 0 |
| `select_idlecmd` | 380,421 | 908,310 | 0 | 0 |
| `select_battlecmd` | 73,649 | 109,428 | 0 | 112 |

### The audit commands fail closed

`summarize` counts every failure counter, and fails on empty or thin coverage. `no-effect`, `compare-soak`, `compare-fields`, `compare_traces.py`, `soak.py` and `deck_label_check.py` all exit nonzero on failure.
`deck_label_check.py` uses the common verdict and requires every game audited. `soak.py` does not call zero duels clean. `compare_traces.py` fails on an earlier trace that changed, and on a new case with `RESULT: FAIL`.
086M tests each path. This round: `differential-new` 0, `differential-binding` 1, `differential-old` 1, `differential-selected-mutant` 1, all comparisons 0, labels on this build 0, labels on the old logic 1.

## Hidden Deck order: the metamorphic test

Test 087 is the direct test of position independence. Two duels share one seed, so one random stream deals by one permutation of list positions. A probe with 40 distinct cards shows which list positions land
in each hand and in the top 8 of each Deck. The two duels keep those positions and reverse the other 27 or so, so public state and hands match while the Deck beneath is in another order. Every first-turn decision must
give byte-identical `cards_`, `global_`, `actions_`, `h_actions_`, `decision_` and `selection_`, and the true sequences at an own Deck prompt must differ, or the test proves nothing. Two seeds per scenario:

| Case | Scenario | Own Deck prompts | Prompts with different true sequences | This build | `045c1d0` binding | Old logic |
| --- | --- | ---: | ---: | --- | --- | --- |
| 087A | Substitoad, then Swap Frog's Deck-or-field prompt | 4 | 4 | PASS | PASS | FAIL |
| 087B | Two picks from the Deck | 4 | 4 | PASS | PASS | FAIL |
| 087C | Unselect list from the Deck | 4 | 4 | PASS | PASS | FAIL |
| 087E | A copy already revealed, planted | 2 | 2 | PASS | **FAIL** | FAIL |

So for these scenarios, observation bytes do not depend on Deck order. The limit is the first turn, since the next draw exposes a different order through ordinary play. See open points.

## Fixtures

Test 086 plays seeded duels through production until it reaches each prompt, and reads expected identities from the prompt's codes. Every case also requires visible Deck rows to be exactly the candidates, selected cards and
allowed reveals, in code order and ahead of the others, with matching face state, and other Deck rows to be blank. Every prompt in those duels also passes the differential.

| Case | Asserts | Fails on |
| --- | --- | --- |
| 086A Substitoad | Every option is the Frog offered, and the pick is the newest new history entry | Old logic |
| 086B Swap Frog | Masked Deck options and real field options in one prompt: each shows its card and zone | Old logic |
| 086C Duplicates | Copies of one code take distinct rows, ascending | Old logic |
| 086D Non-candidate | Only candidates are visible | Old logic |
| 086E Thunder Dragon | The first of two picks is flagged on the exact row showing it | Old logic |
| 086F Advanced Ritual Art | Both lists of an unselect prompt from the Deck map to the exact distinct rows | Old logic, selected mutant |
| 086G Lone candidate | A lone option is the newest history entry, as the true card | Old logic |
| 086H Arsenal Robber | The opponent selecting from their own Deck sees their own candidates | Old logic |
| 086K Planted reveal | A revealed copy binds first, so no extra copy shows | Old logic, `045c1d0` binding |
| 086L Planted `ReverseInDeck` | A face-up candidate binds to a face-up copy | Old logic, `045c1d0` binding, face mutant |
| 086N Planted reveal and candidates | A revealed face-up copy and two face-down candidates all show, and that is not extra | Old logic, face mutant |
| 086J Verbose parser | The verbose and plain parsers give byte-identical observations | None, a guard |
| 086M Audit verdicts | Bad and empty reports are rejected | None, a guard |

Former 086I and 087D, which asserted blank pointers on the opposing Deck, are replaced by 088.

| Build | 086 | 087 | 088 |
| --- | --- | --- | --- |
| This build, `b8fcd2bc...` | 13 PASS | 4 PASS | 9 PASS |
| Old logic, `8b3e867e...` | 2 PASS, 11 FAIL | 0 PASS, 4 FAIL | |
| `045c1d0` binding, `d3ed5ca8...` | 11 PASS, 2 FAIL (K, L) | 3 PASS, 1 FAIL (E) | |
| Face mutant, `f15b3cc6...` | 11 PASS, 2 FAIL (L, N) | 4 PASS | |
| Selected mutant, `a1f22f7e...` | 12 PASS, 1 FAIL (F) | 4 PASS | |
| `5152981`, `9c3edea3...` | | | 1 PASS, 8 FAIL |

Fixtures need this audit's prompt data, so the controls and mutants carry it. They are built from this commit's header with one edit each, and are not committed. On `5152981`, 088E and 088F abort for some other reason that names no
prompt. I did not look further, since this build voids first.

**082D stays as re-pinned in the first round.** It passes here with no change. The re-pin needs review: its `interactions` counts record the labels shown, in 6 of 8 games, and steps, winner and win reason are identical in all 8. `test_learner.py` needed no change.

## Rule 5 and rule outcomes

| Check | Result |
| --- | --- |
| Certification traces | All 567 cases that predate this work identical to `0027cc9`, including 245 `CONTROL` cases |
| Conformance traces | All 114 tests' files identical, and `results.json` identical |
| `edison_rules`, `edison_conformance` | Byte-identical to before. They do not compile `ygopro.h` |
| Soak duels, old logic and this build | 3,376 of 3,376 same status, length, winner, reason, prompt counts and chosen actions |
| Soak duels, `045c1d0` binding and this build | The same, and all 3,376 label traces identical |
| Fields against the certified module, 17,147 decisions over 64 duels, uniform policy | Global, non-Deck rows and non-Deck options never differ. Deck rows and options differ only at own Deck prompts (243 decisions). History differs afterward. Selection flags did not differ |
| Fields against `5152981`, 17,627 decisions | No field differs |

The soak's uniform and eager policies never read a label, which is what makes this comparison hold.

## Label accuracy with the real networks

`tools/edison/learner/deck_label_check.py` plays the three round 1280 checkpoints greedily as Frog Monarch against the heuristic, seeds 80500000-80501535. The position prompt reads the summoned card from its own code, so this does not depend on the audit.

| Substitoad Deck prompts, 1,536 games | Old logic | This build |
| --- | ---: | ---: |
| Chosen label matches the summoned card | 47 of 1,258 (3.74%) | **1,313 of 1,313 (100%)** |
| Chance | 21.4% | 20.8% |
| Labels that are Frogs | 25.6% | **100%** |
| Differential mismatches, this run's every prompt | 39,357 | **0** |

`frog_line.py`'s count reads lower by definition: it takes the last card prompt before the position prompt, which is not always a Deck prompt. By that count this build scores 95.04% (1,321 of 1,390), and the 77 extra prompts account for all 69 misses. Win rates were
not collected as a result, because these networks trained on wrong labels.

## Gates

All on this module, `b8fcd2bc...`.

| Gate | Result |
| --- | --- |
| Certification | **593 PASS, 0 FAIL**: 513 + 10 + 2 + 4 + 3 + 4 + 8 + 4 + 19 + 13 + 4 + 9; identical replay traces |
| Conformance | 114 tests, 149 cases: 139 pass, 10 fail as recorded, 0 setup failures, 149 controls clean, no replay differences |
| Remaining failures | Unchanged: D6 261, 262B; D9 206; D11 216, 218, 219A, 219B, 219E, 223; D12 291 |
| `test_learner.py` | **13 of 13 pass, unchanged.** The identical-games check passes on 16 games. Its seeds are its own pinned development seeds |

Fresh soak seeds, with the differential on and failing the stage on any mismatch:

| Stage | Seeds | Duels | Decisions | Crashes, voids | Diagnostics | Contract violations |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 21 lists, Main and Extra | 80420000-503 | 504 | 114,866 | 0 | 0 | 0 |
| 21 lists, whole Side moved in | 80421000-503 | 504 | 110,070 | 0 | 0 | 0 |
| 114 confirmed crash cards | 80422000-3367 | 1,368 | 196,874 | 0 | 0 | 0 |
| Uniform decks, whole pool | 80430000-999 | 1,000 | 156,763 | 0 | **1** | 0 |

**The uniform stage exits 1 on a real Lua error**, not from this work: Morale Boost, `c93671934.lua:30`, `attempt to index a nil value`. It appears identically on the old logic and the `045c1d0` binding, on the same seeds. It joins the two upstream script errors the hardening report found. The card is in
no gauntlet list, and a duel that reaches it fails its worker. I did not fix or suppress it.

Other seeds: 80410000 for 088, 80440000-80442000 for field comparison, 80450000 for the no-effect check, 80460000 for the selected mutant, 80500000 for networks, 80810000 for 086, 80820000 and 80821000 for 087.

## Hashes

| Item | Before any fix | `045c1d0` | `5152981` | This commit |
| --- | --- | --- | --- | --- |
| Production module, built and imported | `67423a99...` | `b0f7f47f...` | `9c3edea3...` | `b8fcd2bc...`, reproduced by two forced rebuilds |
| `edison_rules` | `6df1bd58...` | Same | Same | Same |
| `edison_conformance` | `ac41e4d3...` | Same | Same | Same |
| Core package and locks | `r2` | Unchanged | Unchanged | Unchanged |

Certification fingerprints are at 110 sources. See [hashes.json](../tests/edison/reports/deck-labels-2026-11-04/review2/hashes.json).

**A build trap.** After restoring a header with `cp`, a plain `xmake b` can keep the previous binary: it reported success in 0.2 s and still held a mutant. `run.py`'s timestamp check would accept it. After swapping headers, use `xmake b -r` and read the hash.

## What a trained model would notice

**No checkpoint is invalidated by shape.** `actions_`, `cards_`, `h_actions_`, `decision_` and `selection_` keep their shapes. But:

- **Own Deck prompts mean something now.** The label is the true card. Networks trained on noise there, so what they learned about those options does not apply, and the effect on their play is not measured.
- **Visible Deck rows change.** They used to expose some hidden cards at the masked positions, and now only candidates show. Any policy that benefited from that exposure loses it.
- **History holds true cards** for own Deck picks, including lone options.
- **Seeded games differ once a policy reads labels.** On 160 seeded heuristic duels in the first round, 17 stay identical. The other 143 first differ at a select-card prompt, with the same number of options and differences only in rows and labels. After that, 82 change length and 11 change winner.
  The later commits add no difference on the soak's duels.

All training and evaluation results to date belong to the defective interface. No training or win-rate study ran here.

## Merge to `edison`

Sol's final check of `983af26` found no new defect reachable with a legal Edison deck, and recommended merging for validated play with `oppo_info=False`. It ran 086, 087 and 088 and negative controls, not the full suites. Certification on main then caught what the worktree's run did not:

- **Test 088 was flaky.** One of two runs read exit `None` rather than `-6`, because the reply pipe can close just before the dying worker is reaped. The cell voided correctly either way. `Worker.run` in `actor.py` now waits up to 5 s for the real status. 088 then passed 6 of 6 repeats under CPU load, 54 of 54 cases.
- **`compare_traces.py` accepted a crashed new case,** as Sol reported. A new case now needs a completed `RESULT: PASS`. I checked crash, pass and fail on synthetic input.

Final gates on main, module `b8fcd2bc...`, matching the reviewed build: certification 593 PASS, 0 FAIL, identical replay traces, `edison_rules` `6df1bd58...`, 110 sources; conformance as recorded; `test_learner.py` 13 of 13. See [merge-gates.json](../tests/edison/reports/deck-labels-2026-11-04/merge-gates.json).

Supervisor's check, independent of the audit code: after a random policy picks an own Deck option, does that card reach the player's hand, field, Graveyard or banished zone within their next decisions? On identical choices, the old build gave 15.2% and 16.2% on two seed blocks, and the final build gave 1,624 of 1,624 (block 80980000). My first version of this check compared labels with rows, and it passed on the old build too, so it was discarded. Misses on intermediate builds were Equip Spells that also appear in Power Tool Dragon's pick-three prompts, and, on one block, cases that fit an opponent's Trap Dustshoot or Phoenix Wing Wind Blast.

**Left open by decision, below the merge bar:**

- **The selected-card audit accepts two same-code cards aliased to one row.** Sol reproduced this in a unit harness. No binder path is known to produce it, and fixtures assert distinct rows.
- **Painful Choice** (74191942) offers own Deck cards to the opponent, which would void. It is limit 0, and the loader rejected a deck containing it, so no legal deck reaches it. This corrects the wider "no pool card" wording above: Sol's stronger search still is not proof.
- **A void loses its whole cell,** including other games in that cell, and no pool game has voided this way.
- **`oppo_info=True` still leaks Deck order** with legal decks. Edison play must not use it.
- **Morale Boost's Lua error** (`c93671934.lua:30`) fails its worker if reached. It is in no gauntlet or fixture list, and it is not fixed.

## Not verified, and open points

- **Position independence past the first turn, and beyond these scenarios.** 087 covers four scenarios, two seeds and one alternative order each, with seat 0 first to act. Later turns diverge through draws, so a same-state comparison needs a way to fix the draw. Sol's later [study review](../tests/edison/reports/retrain-2026-11-04/review/reviewer_report.md) ran paired checks that hold draws equal, through turn 8 with no leak found. That narrows this gap and does not close it.
- **Whether a pool card reaches a voiding prompt.** 0 voids in the soak and a pattern search. A card missed by both would void its cell, loudly.
- **`MSG_SELECT_TRIBUTE`'s throw** has no fixture, as no script can reach it by my reading of the core.
- **`MSG_CONFIRM_DECKTOP` is skipped outside verbose mode**, so cards publicly revealed from the top of a Deck never reach an observation. This hides public information and leaks nothing. It predates this work.
- **A revealed copy may stand in for a candidate.** In 086K the option binds to the copy already shown, though the engine's candidate is another copy of the code. Rows look the same and the response is the index. Copies whose in-Deck statistics differ would break this assumption. The soak and 087 saw none.
- **`PreserveSelectDeckSequence` and a reversed Deck at a prompt** have no fixture. 086L covers mixed face state by `ReverseInDeck`, and the soak reached Parasite Paracide.
- **Pool reachability of the reveal half of A** is by source reading (Dark Tinker, Maharaghi). No fixture uses a pool card for it.
- **What the differential checks.** Identity, zone, controller and face state, not a row's other columns. Soak policies never read labels. The network run covers a label-reading policy only for Frog Monarch and its opponents.
- **`verbose` output.** `show_decision` now prints a bound Deck spec, which is a real position of a same-code copy. Prompt lines still print the prompt's number. Verbose is for reading, and no policy sees it.
- **Tools not rebuilt:** `tools/edison/forward_model` and `pool_audit/build_audit_module.py` compile their own builds and have not picked this up. They already lag, as the trigger-order report notes.
- **`ygopro0` and `edopro` environments** were not audited. Edison does not use them.
- **Not checked:** whether the old networks gained anything from the exposed rows, and their strength on this build. Morale Boost's error is not diagnosed.

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
python tools/edison/hardening/soak.py --stage a2 --per-pair 8 --seed 80420000 --output build/labels/new-soak/lists --prompt-audit
python tests/edison/prompt_audit.py summarize build/labels/new-soak/lists/prompt-audit
python tests/edison/prompt_audit.py compare-soak OLD_SOAK NEW_SOAK
python tests/edison/prompt_audit.py compare-fields OLD_MODULE NEW_MODULE --seed 80440000
python tools/edison/learner/deck_label_check.py --runs RUNS_DIR --seed 80500000 --output labels.json
```

The other soak stages use the seeds above, with `--targets tests/edison/reports/deck-labels-2026-11-04/crash-targets.json --per-target 12` for the crash cards. Controls, each one edit to `prompt_spec` in this commit: old logic returns
`ls_to_spec(loc, seq, pos, controller != player)` at its top; `045c1d0` binding makes `cost` constant; the face mutant drops its face term; the selected mutant pushes `ls_to_spec` for the unselect list. Test 088 goes through `actor.py`, which imports
the checkout's module, so a control there means swapping that file and then restoring it.
