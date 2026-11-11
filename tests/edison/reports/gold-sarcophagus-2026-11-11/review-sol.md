> Review by Sol (gpt-6.1-sol, through the Codex CLI), November 11, 2026, of `d410534` (hardening r5 and the 090D test fix), kept as
> written. Its first turn stopped on the account's usage limit and it was resumed on the new commit. Paths under `sol-review/` are in
> its worktree and are not committed. What was done about each finding is in
> [docs/edison-gold-sarcophagus.md](../../../../docs/edison-gold-sarcophagus.md#reviews).

No - r5's engine change appears correct, but I would add the two missing certification cases below before merging it into the certified engine.

Reviewed detached d410534, including b742363, 935e2cc, f7d5a0c and d410534. The BUG findings below concern tests accepting wrong engines; neither mutant is the submitted r5 engine. Commands below run from this worktree in WSL. All review artifacts are in sol-review/.

**BUG 1 - The entire suite accepts an engine that leaves a legally reachable deleted Special Summon reason.**

Evidence: my omit_spsummon mutant removes only &pcard->spsummon from process_turn's clearing loop. All eight 090 cases and all 535 suite cases pass. A separate probe using the real scripts makes Dark Panther copy an opposing Red-Eyes Darkness Metal Dragon, then use the copied effect to summon Blue-Eyes from its hand. After the copy expires, the next turn deletes it. The mutant leaves Blue-Eyes's spsummon.reason_effect dangling; the existing harness immediately reports:

> A card keeps a deleted effect as its reason effect (card 89631139, spsummon state)

This is a legally reachable board and sequence, requiring no invented script. The submitted r5 passes the same probe.

090D verifies that a *live* Special Summon reason survives; it never puts a *deleted* effect into that state. 090A/B cover current and temp, and 090C covers temporary removal, so none supplies this missing input to check_reason_effects(). Add this copied-summon sequence as a certification case.

Commands:

```sh
python3 sol-review/build_review.py omit_spsummon
python3 sol-review/build_probe.py
sol-review/omit_spsummon/probe summon
```

Results: sol-review/omit_spsummon/{090.log,full.log,probe-summon.log}; probe source: sol-review/probe.cpp, summon().

**BUG 2 - 090C and the entire suite accept skipping the owner check on nil, which returns a card too early in a legal game.**

My skip_owner_on_nil mutant replaces the fallback comparison with:

```cpp
target->current.reason_effect &&
target->current.reason_effect->owner != core.reason_effect->owner
```

It passes all eight 090 cases and all 535 suite cases. 090C tests only matching owners, so unconditional acceptance on nil produces the expected return.

The real-script owner() probe distinguishes it:

1. Turn 1: Panther A copies the opponent's Doom Lord and temporarily banishes the opponent's Blue-Eyes.
2. Turn 2: the opponent uses D.D.R. - Different Dimension Reincarnation, discarding Jerry Beans Man, to summon Blue-Eyes.
3. Turn 3: Panther B copies Doom Lord and temporarily banishes that same Blue-Eyes again.
4. Turn 4: B's copied banishing effect is deleted.
5. Turn 5: A's scheduled return must refuse B's banishment.
6. Turn 7: B's scheduled return must succeed.

R5 refuses on turn 5 and returns on turn 7. The mutant returns on turn 5 and fails "The first Panther returned the second Panther's banishment EARLY." Both Panthers and every action are legal Edison cards/actions. The probe harness adds only D.D.R.'s actual database record and unchanged script to the existing fixture inventory.

Commands:

```sh
python3 sol-review/build_review.py skip_owner_on_nil
python3 sol-review/build_probe.py
sol-review/skip_owner_on_nil/probe owner
sol-review/baseline/probe owner
```

Results: sol-review/{skip_owner_on_nil,baseline}/probe-owner.log. Add the different-owner refusal case alongside 090C. This also checks that temp_remove_owner is replaced on a later banishment.

**NOTE - R5 clears exactly the effects being deleted and preserves the return invariant.**

In core-r4b/processor.cpp:3706-3734, the new loop runs immediately before deletion, while all effect objects still exist. It changes a state only if its pointer belongs to core.reseted_effects. A reset effect is still allocated/in duel::effects until this point; r5 does not erase its reason early. Other live effects are untouched. The loop includes every duel card, not just cards currently on the field.

Saving effect->owner only for current plus REASON_TEMPORARY is appropriate: move_to_field uses that current state's banishment. The saved object is the owner *card*, not its player or the banished card's owner. Control changes do not invalidate that identity. Cards have no in-duel delete_card callers. A live current.reason_effect takes precedence over the saved owner; a later expiring temporary banishment overwrites the cache before clearing its reason, as the owner probe demonstrates.

The cache can retain an old value after a return or another move, but the reader requires REASON_TEMPORARY. Real Duel.Remove operations supply their executing effect; while it lives, the cache is ignored, and when deleted, its owner is saved. Failed operations restore the old reason state within the processing sequence, not across a turn start. I found no legal Edison route making the fallback use the wrong owner's cache. Fabricating REASON_TEMPORARY with a null effect through an artificial caller is outside this conclusion.

Skipping the check when nil loses the identity invariant; BUG 2 demonstrates the difference. The fallback preserves stock's intended accept/refuse behavior without reading freed memory.

For a game that never reaches a deleted reason pointer, these changes have no intended play change: the clearing predicate never matches an observable live reason, and the fallback is used only after clearing. The baseline suite also passes.

Evidence command:

```sh
bash sol-review/source_evidence.sh
```

The precise r5 hardening patch SHA-256 is 88afe488ba7a70e5ebf2894fbc7cc5cce2859564d055bef16c7dcbad2713ec02, matching assets.lock.json. Both requested xmake pins name r5. I did not run xmake.

**NOTE - Other destruction paths and retained engine references do not invalidate this fix's scope.**

- duel::delete_effect also runs in card::copy_effect and replace_effect, for duel::uncopy. card::add_effect puts rejected EFFECT_FLAG_UNCOPYABLE effects there during copied initial_effect loading, before they become movement/summoning reasons. Duel destruction/clear also deletes effects, together with the duel's cards. These are not additional legal sources of dangling card reasons.
- Card relations contain card pointers or effect/chain-ID keys. Effect relations can retain an address, but their readers compare it; they do not enumerate or dereference it into Lua. Ordinary chain relations are released at resolution, and movement/reset clears relations.
- Event and chain records do contain raw effect pointers. Pending operations, chains and event processing finish before turn advancement. Event history is moved to used_event and freed/cleared before this deletion loop; new-turn processing does not replay last turn's history. Some scratch selections may retain old records until overwritten, but I found no consumer that resumes them across a turn start. The fix does not generically scrub every event/processor container.
- core.reason_effect is execution context: execute_cost/target/operation set it while running Lua and clear it on completion/error; nested continuous processing saves/restores it. No executing coroutine advances the turn underneath itself.
- Oath keys are removed with the effects; reason associations are released when the activation resolves. Oath values are compared, not dereferenced or exposed to scripts. Grant removal removes the granted effects and erases its map entry; adjustment erases removed recipients. A grant template stored through label_object is a separate Lua-reference lifetime issue, not a card reason.
- SetLabelObject stores an effect's registry reference number, not a lifetime-owning reference. unregister_effect unreferences that number; retaining it can produce nil or a reused registry value. A Lua table can retain the original effect userdata, whose C++ pointer is not invalidated on deletion. R5 does not repair either general problem. I found no additional legal Edison failure from these mechanisms in this review; arbitrary scripts retaining/resetting effects could construct one, and I did not pursue planted scripts.

The last items are scope observations, not a claim that all effect pointers are now memory-safe. Evidence: source_evidence.sh and source-evidence.log, particularly duel.cpp, card.cpp:1765-1768/2037-2039/2083-2085/2240-2289, field.cpp:1308-1326/2121-2163, processor.cpp:730-878/1525-1543/4378-4389/4460/4587-4617, and interpreter.cpp:73-103.

**NOTE - I found no other newly unsafe engine dereference of a card-state reason effect.**

I searched all four state names and followed their readers, including calls through local variables/parameters. The direct current-state dereferences remaining outside the new guarded fallback are operations.cpp:5029 and 5094; both already test the pointer before calling is_self_destroy_related(). The new processor dereference is guarded by non-null and membership in the not-yet-deleted set.

GetReasonEffect and GetSpecialSummonInfo use interpreter::effect2value, which handles nullptr. Event parameter conversion also handles it. is_affect_by_effect has an existing unguarded access when STATUS_SUMMONING is set before its general null check, but this fix cannot carry an expired previous-turn summon reason into that active summoning window: the new summon installs its current reason before processing it. Other functions requiring a non-null executing effect are not reading the cleared historical state.

In this pinned core, previous.reason_effect is initialized null and never assigned a non-null value: previous copies individual location/card properties, not current's entire state. Clearing it is harmless defensive coverage; a deleted previous-state reason is not presently a legal test input.

Evidence command: bash sol-review/source_evidence.sh; broader references are saved in sol-review/core-references.txt.

**NOTE - Nil is appropriate for the relevant Edison script readers I found; an unguarded textual re use alone is not a legal failure.**

I scanned the codes from the supplied codes_full.txt, then inspected event registrations and guards, rather than treating every re parameter as a historical card reason. The useful cases are:

| Script | What nil does; ruling relevance |
| --- | --- |
| Earthbound Immortal Aslla piscu, c10875327.lua:39-42, EVENT_LEAVE_FIELD | Explicitly accepts nil through `not re or ...`. A monster summoned by a copied The Creator effect can outlive that effect; Relinquished equipping it later preserves its reason and raises the leave-field event. Nil permits its leave-field trigger, correctly. Its actual own self-destruction installs its live self-destroy effect, so the exception still works. |
| Earthbound Immortal Ccarayhua, c79798060.lua:39-41, EVENT_DESTROYED | Explicitly requires re, so nil suppresses its wipe without a Lua error. In the analogous copied-summon/equip/lost-target destruction, destruction is by the rules, not a card effect; suppression is correct for the card-effect destruction condition. Actual self-destruction supplies its own live effect and remains excluded. |
| Raging Cloudian, c23639291.lua:13-16, EVENT_TO_GRAVE | Guards GetReasonEffect() before GetOwner(). Nil rejects a destruction that was not by the destroyed monster's own effect. Actual EFFECT_SELF_DESTROY processing supplies the live self-destroy effect before raising the event, so a legitimate own-effect trigger is not lost. |
| Miracle Flipper, c131182.lua:83-84; Malice Doll of Demise, c72657739.lua:10-13 | Their unguarded re method calls would error if reached with nil, but their REASON_EFFECT tests short-circuit the relevant later rule destructions. Actual effect destruction/sending supplies a live executing effect; actual self-destruction explicitly installs one. I found no legal newly-cleared-nil route through those guards. |
| Security Ball, c26533075.lua:41-44 | Its re call is also unguarded, but the destruction/opponent-player guards prevent it on the relevant lost-target rule destruction (PLAYER_NONE). A real opponent's destroying effect supplies a live re. |

These script outcomes are source reasoning, not additional replay results. Garoth's EVENT_TO_GRAVE condition also explicitly rejects nil before using re (c59019082.lua:19-22). Many other unguarded readers, such as Royal Oppression's chain-response condition, receive the active chain effect rather than a card's saved movement reason; deletion waits until that chain and turn have finished. I found no legal nil-triggered Lua error caused by r5.

Reproduce the inventory with python3 sol-review/audit_scripts.py; selected source excerpts are in source-evidence.log. Scripts and the database were read only.

**NOTE - Individual tests and the harness otherwise check the intended boundaries.**

090A checks a live activation reason through multiple turn starts, the delayed effect while reset but still allocated, and clearing current only on deletion. It fails on the supplied r3 binary. Alone it can pass a current-only fix; 090B supplies the temp-state counterexample and also fails on r3. The six Blue-Eyes in 090B are a synthetic hand-size fixture, not a legal deck list; the same discard path is reachable with a legal six-card hand.

090C reaches the formerly unsafe ReturnToField reader and requires the correctly timed return of an opponent's card. It catches the missing-owner/old-dereference class, but needs BUG 2's negative case. 090D checks an effect that resets itself without RESET_PHASE and preservation of unrelated live reasons. d410534 correctly reads destroyer->owner/reset_flag only on turn 2 after effect_live succeeds; turn 3 only compares its saved address.

check_reason_effects() examines all duel cards and all four states after every public process() call without dereferencing the candidate effect. This is a useful invariant check, not a complete lifetime oracle: an internal use-before-return can happen before it runs, and address reuse could hide a dangling pointer. I did not construct an artificial script to exploit those limitations. The concrete false passes above need only ordinary card scripts and missing test sequences.

**What I ran**

Hand builds used g++/ar and the supplied core-r4b objects/Lua 5.5.1, with only the changed mutant object rebuilt. The certification translation unit was rebuilt from this worktree's d410534 tests. Build commands and diagnostics are in each build.log; build_review.py is repeatable. No sanitizer build was run by me.

| Build | 090 | Full suite | Copied-summon probe | Different-owner probe |
| --- | --- | --- | --- | --- |
| Review baseline, r5 | 8 PASS | 535 PASS | PASS | PASS, refusal turn 5 / return turn 7 |
| Own mutant omit_spsummon | 8 PASS | 535 PASS | FAIL, deleted spsummon reason | PASS |
| Own mutant skip_owner_on_nil | 8 PASS | 535 PASS | PASS | FAIL, early return turn 5 |
| Supplied out-plain-r3 binary (935e2cc tests) | 0 PASS / 4 FAIL | Not run | Not run | Not run |
| Supplied out-plain-r4b binary (d410534 tests) | 8 PASS | Not run | Not run | Not run |

Rebuild/run commands:

```sh
python3 sol-review/build_review.py baseline
python3 sol-review/build_review.py omit_spsummon
python3 sol-review/build_review.py skip_owner_on_nil
python3 sol-review/build_probe.py
bash sol-review/source_evidence.sh
python3 sol-review/audit_scripts.py
```

The supplied binaries were invoked with /home/mada/ygo-agent/assets/locale/en/cards.cdb /home/mada/ygopro-scripts --test=090. Their logs are sol-review/r3-090.log and sol-review/provided-r5-090.log. Probe and full-suite results are in the corresponding build directories. Existing tracked files were not edited, and no commit was made.
