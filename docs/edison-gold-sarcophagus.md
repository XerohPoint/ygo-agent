# Edison stale reason effect fix (Gold Sarcophagus)

Prepared November 11, 2026, on branch `sarc-fix` from `edison` at `ef0624f`. Not pushed. Three reviews are done; see [Reviews](#reviews).

**Result:** the use-after-free the Dark Panther review found is fixed in `edison-hardening` r5. It was already in the certified engine, and
legal games reached it through Gold Sarcophagus.

- **The fix.** Before the turn start deletes reset effects, it clears every card's pointer to them. For a temporarily banished card, it keeps
  the owner card that the return check needs.
- **r4 was not merged.** The independent review found that it crashed duels in which Dark Panther copies Destiny HERO - Doom Lord. r5 fixes that.
- **Module and certification.** The production module is now `d1f1f701...`, replacing `7e39a113...`. Certification passes 619 of 619.
- **Play.** The fixed and r3 modules play identically in all 6,780 duels of the earlier gates. In the review's 2,000 Doom Lord duels they
  differ in 8, each because r3 wrongly refused to return a banished monster after reading a deleted effect.
- **Sanitizer.** Under AddressSanitizer, 5,224 duels on r5 read no freed memory.

The evidence is in [reports/gold-sarcophagus-2026-11-11](../tests/edison/reports/gold-sarcophagus-2026-11-11/).

## The bug

A card keeps the effect that last moved or summoned it as its reason effect, in four copies of its state: `current`, `temp`, `previous` and
`spsummon`. Scripts read it as the `re` of events the card raises, and through `Card.GetReasonEffect` and `Card.GetSpecialSummonInfo`. The
engine itself reads it in `field::move_to_field`, below.

- **Some moves keep the old pointer.**
  - `field::destroy` with no reason effect, as in battle, keeps it.
  - `destroy` and `send_to` with `PLAYER_SELFDES` keep it.
  - Several paths restore `current` from `temp`.
  - Activating a card from the hand keeps it too.
- **The effect it names can be deleted.** A reset effect goes into `core.reseted_effects`, and `field::process_turn` deletes the whole set at
  the next turn start.
- **Gold Sarcophagus reaches this.** Its player effect adds the banished card to the hand during the second Standby Phase and resets there. The card
  names that effect, which the next turn start deletes. If the card later moves by a path that keeps the pointer, its event passes the freed
  effect to Lua as `re` in `interpreter::push_param`. For example, Supervise is added and activated, and then leaves the field when the
  equipped monster does.
- **Other effects reach it too.** A trace build checked each turn start in the six recorded duels. It reported 44 card pointers naming an effect
  about to be deleted: 30 in `current`, 14 in `temp`.
  - 16 were the first report for their card and state. Of those, 12 named Gold Sarcophagus's Standby effect and one named Jinzo - Returner's
    End Phase effect.
  - The trace build has no sanitizer, so later reports can be a freed address that a new effect reused.
  - No other holder of an effect pointer was ever reported: no event or chain list, `delayed_quick`, card relation, `oath` or `grant_effect`
    value, or `core.reason_effect`.
- **The return check reads the owner.** `Duel.ReturnToField` returns a temporarily banished card only if the effect that banished it has the same
  owner as the effect returning it. Doom Lord's banishing effect, used through a Dark Panther or Phantom of Chaos copy, is a copy that
  the next turn start deletes, two Standby Phases before the return.
  - **r3 read that owner from freed memory.** In the 350 Doom Lord duels of the 7 batches logged below, 11 return checks read a deleted effect this way:
    9 found it gone and 2 found its address reused by a new effect. 9 of the 11 got the wrong owner and refused the return.
  - **What production did with `re` is inferred, not observed.** `interpreter::unregister_effect` releases the effect's Lua reference and sets
    `ref_handle` to 0 before the delete. A later read gets whatever the freed slot then holds, so `re` was usually nil and sometimes another
    object.

## The change

| File | Change |
| --- | --- |
| `repo/packages/y/ygopro-core/edison-hardening.patch` | r5. The r3 hunks are byte for byte unchanged. Three new hunks are listed below |
| `xmake.lua`, `tests/edison/conformance/xmake.lua` | `edison_hardening` r3 to r5. The r5 package installs side by side as `5334b050...`, library `4fd0ffdc...` |
| `tests/edison/harness.h` | `check_reason_effects()` runs after every `process()` call in every case. It fails if any card's four states name an effect that is not in `duel::effects`, naming the card and state. The harness pins Gold Sarcophagus's, Doom Lord's and D.D.R.'s scripts and looks cards up by code |
| `tests/edison/gold_sarcophagus_tests.h`, `rules.cpp` | Test 090 |
| `tests/edison/script_consistency_tests.h`, `assets.lock.json` | 93 pinned scripts; the r5 patch hash |
| `tools/edison/forward_model/build.sh`, `test_build_refusal.sh` | The search build now requires r5 and its library hash, and the refusal test also refuses r3 and r4 |
| `tools/edison/corpus/privacy_check.py` | "koala" joins the reviewed card words, for Sea Koala |

The three new hunks:
- **`process_turn` step 0:** before the delete, every card's `current`, `temp`, `previous` and `spsummon` reason effect that names a member of
  `reseted_effects` is set to null. If the card is temporarily banished, the owner of its `current` reason effect is first kept in a new
  member, `card::temp_remove_owner`.
- **`move_to_field` step 0:** the return check compares that kept owner when the reason effect is null.
- **`card.h`:** the new member.

The fix is unconditional, like r3's, because it is about memory, not rules. It changes a pointer only at the moment its effect is deleted. A
pointer to a reset effect still names it until the next turn start, as in the stock engine. Cards are never deleted during a duel, so the kept
owner cannot dangle. It is read only for a temporarily banished card whose reason effect is null, which only this clearing produces, and the
clearing sets it at the same time.

r4, the first version reviewed, cleared the pointers without keeping the owner. `move_to_field` then read the owner through a null pointer, and
the process died. The 20 duels in which the review saw this all die on the r4 production module, and all finish on r3 and r5.

**Test 090**, each case with a Rule 5 control on the stock scripts:

| Case | Expected, in both modes |
| --- | --- |
| 090A: Gold Sarcophagus banishes Call of the Haunted, and five turns pass | The banished card names the activation effect, which stays live, through three turn starts. After the add, the card names the reset Standby effect. After the next turn start deletes that effect, `current` is null and `temp` still names the live activation effect |
| 090B: the same, but the card is discarded for hand size at that End Phase | The discard moves the Standby effect into `temp`. The next turn start deletes it and clears it there |
| 090C: Dark Panther copies the opponent's Doom Lord and banishes the opponent's Blue-Eyes with the copied effect | After the next turn start deletes the copy, the banished card names no reason effect. It still returns on turn 5. r4 crashes the suite here |
| 090D: Rescue Cat recruits two Sea Koala, which its End Phase effect destroys | That effect has no reset and resets itself at the next End Phase. The turn start after that clears the Koala's pointers. Rescue Cat's own live effect stays as the Cat's reason and as the Koala's Special Summon reason |
| 090E, from Sol's review: Dark Panther copies the opponent's Red-Eyes Darkness Metal Dragon and Special Summons Blue-Eyes from the hand with the copy | After the next turn start deletes the copy, Blue-Eyes names it neither as its current reason nor as its Special Summon reason |
| 090F, from Sol's review: two Dark Panthers banish the same Blue-Eyes with copied Doom Lord effects on turns 1 and 3, and D.D.R. revives it in between | The second copy's delete keeps the second Panther as the banisher. The first Panther's return on turn 5 is refused, and the second's on turn 7 brings Blue-Eyes back |

**The run-loop check applies to every case.** On r3 it also fails 052C, an existing case in which Susa Soldier keeps a deleted effect in
`current`.

Seventeen mutants of the r5 hunks, each built by hand and run through the full suite of 539 cases:

| Mutant | Fails |
| --- | --- |
| No clearing (r3's turn start) | 052C, and 090A-F in both modes |
| r4: no owner kept, the old return check | Crashes the suite, at the first 090C |
| The owner is not kept | 090C and 090F in both modes: Blue-Eyes is not returned |
| The return check reads the null reason effect | Crashes the suite, at the first 090C |
| Skips the owner check when the reason effect is null, keeping no owner | 090F in both modes: the first Panther returns Blue-Eyes early. The first review suggested this simpler fix, and Sol showed it is wrong |
| Clears `current` only | 090B and 090E in both modes |
| Clears `current` and `temp` only | 090E in both modes |
| Skips `spsummon` | 090E in both modes |
| Clears after the delete loop has emptied the set | As with no clearing |
| Clears every reason effect at each turn start, live or not | 090A and 090D in both modes |
| Clears every card's `current` whenever anything is deleted | 090D in both modes |
| Clears every card's `spsummon` whenever anything is deleted | 090D in both modes |
| Clears only effects with a phase reset | 090D in both modes |
| Clears only cards player 0 owns | 090C and 090F in both modes |
| Clears only cards the ended turn's player owns | 090C, 090D and 090F in both modes |
| Clears when the effect is reset instead of when it is deleted | 090A, 090B, 090C and 090F in both modes |
| Clears only effects with the Field type bit | Nothing |

**One gap remains.** `Effect.SetType` gives Ignition, Quick and Activate effects the Field bit, so every deleted effect in these cases has it,
the copied Doom Lord and Red-Eyes effects included. Catching the last mutant needs a deleted effect without that bit that had moved a card.
Neither review found one in its traced legal duels: 1,566 for the first. The core never writes `previous.reason_effect`, so clearing that state
is defensive and no case can test it.

See [mutants.txt](../tests/edison/reports/gold-sarcophagus-2026-11-11/mutants.txt), made by
[mutants.py](../tests/edison/reports/gold-sarcophagus-2026-11-11/mutants.py).

## Gates

On module `d1f1f701...`, built by xmake from the r5 package and reproduced by a rebuild without xmake's object cache.

| Gate | Result |
| --- | --- |
| Certification | **619 PASS, 0 FAIL**, two identical replay traces, `edison_rules` `29ca41fd...`, 112 sources |
| The rules suite under AddressSanitizer, against the r5 sanitizer core | 539 PASS, 0 FAIL, no sanitizer report. The second review found the first version of 090D reading its deleted effect here |
| Certification traces against r3 | All 607 earlier cases identical except two. 075A-C now count 93 scripts. 089G was last before the run's closing summary line |
| Conformance | As recorded. All 114 test files and `results.json` byte-identical to r3's. `edison_conformance` is now `8a3a107b...` |
| `test_learner.py` | 13 of 13, every detail identical to r3's |
| Observation fields, 64 duels, seed 80730000 | No field differs in 15,708 decisions |
| Forward-model refusal test | All 9 configurations refused, including r3 and r4 |
| Privacy check of the report, evidence and tests | No unreviewed hit, after adding "koala" (the card Sea Koala) to the reviewed card words. [privacy.json](../tests/edison/reports/gold-sarcophagus-2026-11-11/privacy.json) |

Soak, the r3 round's seeds, each run on both modules:

| Stage | Seeds | Duels | Decisions | Crashes, diagnostics, violations | Differ from r3 |
| --- | --- | ---: | ---: | ---: | ---: |
| 21 lists, Main and Extra | 80700000-503 | 504 | 114,423 | 0 | 0 |
| 21 lists, whole Side moved in | 80701000-503 | 504 | 110,041 | 0 | 0 |
| 114 confirmed crash cards | 80702000-3367 | 1,368 | 201,234 | 0 | 0 |
| Uniform decks, whole pool | 80710000-999 | 1,000 | 152,171 | 0 | 0 |
| Gold Sarcophagus and Jinzo - Returner, 100 each, new | 80740000-199 | 200 | 28,267 | 0 | 0 |

Corpus and training-list duels, through `tools/edison/corpus/soak_worker.py`, on hand-built sanitizer modules and on the production modules.
Each production comparison covers every line the worker wrote:

| Set | Duels | r3 under AddressSanitizer | r5 under AddressSanitizer | r3 against r5 in production |
| --- | ---: | --- | --- | --- |
| The 110 legal corpus lists with Gold Sarcophagus or Jinzo - Returner, 10 duels each, seeds 91000000-91001099 | 1,100 | 2 freed reads, 91000713 and 91000822, one duel per process | 0 | Identical |
| The first review's corpus duels without copiers, seeds 86662000-86663599 | 1,600 | 3, as that review found. They are 3 of the 6 recorded duels | 0 | Identical |
| Training lists with Gold Sarcophagus or Jinzo - Returner against all 21, seeds 89003000-89003503 | 504 | 3 on r2, as measured before. These are the other 3 recorded duels, and they read freed memory on r3 too | 0 | Identical |
| The first review's duels of fl22344, the only corpus list with Doom Lord, against the six Dark Panther lists, seeds 92001566-92003565 | 2,000 | The review checked 3 of the 20 that crash r4: all read freed memory in `move_to_field` | 0 | 1,992 identical. 8 differ, each where r3 refused a return |
| The 20 of those that crash r4, one duel per process | 20 | | 0 | 10 identical, 10 differ: alone in a process, r3's freed memory held different values |

**Why the 8 differ.** Builds of r3 and r5 that log every return check were run on the 7 batches that hold those duels; their decision logs
match the production modules' byte for byte.
- On r3, 9 of the 120 checks refused, all in the 8 duels: the freed memory gave a different owner.
- On r5, none of the 123 checks refused, and 14 of them used the kept owner.
- The reason for each refusal is in [return-checks.json](../tests/edison/reports/gold-sarcophagus-2026-11-11/return-checks.json).

**Pointers at the turn start.** A trace build of r5, which reports every pointer still naming an effect at its delete after the fix runs, found
none at any of 191 turn starts in the six recorded duels.

See [sanitizer.json](../tests/edison/reports/gold-sarcophagus-2026-11-11/sanitizer.json) and
[production-r3-vs-r5.json](../tests/edison/reports/gold-sarcophagus-2026-11-11/production-r3-vs-r5.json).

## What this changes

- **Results to date stand.** Gold Sarcophagus is in code list v1, so M1, M2 and Stage A games could reach the freed read in a small share of
  games. Doom Lord is not in v1, and no v1 card copies effects, so no training game could reach the wrong refusal. r3 and r5 decide
  identically in every training-list, soak and corpus duel compared, including all eight known to read a deleted effect through `re`, except
  the 8 Doom Lord duels above.
- **Stage B needs no exclusion for these cards.**
- **The search binary.** `build.sh` now refuses r3, so the search binary must be rebuilt on r5 before Stage B uses search. It was not built
  here.
- **Not covered:**
  - Effects without the Field type bit, as above.
  - Script-held effect objects. A Lua value still names its effect after the delete, because `unregister_effect` does not clear it. This was
    already in the engine and r5 does not change it. Sol also lists the event, chain and selection records that keep raw
    effect pointers; none of them outlives the turn start into a reader it found.

## Reviews

**First independent review** of `b742363` and `935e2cc` (r4), by a Claude subagent with sanitizer and trace builds, 20 mutants and 3,566
new duels: [review-claude.md](../tests/edison/reports/gold-sarcophagus-2026-11-11/review-claude.md). Its verdict: not safe to certify.

| Finding | Class | Done |
| --- | --- | --- |
| r4 turns the freed read in `move_to_field` into a null dereference. Dark Panther or Phantom of Chaos copying Doom Lord reaches it, and 20 of 2,000 legal duels crash | BUG | Confirmed: its fixture segfaults on r4, and its 20 duels die on the r4 production module and finish on r3. Fixed in r5 by keeping the owner, and test 090C |
| The suite passes hunks that clear the live `current`, only player 0's cards, only the ended turn's player's, or only phase-reset effects | BUG | Each now fails 090C or 090D |
| `spsummon` is untested and no pool script reads it | GAP | Covered after Sol's review by 090E |
| Events raised at the turn's end survive the turn start, but no pool card can reach them | GAP | Recorded |
| Effect-type and zone filters survive with unknown reach | GAP | Reported above |
| Scripts now see nil where they read freed memory; 6 such reads in 1,566 traced duels, in scripts that ignore `re` | NOTE | Agreed |
| Script-held effect objects outlive the delete; that is not new | NOTE | Reported above |

**Second review** of `f7d5a0c` (r5), by the same subagent: [review-claude-2.md](../tests/edison/reports/gold-sarcophagus-2026-11-11/review-claude-2.md).
It found r5's engine change sound.
- Its fixtures return the monster, and the 20 crash duels finish on r5 under AddressSanitizer.
- It ran 1,500 new duels on seeds 92004000-92005499 with no sanitizer error, 1,000 of them Doom Lord against every copier list.
- It argued that the kept owner cannot be stale where it is read.
- It counted the return checks in the 20 crash duels: 10 that r3 refused because the freed block had been reused, which r5 returns, and
  none the other way.

One finding:

| Finding | Class | Done |
| --- | --- | --- |
| 090D itself read its End Phase effect on turn 3, after the delete, so the suite aborted under AddressSanitizer and passed on plain builds only by luck | BUG | Fixed: the effect is read only on turn 2, while it is live. The suite then passed 535 of 535 under AddressSanitizer with no report |

**Sol** (gpt-6.1-sol, through the Codex CLI) reviewed `d410534`: [review-sol.md](../tests/edison/reports/gold-sarcophagus-2026-11-11/review-sol.md).
- **The run.** Its first turn stopped on the account's usage limit after its first reads, and it was resumed on r5 when the limit reset.
- **The engine change.** It found it correct. The clearing touches only effects about to be deleted. The kept owner is replaced on each later
  temporary banishment and is never read stale in legal play. No other engine code dereferences a card's reason effect unguarded.
- **Scripts.** Nil gives the right result in the pool scripts that read these reasons: Earthbound Immortal Aslla piscu and Ccarayhua, Raging
  Cloudian and Garoth. The scripts with unguarded reads are guarded by their reason tests.

It found two wrong versions of the hunks that the whole suite passed. I reproduced both on its builds:

| Finding | Class | Done |
| --- | --- | --- |
| Skipping the `spsummon` state passes every case, though Dark Panther copying Red-Eyes Darkness Metal Dragon and summoning Blue-Eyes with the copy leaves that state naming the deleted copy | BUG | Test 090E. The mutant now fails it. This also corrects the earlier view that no legal game leaves that pointer dangling |
| Skipping the owner check when the reason effect is null passes every case, though two Dark Panthers banishing the same Blue-Eyes make the first Panther's return take it back early | BUG | Test 090F. The mutant now fails it, so r5 keeps the owner rather than adopting the simpler check |
| `check_reason_effects()` checks membership after each `process()` call; it cannot see a use inside a call, and a reused address can hide a stale pointer | NOTE | Agreed. The suite also runs clean under AddressSanitizer, which sees uses inside a call |

## Reproduce

```bash
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
export http_proxy=http://127.0.0.1:9 https_proxy=http://127.0.0.1:9   # xmake fails silently while it tries the network
W=$PWD
xmake f -P "$W" -o "$W/build" -m release -y          # installs ygopro-core r5 from the cached clone
xmake b -r -P "$W" -j 8 ygopro_ygoenv
xmake b -r -P "$W" -j 8 edison_rules
(cd tests/edison/conformance && xmake f -P . -o ../../../build/edison-conformance -m release -y && xmake b -r -P . -j 6)
python tests/edison/run.py
python tools/edison/pool_audit/build_codelist.py     # the soak worker's code list, in build/
python tools/edison/hardening/soak.py --stage targets --targets GOLD_TARGETS --per-target 100 --seed 80740000 --output build/sarc/gates5/soak-gold-r5/targets
bash tools/edison/forward_model/test_build_refusal.sh
python tests/edison/reports/gold-sarcophagus-2026-11-11/mutants.py --full
```

`GOLD_TARGETS` is a file holding `[75500286, 9418534]`. The other soak stages and comparisons are as in
[edison-dark-panther.md](edison-dark-panther.md#reproduce).

The sanitizer runs use `build/sarc/`. Its scripts are adapted from the first Dark Panther review's and are not committed:
- `prep_core.py` copies the patched sources and can add the trace check. `core-r4b` holds the r5 sources.
- `build.sh` builds a module or `libcore.a` by hand.
- `mktree.sh`, `replay.sh` and `runmany.sh` play specs in a run tree per module.

Committed alongside the report:
- [mk_g4.py](../tests/edison/reports/gold-sarcophagus-2026-11-11/mk_g4.py) writes the Gold Sarcophagus set.
- [cmp_prod.py](../tests/edison/reports/gold-sarcophagus-2026-11-11/cmp_prod.py) compares two trees' logs duel by duel.
