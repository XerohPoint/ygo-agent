> Second independent review by the same Claude subagent, November 11, 2026, of `f7d5a0c` (hardening r5), kept as written. Paths under
> `review/` are in that reviewer's worktree and are not committed. What was done about each finding is in
> [docs/edison-gold-sarcophagus.md](../../../../docs/edison-gold-sarcophagus.md#reviews).

# Review 2: hardening r5 (`temp_remove_owner`)

Base commit: `f7d5a0ca7c48` ("fix: keep the banishing card for ReturnToField after the delete (hardening r5)"), detached, on `935e2cc`. Nothing committed. All work is under `review/`.

**Verdict: not yet.** The r5 engine change is sound and closes BUG 1. But the new test 090D reads a deleted effect, so the suite aborts under ASan and its result on plain builds is undefined. Fixing that test is one small edit, which I verified. With it, r5 is safe to certify.

---

## BUG: test 090D reads a freed effect

`tests/edison/gold_sarcophagus_tests.h:252` (`gold_sarcophagus_rescue_cat`) runs this check at every idle prompt on turns 2 and 3:

```cpp
require(destroyer && destroyer != activation && destroyer->owner->data.code == rescue_cat &&
            !(destroyer->reset_flag & RESET_PHASE), ...);
```

On turn 3 the turn start has already deleted `destroyer`, the Rescue Cat End Phase effect. The test itself checks that a few lines later with `!d.effect_live(destroyer)`. So `destroyer->owner` and `destroyer->reset_flag` are freed reads.
- **Plain builds:** the suite passes only because the freed block still holds its old bytes. If the allocator reuses the block, the check could fail spuriously or crash.
- **ASan r5:** the suite aborts in CONTROL 090D. The EDISON 090A–D cases never run.

```
==318913==ERROR: AddressSanitizer: heap-use-after-free on address 0x51700002dc10 ... READ of size 8
    #0 ... gold_sarcophagus_rescue_cat(...)::{lambda(certification::Event const&)#1}::operator() ../tests/edison/gold_sarcophagus_tests.h:252
    #5 certification::TestDuel::run ../tests/edison/harness.h:584
    #6 gold_sarcophagus_rescue_cat ../tests/edison/gold_sarcophagus_tests.h:228
0x51700002dc10 is located 16 bytes inside of 696-byte region   (offset 16 = effect::owner)
freed by thread T0 here:
    #1 duel::delete_effect(effect*) trees/r5/duel.cpp:98
    #2 field::process_turn(unsigned short, unsigned char) trees/r5/processor.cpp:3732
previously allocated by thread T0 here:
    #1 duel::new_effect() trees/r5/duel.cpp:80
    #2 scriptlib::effect_new(lua_State*) trees/r5/libeffect.cpp:20
```

Log: `review/logs/suite-asan-r5.txt`.

**Fix (verified):** dereference `destroyer` only once, when it is first captured on turn 2 and still live (add `d.effect_live(destroyer)` to that check), and check only membership on turn 3. The patched copy is `review/tests5/gold_sarcophagus_tests.h`.
- ASan r5 suite: **535 PASS, 0 sanitizer reports** (`review/logs/suite-asan-r5-fix.txt`).
- The mutants that 090D kills still fail on turn 2: `clear_current_always` gets a null `destroyer`, and `phase_reset_only` fails the harness check.

## Q1: r5 closes BUG 1

**Doom Lord fixtures** (`review/tests/doom_lord.cpp`, rebuilt against the r5 headers because `card` changed layout):

| Build | `poc`, `panther`, `own` | Harness check on (`check`, plus new `checkall` through the return) |
|---|---|---|
| plain r5 | `returned on turn 5`, `RESULT returned=1 banished=1` | `CHECK PASS through turn 4`; `checkall` returns on turn 5 |
| ASan r5 | same | same, 0 sanitizer reports |

**The 20 crash specs** (`review/specs3/`; seeds 92001586 … 92003346):

| Build | Result |
|---|---|
| ASan r5 module (`run-asanr5`) | 20/20 status ok, 0 sanitizer reports |
| plain r5 module | 20/20 ok |

Plain r5 and ASan r5 give identical traces for all 20 duels.

**New duels** on ASan r5, seeds 92004000–92005499 (`review/specs4/`):
- 1000 duels of fl22344 against every Dark Panther and Phantom of Chaos list;
- 500 duels of 250 lists holding expiring or copied effects against random opponents.

**1500/1500 ok, 0 sanitizer reports.** A logging build replayed the 1000 Doom Lord duels: 8 returns whose banishing effect had been deleted (in 5 duels) and 258 with a live one. All of them returned.

## Q2: `temp_remove_owner` is never stale or wrong in reachable play, and the return matches what stock intended

**Why it is not stale:**
- It is written only at the turn start, just before the hunk nulls `current.reason_effect` on a card whose `current.reason` has REASON_TEMPORARY. The value is the effect's `owner`, read while the effect is still allocated.
- It is read only when REASON_TEMPORARY is set and `current.reason_effect` is null.
- That state cannot arise any other way:
  - REASON_TEMPORARY comes only from a script's `Duel.Remove(..., REASON_TEMPORARY)`. That move always writes a non-null `core.reason_effect`, because removal is refused outside operations, costs and targets.
  - Every other move rewrites `current.reason`, and the return clears the bit (`operations.cpp:4655`).
  - A cancelled move restores the pre-move `temp` state, so it can bring back only that same cleared state.
- So every read sees the owner from the most recent clear.
- `card*` owners are never freed during a duel.

The field is never reset. That is harmless, because it is gated as above, though resetting it when REASON_TEMPORARY is cleared would be tidier.

**Return or refuse matches what stock intended:**
- The fallback compares exactly the value stock would read if the freed memory were intact.
- All 10 pool scripts that call `ReturnToField` (c22959079, c36261276, c41006930, c41382147, c42810973, c41613948, c47295267, c72885174, c56460688, c87902575) banish the card with REASON_TEMPORARY themselves. Their returner is the banishing card, or the card that copied its effect.

**Measured:** I logged every return check on r3 and r5 over the 20 crash duels, up to each duel's first divergence (`review/retlog/`, `review/runs/retlog{r3,r5}-*`):

| Banishing effect | Checks | r3 | r5 |
|---|---|---|---|
| live | 22 | return | return (identical) |
| deleted; r3's freed read happened to be right | 12 | return | return (identical) |
| deleted; r3's freed block reused | 10 | refuse (read garbage owners 63977008, 17732278, 1845204, 0, 35027493, 33420078, 538215872, 41613948) | return (kept owner Dark Panther = returner) |

No check refuses on r5 where r3 returned. Those 10 cases explain why only 10/20 r3 and r5 traces are identical, and in them r3 was the wrong one.

## Q3: the earlier survivors now fail, except `field_type_only`

My mutants of the r5 hunks (`review/mutants5/`; 18, run against the 535-case suite on plain r5, which itself gives 535/535):

| Mutant | r5 suite | Caught by |
|---|---|---|
| clear_current_always | 533/2 FAIL | 090D ("The Koala do not name the End Phase effect") |
| owner0_only | 533/2 FAIL | 090C (card 89631139, current state) |
| ended_turn_owner_only | 531/4 FAIL | 090C, 090D |
| phase_reset_only | 533/2 FAIL | 090D (card 87685879, current state) |
| clear_spsummon_always | 533/2 FAIL | 090D ("A Koala lost its Special Summon reason") |
| **field_type_only** | **535 PASS** | none (GAP below) |
| keep_owner_never | 533/2 FAIL | 090C "Blue-Eyes did not return" |
| keep_owner_temp_state | 533/2 FAIL | 090C |
| return_null_guard_refuse | 533/2 FAIL | 090C |
| return_r4_line | crash (exit 139) | 090C |
| keep_handler, keep_owner_no_temp_test, return_null_guard_accept | 535 PASS | none, but equivalent in the pool (NOTE) |
| current_temp, skip_spsummon, skip_previous, skip_deck, zone_cards_only | 535 PASS | unchanged since review 1 |

## GAP: what the suite still does not pin

- **`field_type_only` survives.**
  - All three pinned deleted effects, and every deleted effect named by a card in my 1566 traced duels, have the FIELD bit. IGNITION, TRIGGER and continuous effects all get it.
  - A non-FIELD effect (SINGLE or EQUIP) that moves a card, then expires, and is read later is still unshown in legal play.
- **`skip_spsummon` and `current_temp` survive.** Unchanged since review 1: no pool script reads `SUMMON_INFO_REASON_EFFECT`. 090D now pins a live spsummon pointer, but no case has a deleted one there.
- **`skip_deck` and `zone_cards_only` survive.** Reachability is unknown, as before.

## NOTE

- `keep_handler`, `keep_owner_no_temp_test` and `return_null_guard_accept` cannot differ from r5 in pool play:
  - every pool temp-banish effect has handler equal to owner, including CopyEffect copies, which are owned and handled by the copier;
  - the owner is read only under REASON_TEMPORARY;
  - every pool returner is the banishing card.
- `skip_previous` is dead code, as before.
- The patch applies cleanly on f96929650ff8 after `early-trigger.patch`. With the recipe's header inserts, the result equals `build/sarc/core-r4b` except for the trailing newline of two headers.

---

## What I ran

**Builds** (g++ by hand, no xmake, at most 6 processes, `nice -n 19`):
- Tree `review/trees/r5`, from the patch at f7d5a0c, and everything below built from it.
- Core libraries: plain r5, ASan r5.
- Python modules: ASan r5 and plain r5. `ygopro.cpp` was recompiled for each, because `card` changed layout.
- `edison_rules`: plain r5, ASan r5, ASan r5 with the 090D fix, and the 18 mutants.
- Return-check logging builds: `review/retlog/{r3,r5}.so`.

**Suites:**

| Build | Result |
|---|---|
| plain r5 | 535/535 |
| ASan r5 | aborted at CONTROL 090D (BUG above) |
| ASan r5 with the 090D fix | 535/535, 0 sanitizer reports |

**Duels:**

| Specs | Duels | Build | Result |
|---|---|---|---|
| `specs3` (20 crash duels from review 1) | 20 | ASan r5, plain r5, retlog r3/r5 | all ok on r5 |
| new, seeds 92004000–92005499 (`specs4`) | 1500 | ASan r5 | 1500 ok, 0 sanitizer errors |
| the 1000 Doom Lord duels of `specs4` | 1000 | retlog r5 | 8 deleted-banisher returns, all returned |

**Sanitizer errors seen:** only the 090D test read above.
