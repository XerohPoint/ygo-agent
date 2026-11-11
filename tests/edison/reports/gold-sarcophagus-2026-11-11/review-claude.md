> Independent review by a Claude subagent, November 11, 2026, of `b742363` and `935e2cc` (hardening r4), kept as written. Paths under
> `review/` are in that reviewer's worktree and are not committed. What was done about each finding is in
> [docs/edison-gold-sarcophagus.md](../../../../docs/edison-gold-sarcophagus.md#reviews).

# Review: hardening r4 (Gold Sarcophagus reason-effect fix)

Base commit: `935e2ccf18d8752ebe16cd42ba0fa66e52d93ed2` ("test: add 090B, a deleted effect in a card's temp state"), detached, two commits on `ef0624f` (`b742363` fix + 090A, `935e2cc` 090B). Nothing committed; all work is under `review/`.

**Verdict: not safe to certify as it stands.** r4 turns an existing use-after-free in `field::move_to_field` into a guaranteed NULL dereference. Legal production games reach it: Neo-Spacian Dark Panther copies Destiny HERO - Doom Lord. In 2000 such duels with the production policies, 20 crash on r4 and finish normally on r3. The suite also passes several wrong versions of the hunk that legal games can reach.

---

## BUG 1: r4 turns the ReturnToField use-after-free into a SIGSEGV

`operations.cpp:4553` (`field::move_to_field`, step 0, used by `Duel.ReturnToField`):

```cpp
if((ret == RETURN_TEMP_REMOVE_TO_FIELD) && (!(target->current.reason & REASON_TEMPORARY) || (target->current.reason_effect->owner != core.reason_effect->owner)))
```

The line dereferences the card's `current.reason_effect` without a null check. A card banished with `REASON_TEMPORARY` keeps the banishing effect there until it returns.

The path, step by step:
1. Destiny HERO - Doom Lord (41613948) banishes a monster with `REASON_EFFECT+REASON_TEMPORARY` and returns it at "your 2nd Standby Phase" through `Duel.ReturnToField`.
2. When Dark Panther (43237273) or Phantom of Chaos (30312361) uses a copied Doom Lord effect, the banishing effect is a `CopyEffect` copy. That copy resets at the End Phase, and the next turn start deletes it.
3. On r3, the return reads `->owner` from freed memory (heap-use-after-free). The read usually works by luck, and the monster returns.
4. On r4, the hunk has nulled the pointer, so the same line reads address 0x10 and the process dies.

The ruling is that the monster returns. Doom Lord's return is part of an effect that has already resolved.

Evidence:
- **Fixture.** `review/tests/doom_lord.cpp` (uses `review/tests/rharness.h`, a copy of the harness that loads any card). Usage: `doom_lord <cdb> <scripts> poc|panther|own [check]`.

  | Build | `poc` | `panther` |
  |---|---|---|
  | ASan r4 | `SEGV on unknown address 0x000000000010 ... field::move_to_field ... trees/r4/operations.cpp:4553` ← `field::process() processor.cpp:339` ← `process ocgapi.cpp:137` | same |
  | ASan r3 | `heap-use-after-free ... READ of size 8 ... operations.cpp:4553`; freed by `duel::delete_effect duel.cpp:98` ← `field::process_turn processor.cpp:3717` | same |
  | plain r3 | `returned on turn 5`, `RESULT returned=1` | same |
  | plain r4 | exit 139 | exit 139 |

  Logs: `review/logs/doom_lord-{poc,panther}-asan-r{3,4}.txt`.
- **Legal production duels.**
  - Specs: `review/specs2/doomlord2-*.json`, seeds 92001566–92003565, 2000 duels. fl22344 (hero_frog, the only corpus list with Doom Lord) plays the six Dark Panther lists, all three policies.
  - On the trace build (r4 plus logging, see "What I ran"), 20 duels reach the null read: 92001586, 92001925, 92002047, 92002129, 92002137, 92002410, 92002650, 92002762, 92002774, 92002956, 92002959, 92003037, 92003038, 92003083, 92003098, 92003143, 92003149, 92003233, 92003335, 92003346.
  - Replayed alone (`review/specs3/crash-*.json`):

    | Build | Result |
    |---|---|
    | ASan r4 module | **20/20 SEGV at operations.cpp:4553** (stack runs through `YGOProEnvImpl::next()` and `step()` on the env thread pool) |
    | plain r4 module | **20/20 Python worker segfaults** |
    | plain r3 module | 20/20 finish with status ok |
    | ASan r3 module | 3 checked, 3/3 heap-use-after-free at the same line |

    Logs: `review/runs/{asanr4,plainr4,plainr3,asanr3}-crash-*.{err,log}`.
- **A one-line null guard fixes it.** I changed the condition to `(target->current.reason_effect && target->current.reason_effect->owner != core.reason_effect->owner)`, which lets the return go ahead when the pointer is null. With that change the monster returns in both fixtures, and the full suite passes 531/531 (`review/fixcheck/`, `review/logs/suite-fixcheck.txt`).
  - Alternatives: keep the deleted effect's owner card when nulling a `REASON_TEMPORARY` card, or accept "no return" with a guard. Either way, r4 needs that line fixed and a regression test (the `doom_lord` fixture asserting `returned=1`) before certification.

Commands:
```
cd review && SAN=-fsanitize=address OBJ=out/asan-r4/doom_lord.o bash bin/build_rules.sh tests/doom_lord.cpp out/asan-r4 out/asan-r4/doom_lord
cd /tmp && out/asan-r4/doom_lord <wt>/assets/locale/en/cards.cdb <wt>/scripts/script panther
TREE=$PWD/run-asanr4 bash bin/replay.sh $PWD/specs3/crash-92001586.json asanr4-crash-92001586
```

## BUG 2: the suite passes wrong versions of the hunk that legal games reach

I ran 20 mutants of the r4 hunk through the full 531-case suite (`review/mutants/`, summary in `review/mutants/summary.txt`).

**Caught** (one or more of 090A/B and 052C fail):
- `r3_no_hunk`
- `after_clear` (hunk after `reseted_effects.clear()`)
- `clear_all`
- `clear_temp_always`
- `current_only`
- `skip_temp`
- `temp_only`
- `owner1_only`

**Survive with 531/531 PASS, and a legal game reaches them:**
- **`clear_current_always`** clears every card's `current.reason_effect` whenever any effect is deleted.
  - Doom Lord's own, live effect (no copy) then crashes at the return. Doom Lord's oath effect makes `reseted_effects` non-empty at that turn start.
  - `mutants/clear_current_always/doom_lord ... own` exits 139. r4 and r3 return the monster (`returned=1`).
  - Gap: no test checks a live `current` pointer across a turn start with a non-empty `reseted_effects`. 090A checks the live pointer only in `temp`, and checks `current` only on turns where nothing was reset.
- **`owner0_only` and `ended_turn_owner_only`** clear only cards owned by player 0, or by the player whose turn just ended.
  - Every pinned case (090A, 090B, 052C) is a P0-owned card at the end of P0's turn.
  - The Panther/PoC + Doom Lord fixture in `check` mode catches both: "card 89631139, current state", the opponent's banished monster. Commands: `mutants/<m>/doom_lord ... poc check`.
  - So a fix scoped to the turn player passes the suite.
- **`phase_reset_only`** clears only effects whose `reset_flag` has `RESET_PHASE`.
  - The suite pins only RESET_PHASE effects: the Gold Sarcophagus Standby effect and the Spirit return.
  - The trace of 1566 legal duels shows the turn start nulling pointers to many effects with no RESET_PHASE. Reset 0, via `e:Reset()`: Return from the Different Dimension (266), Instant Fusion (84), Rescue Cat (37), Jinzo - Returner (14). RESET_EVENT only: Armory Arm (97).
  - In those duels this mutant leaves the pointers dangling. I saw no later read of such a pointer in the sample.

**Survive but not shown reachable:** listed as GAP or NOTE below (`skip_spsummon`, `clear_spsummon_always`, `skip_previous`, `after_delete_before_clear`, `field_type_only`, `skip_deck`, `zone_cards_only`, `current_temp`).

Tests to add:
- the Doom Lord return, which asserts the return and no crash;
- a P1-owned card;
- a non-phase-reset effect, such as Return from the Different Dimension or Rescue Cat;
- a live `current` pointer surviving a turn start that deletes other effects.

## GAP 1: `spsummon.reason_effect` is untested, and no pool script reads it

- **Legal games make it dangle.** The trace shows 58 `spsummon` nulls in 1566 duels.
- **No pool reader.** The only reader is `Card.GetSpecialSummonInfo(SUMMON_INFO_REASON_EFFECT)` (`libcard.cpp:918`). The 9 scripts that use it are all outside the 4035-card pool. Pool `GetSpecialSummonInfo` callers (c23756165, c37675138, c50140163, c71645242, c78637313, and utility.lua) ask only for TYPE or other fields.
- **Untested.** `skip_spsummon` and `clear_spsummon_always` both pass 531/531.
- **Verdict.** r4 handles this state correctly, but nothing would catch a regression in it.

## GAP 2: events that survive the turn start are not covered by r4; unreachable in the pool

- **What survives.** Events raised at `process_turn` step 18 (EVENT_TURN_END and its `adjust_all`) stay in `instant_event` and `delayed_activate_event`. Step 19 and step 0 do not clear them. They reach the next turn's first point event. There, quick-effect step 2 and the delayed-activate loop pass `ev.reason_effect` to Lua conditions.
- **Why the pool cannot reach it.** A dangling `re` would need an effect that was reset this turn and that moves a card after step 26.
  - The pool's only EVENT_TURN_END handlers are Seal of Wickedness (17874674) and Life Absorbing Machine (14318794), and neither moves cards.
  - Adjust destroys pass a null reason effect to the group events.
  - Single events go through `process_single_event` into the chain lists, which step 19 clears.
- **Other holders that do not outlive the turn start:**
  - `delayed_quick` is cleared at point-event step 10.
  - `delayed_quick_tmp`, `quick_f_chain` and the new chain lists are cleared at step 19.
  - `point_event` and `full_event` are cleared at phase-event step 26.

## GAP 3: survivors with unknown reachability

- `field_type_only`: every deleted effect I saw named by a card had the FIELD bit; IGNITION and TRIGGER effects get it too.
- `skip_deck`: needs a Deck card whose temp state names a deleted effect, then a cancelled move that restores temp into current.
- `zone_cards_only`: skips only cards with no location, such as tokens that left the field.
- `current_temp`: skips spsummon, as in GAP 1.

I built no scenario for these. The trace saw 1197 nulls with no read outside the Gold Sarcophagus equip cases (Task 2).

## NOTE: what the fix changes in play (Task 2)

- **The value is right.** Scripts now get `nil` where r3 read a freed effect. A deleted effect always comes from a move in an earlier turn, so `nil` (no reason effect) matches the real game better than any stale value.
- **Reads observed.** Trace logging at every read site, over 1566 legal duels (seeds 92000000–92001565): 6 reads of a nulled pointer, all `re` (Lua param 5), in two cards that ignore `re`:
  - D.D.R. - Different Dimension Reincarnation (c9622164.lua:55, `desop`, single EVENT_LEAVE_FIELD);
  - Supervise (c95750695.lua:57/64/71, single EVENT_TO_GRAVE condition, target and operation).

  Both were Equip Spells added by Gold Sarcophagus and then activated. Activation goes through `move_to_field`, which does not overwrite `current.reason_effect`.
- **No new Lua errors.** `GetReasonEffect` in the pool:
  - Raging Cloudian (23639291) guards with `tc:GetReasonEffect() and ...`;
  - Dragon Ice (64262809) compares the result with `e` in the same chain.

  My static scan (`review/audit/nil_re_scan.py`, `audit/nil_re_scan.txt`) found 16 unguarded `re:` reads in places where r4 can hand over nil. None can error:
  - EFFECT_INDESTRUCTABLE_EFFECT values never get nil, because `check_indestructable_by_effect` returns first.
  - Security Ball, Calling Magic, Trap and Magic Capture, and Storm and Magma Neos read `re` only on moves that always set a fresh reason effect, such as REASON_EFFECT moves and `send_to`.
  - The paths where r4's nil appears (rule destroys of equips, `move_to_field` events, destroy-replace and indestructible values) already see nil for any card never moved by an effect, for example a drawn card.
- **C++ consumers.** `move_to_field` (BUG 1) is the only consumer of a card-state reason effect that dereferences it without a null check. `card::is_affect_by_effect` dereferences before its null check only under STATUS_SUMMONING, and a summoning card always has a fresh pointer.

## NOTE: holders of effect pointers (Task 1)

| Holder | Can it name a deleted effect, then be read? |
|---|---|
| `current`, `temp` | Yes (090A, 090B, 052C, the Doom Lord copy). r4 handles them, except the `move_to_field` consumer (BUG 1). |
| `previous` | Never: `previous.reason_effect` is never assigned (no `previous.reason_effect=` and no whole-state copies anywhere). Clearing it is dead code, and `skip_previous` is an equivalent mutant. |
| `spsummon` | Dangles in legal games; no pool reader (GAP 1). |
| `chain::triggering_state.reason_effect` | Never set (`set_triggering_state` copies only code and stats). |
| `relate_effect` | Pointer identity only, never dereferenced. A freed address reused by a new effect could give a false `IsRelateToEffect` for chain_id 0 relations (ABA). No read of freed memory. |
| Event and chain lists, `delayed_quick` | See GAP 2. `current_chain` is empty at the turn start. |
| `effects.oath` | Values are compared, never dereferenced, and `release_oath_relation` zeroes them when the chain link resolves. Keys leave the map in `remove_effect`. |
| `grant_effect` | The entry is erased with its key (`erase_grant_effect`), together with the cloned effects. |
| `core.reason_effect` | Zeroed after every execute_cost, target and operation, and restored by `solve_continuous`. It is 0 at the turn start. |
| `card::unique_effect` | Not cleared when a copied SetUniqueOnField resets. Reads are gated by `unique_code` and `unique_cards`, which the reset clears. |
| `immune_effect` cache | Another pointer cache, refreshed through the disable-check list. Not traced to closure; no ASan report. |
| Lua side | `interpreter::unregister_effect` unrefs the registry slot and the function refs and sets `ref_handle=0`. It does **not** clear the `effect*` inside the userdata. (1) Any script-held effect userdata (local upvalue, table) is a dangling pointer after the delete. (2) `label_object` stores the integer registry ref, which a later `luaL_ref` (new effect, group, coroutine) can reuse, so `GetLabelObject` can return an unrelated object. The pool patterns I checked keep the holder's reset equal to the label's (for example Greed Quasar's flag effect). This class predates r4 and r4 does not change it. |

Other paths that delete effects:
- `card::copy_effect` and `card::replace_effect` delete `pduel->uncopy` immediately. Those effects were never registered, so no card state can name them, but an `initial_effect` local or label could.
- Duel teardown.

`Card.RegisterEffect` on an immune card puts the unregistered effect into `reseted_effects`.

r4 cannot null a pointer to an effect that stays live: the hunk and the delete loop walk the same `reseted_effects`, and every member is freed on the next lines.

## NOTE: smaller points

- `after_delete_before_clear` (the hunk between the delete loop and `clear()`) is equivalent: it only compares pointer values.
- `check_reason_effects` tests membership in `pduel->effects`. A freed address reused by a new effect before the check would hide a dangling pointer (ABA). Running the suite under ASan is an independent check: r4 ASan gives 531/531 with 0 reports. r3 ASan gives 526/5 with 0 reports, because the harness check throws before the read.
- The patch applies cleanly on f96929650ff8 after `early-trigger.patch`. With the recipe's header inserts, the result equals `build/sarc/core-r4` except for the trailing newline of two headers.

---

## What I ran

**Builds** (g++ by hand, no xmake, at most 6 processes, all `nice -n 19`):
- Core trees: `review/trees/r4` and `review/trees/r3`, made from a clone of `core-src` checked out at f96929650ff8, with `early-trigger.patch` and the hardening patch applied (r4 from this worktree, r3 from `ef0624f`) plus the recipe's header inserts.
- Core libraries: plain r4, plain r3, ASan r4, ASan r3.
- Trace build `review/trees/trace`: r4, with a tombstone in place of nullptr that is logged and read as null at every read site.
- Python modules: ASan r4 (from `ygopro.cpp` in this worktree), ASan r3, plain r3, plain r4, trace.
- `edison_rules`: plain r4, ASan r4, ASan r3, the 20 mutants, and `fixcheck`.
- Scripts: `review/bin/{build_core,build_rules,build_module,replay,runmany}.sh`, `review/mutants/{make_mutants.py,run_mutants.sh}`.

**Suite results:**

| Build | Result |
|---|---|
| plain r4 | 531 PASS |
| ASan r4 | 531 PASS, 0 sanitizer reports |
| ASan r3 | 526 PASS, 5 FAIL (052C, 090A×2, 090B×2) |
| fixcheck (null guard) | 531 PASS |

**Mutants** (20; full results in `review/mutants/summary.txt`):
- Caught: `r3_no_hunk`, `after_clear`, `clear_all`, `clear_temp_always`, `current_only`, `skip_temp`, `temp_only`, `owner1_only`.
- Survive: `after_delete_before_clear`, `clear_current_always`, `clear_spsummon_always`, `current_temp`, `field_type_only`, `owner0_only`, `ended_turn_owner_only`, `phase_reset_only`, `skip_deck`, `skip_previous`, `skip_spsummon`, `zone_cards_only`.

**Duels**, all on seeds 92000000–92003565. Seed 92000000 was used twice: a 1-duel smoke run and the first doomlord duel.

| Specs | Duels | Build | Result |
|---|---|---|---|
| `review/specs/` (doomlord 60, expiring 906 over 151 target lists, random 600), seeds 92000000–92001565 | 1566 | ASan r4 | **1567/1567 ok (including the smoke duel), 0 sanitizer errors** (`runs/asan-*`) |
| same | 1566 | trace | 1197 nulls; 6 reads, all benign (Task 2) (`runs/trace-*`) |
| `review/specs2/`, fl22344 vs Dark Panther lists, seeds 92001566–92003565 | 2000 | trace | 20 duels reach the r4 null dereference |
| `review/specs3/` (those 20 replayed) | 20 | ASan r4 / plain r4 / plain r3 / ASan r3 | 20 SEGV / 20 worker crashes / 20 ok / 3 of 3 checked heap-use-after-free |

**Sanitizer errors seen:**
- the r4 SEGV and r3 heap-use-after-free at `operations.cpp:4553`, stacks quoted in BUG 1;
- no other sanitizer error in any duel or suite run.
