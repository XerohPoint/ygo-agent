> Independent review by a Claude subagent, November 10, 2026, of `cd8b92e` and test 089 in `0e6ba2c`, kept as written except that a
> card-text quote is paraphrased. Paths under `review/` are in that reviewer's worktree and are not committed. What was done about each
> finding is in [docs/edison-dark-panther.md](../../../../docs/edison-dark-panther.md#reviews).

Core fix (hardening r3): safe to certify; I found no remaining use-after-free from copying and no change in play. Dark Panther override: safe to certify; it follows both UDE rulings in every case I ran. Separately, a use-after-free that was already there before these fixes, through Gold Sarcophagus, still happens in legal games; neither commit touches it.

Independent review of `cd8b92e` (and, as asked later, test 089 in `0e6ba2c`), 2026-11-10. My worktree stays at `cd8b92e`, with no commits. Everything I built or ran is under `review/` (builds and logs in `review/build/`, the fixture driver in `review/fixture/`, scripts in `review/scripts/`). I did not use xmake, fetch anything, or write outside this worktree. At most 6 processes ran at once, all under nice 19.

Most commands below assume `B=review/build` and `F=review/fixture`, run from the worktree root, with `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:quarantine_size_mb=1024` set.

## Findings that a legal Edison game can reach

### 1. GAP: a use-after-free that already existed, through a card's stale `reason_effect` (Gold Sarcophagus and others). Neither commit fixes it.

On the r3 ASan build, 3 of the 1,600 no-copier duels I sampled stopped with a heap-use-after-free that has nothing to do with copying:

- 86662165: fl1824 against fl50771
- 86662582: fl68110 against fl85534
- 86663122: fl1824 against fl50771

All three are eager play. `interpreter::push_param` reads an effect that `process_turn` deleted at an earlier turn start (`processor.cpp`, the `reseted_effects` loop). In each case the freed effect is Gold Sarcophagus's delayed player effect (`c75500286`: FIELD+CONTINUOUS, `EVENT_PHASE+PHASE_STANDBY`, reset `RESET_PHASE+PHASE_STANDBY+RESET_SELF_TURN`, count 2). Lua receives it as `re`, the sixth parameter, in a condition (86662165) or an operation (86662582, 86663122; in 86663122 the operation is Tytannial's).

What happens:

1. The delayed effect calls `Duel.SendtoHand(tc, …)`, which stores the effect in `tc->current.reason_effect`.
2. The effect expires and is deleted at a later turn start. The card still points to it.
3. Some later moves keep the old pointer: `destroy` with a null reason effect, and `send_to` with `PLAYER_SELFDES`. Their events then pass the freed pointer to Lua as `re`.

A trace build confirms the stale card pointers at the turn start. The cards were ones Gold Sarcophagus had added to hand (Supervise, D.D.R., Brain Control, Trap Dustshoot, Call of the Haunted), and also Jinzo, which pointed to Jinzo - Returner's player effect.

- **Pre-existing:** all three duels fail the same way on my r2 ASan build and on the investigation's r2 ASan module (`85a5bebaa179…`, 3dd4daf scripts).
- **Effect on play:** the fixed and certified modules play all three duels identically.
- **What production probably does:** it reads 4 bytes, `ref_handle` at offset 0. Without ASan the slot is probably reused, so `re` becomes some other Lua object or nil. That is a wrong-ruling or Lua-error risk, not a write. This is inferred, not observed.
- **Exposure:** 108 corpus lists play Gold Sarcophagus in the Main Deck.
- **Possible fix (not part of this review):** at the turn-start delete, clear `current.reason_effect` and `temp.reason_effect` in any card that points into `reseted_effects`.

Evidence:

```bash
python3 review/scripts/batch_summary.py $B/runs asanr3 t4      # 3 heap-use-after-free, all no-copier duels
bash $B/runmany.sh asanr2 asanr2 $PWD/$B/specs/x-86662165.json  # same on r2 (also asanold = investigation's r2 module)
grep -E "card_(temp_)?reason_effect_deleted|@@stale_param" $B/runs/tr2-x-86663122.err   # trace build out-trace
```

### 2. RULING: the copy stays after a control change, but the text says "while you control this face-up card". The bug was already in the stock script; the override keeps it.

The DP03 text that applied in Edison (paraphrased here) gives Dark Panther the selected monster's name and effects until the End Phase, while you control it face-up. The current text in `cards.cdb` has the same condition. The script resets the copy, the name change and `e2` with `RESET_EVENT+RESETS_STANDARD+RESET_PHASE+PHASE_END`. That reset does not include `RESET_CONTROL`.

- **What I observed:** fixture R5c. Dark Panther copies Blue-Eyes, then Creature Swap gives it to the opponent. Under the opponent it still has Blue-Eyes's name: `QUERY_ALIAS` returns 89631139. This happens with the override and with the stock script.
- **Reachable:** yes. Creature Swap or Enemy Controller after the effect is used, in the same turn.
- **Impact:** small. The new controller keeps the copied name and effects until the End Phase.
- **The flag itself is right:** no reset on a control change, the same as the count limit. Once Dark Panther has used its effect, it stays used that turn even if it changes control and comes back (R5c "A offered after regaining it: 0" with both scripts).
- **Possible fix:** add `RESET_CONTROL` to `e1`, the `CopyEffect` reset and `e2`. I did not test this.

```bash
$F/fixture-r3inv assets/locale/en/cards.cdb "--only=R5c " --trace
```

### 3. NOTE: legal games can reach the bug by more routes than the investigation found. r3 closes every one of them.

The investigation said Divine Neos and Pseudo Space "cannot target either card" and did not mention Majestic Star Dragon. These copy chains are legal and reproduce the freed read on r2. Each one is clean on r3 (fixtures M1-M11; the r2 stale holder is shown on the right):

| Route | r2 stale holder |
| --- | --- |
| Dark Panther copies Treeborn Frog or D-END (M1, M2) | Panther |
| Phantom of Chaos banishes its own Treeborn or D-END (M3, M4) | Phantom |
| Phantom banishes a GY Dark Panther, then copies the opponent's Treeborn (M5) | Phantom, copy_id 2 |
| Divine Neos banishes Dark Panther as cost, then copies Treeborn (M6) | Divine Neos, copy_id 2 |
| Majestic Star Dragon negates and clones the opponent's Dark Panther, then copies Treeborn (M7) | MSD |
| Dark Panther copies the opponent's Phantom, then banishes its own GY Treeborn (M8) | Panther, copy_id 2 |
| The copy ends early: Book of Moon (M9), Dark Hole (M10); two separate turns (M11) | Panther |

MSD cloning Treeborn directly (M12) is safe on both builds. `MajesticCopy` clones the effect, and `add_effect` gives the clone the default limit back.

In random corpus games on r2, every stale entry I saw was a Dark Panther copy: 53 entries. That includes fl933 (MSD + Treeborn) against Dark Panther lists, through the plain route.

```bash
bash $F/run_fixtures.sh r2inv r2; bash $F/run_fixtures.sh r3inv r3    # summaries: $B/fixture-runs-r{2,3}.txt
```

### 4. GAP, closed by 0e6ba2c: cd8b92e by itself left the r2 pins in place

At cd8b92e, three things still named r2:

- `tests/edison/assets.lock.json` pinned the r2 patch (`9adf1905…`), so `tests/edison/run.py` refuses.
- `tests/edison/conformance/xmake.lua` required r2.
- `tools/edison/forward_model/build.sh` expected `edison_hardening = "r2"`. Against the r3 `xmake.lua` it refuses.

0e6ba2c updates all three. I checked that the new forward-model pin `984301c4…` equals the r3 package's `libygopro-core.a`.

The search binary `167be8aa…` used for M2 and M3 is still built on r2, as `docs/edison-dark-panther.md` says. Any search build from before r3 can still reach the freed read with Dark Panther lists. It needs a rebuild before Stage B uses search with those lists.

### 5. NOTE: no change in play. The rules logic is identical by construction; heap addresses are not.

**Rules logic.** The only reader of `effects.rechargeable` is the turn-start loop. That loop calls `effect::recharge()` on each member, and `recharge()` does nothing unless `EFFECT_FLAG_COUNT_LIMIT` is set. So r3 can only change which effects get recharged by removing effects that have no flag:

- **RemoveCountLimit hunk:** it removes exactly the effects whose flag it has just cleared.
- **The two remove_effect hunks:** in r3, nothing in the set can lack the flag. `RemoveCountLimit` is the only path that clears the flag after registration; `SetProperty` keeps `INTERNAL_FLAGS`, and that includes `COUNT_LIMIT`. So these hunks change nothing; they are only a safeguard.

One exception would break this: a script that calls `SetCountLimit` on a live effect after `RemoveCountLimit`. r2 would recharge that effect; r3 would not. No pool script does this; only the Treeborn and D-END `initial_effect`s call `RemoveCountLimit`, and as their last call.

**Heap addresses.** r3 frees a hash node on every `RemoveCountLimit`, which runs for each Treeborn and D-END at duel start. The override adds a flag effect and Lua allocations. Later addresses can therefore differ. The core has pointer-ordered containers that can reach the order of choices:

- `effects.pheff`, which feeds the control-return list in `process_phase_event` (`processor.cpp:1045`);
- each card's `indexer` in `card::reset`;
- the `std::set` `reseted_effects`.

So identical play cannot be proved by construction. It held in every comparison:

| Comparison | Seeds | Result |
| --- | --- | --- |
| Fixed and certified modules, no copier in either list | 86662000-86663599 | 1,600 of 1,600 identical. About half of the 319 pairs hold Treeborn Frog or D-END; 10 of them use the D-END list fl28337 |
| Fixed and certified, Dark Panther lists against random lists | 86664000-86664299 | 300 of 300 identical |
| Crash replays | recorded seeds | 5 of 6 identical. 88515054 differs as intended: 74 Dark Panther activation choices drop to 12 |
| Certified module, address randomization off (`setarch -R`) against on | 6 specs, 600 duels | 600 of 600 identical. Memory layout did not reach any decision in this sample |

```bash
python3 review/scripts/cmp_traces.py $B/runs fixed cert t4-     # and t4p-, and cna cert t4-00
```

### 6. NOTE: on test 089 (0e6ba2c), and whether a build that still has its bug can pass it

I built 0e6ba2c's `edison_rules` by hand against my ASan cores (`$B/t089/build.sh`) and ran `--test=089`:

| Build | Result |
| --- | --- |
| r3 | 10 of 10 PASS |
| r2 core | 089A and 089B fail, as `mutants.txt` says |
| Override never resets at End Phase (`RESET_EVENT+RESETS_STANDARD`; `t089-noend`) | **10 of 10 PASS.** No 089 case checks that a used Dark Panther is offered again on its controller's next turn. 089C turn 2 looks only at the opponent's unused Dark Panther. This build is wrong: my R1 "next own turn" gives 0 instead of 1, and R5/R5b also break |
| Override without `RESETS_STANDARD` (`t089-nostd`) | 089E fails, as intended |
| r3 without the two remove_effect hunks (`out-mb`) | 10 of 10 PASS. The safeguard hunks are untested, which is harmless (finding 5) |
| r3 without the RemoveCountLimit hunk, keeping the remove_effect hunks (`out-mc`) | 089A and 089B fail, but this build is memory-safe on all 12 memory fixtures |

So 089A and 089B test the flag invariant, not freed memory. On r2 they fail at the first prompt on Treeborn's own harmless flagless entry, before any copy is deleted. The check for "a deleted effect in the set" never runs in either mutant. No 089 case can pass with the original core bug. A broken override that never resets its flag passes every case.

Three smaller points about 0e6ba2c:

- **The lock pins the stock Dark Panther file, not the override.** Its `c43237273.lua` hash, `570cd2dc…`, is the stock ygopro-scripts file. The override (`dd7ada5d…` over its Lua text) is covered only by the source hashes in the report.
- **The certification harness reads past its own buffer.** `harness.h`'s `respond(bytes)` uses a 64-byte stack buffer, but `duel::set_responseb` copies `SIZE_RETURN_VALUE` = 512 bytes. ASan reports a stack-buffer-overflow read on the first card selection. This was already true at 3dd4daf. My copy uses 512 bytes. Production `resp_buf_[128]` over-reads the same way, but inside its object; it never tripped ASan.
- **Cases the doc lists as having no fixture:** "Phantom copying Treeborn/D-END, Phantom copying Dark Panther, a Dark Panther that changes control after use". My fixtures cover them: M3-M5, R3, R4, R5, R5b, R5c.

```bash
$B/t089/rules-r3 assets/locale/en/cards.cdb /home/mada/ygopro-scripts --test=089      # also rules-r2, rules-mb, rules-mc, ../t089-noend/rules, ../t089-nostd/rules
```

### 7. Dark Panther override against the rulings (no finding)

The override is the stock script plus a card-flag cost, as the brief says (`diff` against `/home/mada/ygopro-scripts/c43237273.lua`). The fixtures below run with the override and with the stock script (`--stock-panther`):

| Case | Override | Stock | Ruling |
| --- | --- | --- | --- |
| R1: copies the opponent's Dark Panther | Not offered again; offered next own turn | Offered again; R8 activates 6 times in one turn | UDE: the copied effect is the one just used. Override right |
| R2a: activation negated by Divine Wrath, then Call of the Haunted | The revived card is offered | Same | A new card. Right |
| R2b: effect negated by Forbidden Chalice | Not offered again | Same | Used once per turn. Right |
| R3: copies the opponent's Phantom, then banishes its own GY Dark Panther | Phantom copy offered; the Dark Panther copy after it is not | Dark Panther copy offered | UDE logic. Override right |
| R4: Phantom banishes a GY Dark Panther, then copies the opponent's Dark Panther | Not offered again; Phantom copy not offered | Offered again | UDE logic. Override right |
| R5: Brain Control on the opponent's Dark Panther, which copies the other one | Not offered again; both offered on the owner's turn | Offered again | Right |
| R5b, R5c: control changes after use | Not offered after regaining it; offered next turn | Same | Right. But see finding 2 for the copy itself |
| R6: Book of Moon, then Book of Taiyou, same turn | Offered again; blocked after the second use | Same | UDE ruling 2. Right |
| R7: two Dark Panthers on one side | Each once | Same | Right |

Two things I could not verify offline. The report names Konami FAQs tid 41308/41305 on copying Phantom of Chaos but does not quote them. The stock script's "targets" (`EFFECT_FLAG_CARD_TARGET`) reading of the old "select" wording is also unchecked. The override changes neither.

## Findings that the current pool cannot reach

### 8. GAP: the other field-level sets have the same weakness

Whether an effect is removed from these sets depends on fields that can change after `RegisterEffect`:

| Set | Decided by |
| --- | --- |
| `pheff`, `cheff` | `reset_flag` (changed by `SetReset`) |
| `oath` | `EFFECT_FLAG_OATH` (changed by `SetProperty`) |
| `spsummon_count_eff` | `code` (changed by `SetCode`) |
| `grant_effect` and the effect containers | `type` (changed by `SetType`) |

Clearing such a field after registration would leave a deleted effect in the set. `reset_phase`, `reset_chain` and the `processor.cpp:1045` loop dereference `pheff` and `cheff`. `oath` keys are dereferenced only when their value matches.

r3 fixes only `rechargeable`. A cheap way to close the rest would be to erase from every set without a condition. `field.cpp:3445` only adds `RESET_CHAIN`, so it cannot cause this.

Why I think no pool script reaches it:

- **Static scan (`review/scripts/post_register_scan.py`, `receiver_scan.py`):** the only changes after registration are `RemoveCountLimit` (Treeborn, D-END) and dynamic `SetProperty(CARD_TARGET/PLAYER_TARGET)` on the activating effect. `SetProperty` keeps the internal flags, and none of those 19 scripts uses OATH. Divine Neos's `rstop` calls `SetReset` on a registered effect, but it carries neither `RESET_PHASE` nor `RESET_CHAIN` before or after.
- **Dynamic check:** my invariant check runs at every turn start. It looks for a deleted effect in any set, indexer, oath key or value, grant entry or card indexer, and for a live member whose flag no longer matches its set. It reported nothing on r3 over about 3,175 ASan duels, nor over any r3 fixture.

### 9. GAP: SetCountLimit after RemoveCountLimit

On r3, an effect that gets `SetCountLimit` after `RemoveCountLimit` is not recharged at turn start; on r2 it was. No script does this.

## What I ran

**Sources and builds.** All are by hand with `review/build/build.sh`: `-g -O1 -fsanitize=address`, Lua 5.5.1 from xmake's cached tarball (identical to the installed headers), and `ygopro.cpp` from cd8b92e.

- **Core sources.** `review/build/core` is `git archive f969296` from xmake's cached clone, plus `early-trigger.patch`, plus r3 from cd8b92e, plus the recipe's header inserts. Every `.cpp` and `.h` is byte-identical to xmake's patched tree. The headers are identical to the r3 package `792b98d4`. The code differs from the investigation's `core-fix` only in comments.
- **Variants:**
  - `core-inv`: r3 plus the turn-start invariant check.
  - `core-r2inv`: the investigation's r2 sources plus the same check.
  - `core-trace`: `core-inv` plus deletion, stale-parameter and stale-`reason_effect` logging.
  - `core-mb` and `core-mc`: the two partial-r3 mutants.
- **Modules:** `out-r3inv` (`6a6f3c15…`), `out-r2inv` (`f3d33d0c…`), `out-trace`, and the investigation's r2 module copied into the `run-asanold` tree (`85a5beba…`).
- **Production modules,** read-only copies in their own trees: fixed `75d1967b…` and certified `b8fcd2bc…`, both checked by sha256.

**Fixture driver.** `review/fixture/` places cards, then follows a scripted plan. It reads any card or script, with the historical overrides. It has 24 scenarios: M1-M12 for memory, R1-R8 for rules (with R2a/b and R5b/c). Each ran on r3, on r2, on r3 with the stock Dark Panther, on mb, on mc, and with the never-expiring flag override. Results are in `review/build/fixture-runs-*.txt` and `fixture-runs/<variant>/<case>.txt`. On r3 there was no ASan error and no invariant report. On r2, M1-M11 each give a heap-use-after-free plus `deleted_in_rechargeable`.

**Crash replays** (recorded seeds 88264285, 88515054, 88707769, 88711111, 88715221, 88721413):

| Build | Result |
| --- | --- |
| r3 ASan | 6 of 6 clean, no invariant report |
| r2 ASan | 6 of 6 heap-use-after-free |
| Fixed module | 6 of 6 finish; traces match certified except 88515054 |
| Certified module | 6 of 6 finish this time |

**New games.** All use env seeds inside 86660000-86669999, with the policy chosen by seed % 3 as in `pair_spec.py`. Specs come from `review/scripts/mkspecs.py`.

| Seeds | Games | Runs and results |
| --- | --- | --- |
| 86660000-86660399 | fl41606 against fl41081 | r3 ASan: 400 of 400 clean. r2: 20 of 20 chunks stopped by the freed read (87 finished) |
| 86661000-86661999 | 160 pairs: each of the 14 copier lists against 11 Treeborn/D-END lists, plus fl933 against the Dark Panther lists | r3 ASan: 1,000 of 1,000 clean. r2: 30 of 50 chunks stopped (624 finished) |
| 86662000-86663599 | No copier in either list | Fixed and certified: 1,600 identical. r3 ASan: 1,469 finished, 3 stops from finding 1 |
| 86664000-86664299 | Dark Panther lists against random lists | Fixed and certified: 300 identical. r3 ASan: 300 clean |

Also: the certified module with `setarch -R` on 86662000-86662599 (600 identical), and the fixture core RNG seed 86669999. Test 089 runs use its own harness seed, 20100424.
