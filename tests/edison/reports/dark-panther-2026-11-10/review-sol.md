<!-- Sol (gpt-6.1-sol) review of c9efc60, Nov 10-11 2026, second brief (context in docs/edison-dark-panther.md, Reviews). Converted to UTF-8: the Windows CLI garbled its dashes and quotes, restored here by context. Paths under sol-review/ are in Sol's worktree and are not committed. -->

Engine bookkeeping r3: safe to merge for the legal Edison script paths reviewed. Dark Panther override: not safe to merge as it stands; fix the resolution-time control check and strengthen 089F before recertifying.

Reviewed detached commit c9efc60 and the three requested diffs against 3dd4daf. No commits or changes to project sources. Commands below run in WSL from this worktree root; Python means `OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 /home/mada/ygo-agent/.venv/bin/python`. Review code, extracted sources, executables and logs are in `sol-review/`. I did not repeat the established sanitizer crash investigation.

**BUG 1 — A chained control change still lets the opponent receive the copy. Legal Edison game; also a disagreement with the supplied DP03 control condition.**

At `historical_scripts.h:1751`, resolution checks face-up status and effect relations, but never checks `c:IsControler(tp)`. `RESET_CONTROL` only removes effects that already exist when control changes.

Reproduction uses only real, unmodified card scripts: player 0 activates Dark Panther targeting player 1's Bazoo. Player 1 chains its previously Set Enemy Controller, Tributing its Blue-Eyes and targeting Panther. Enemy Controller takes Panther at Chain Link 2; Panther resolves at Chain Link 1. The actual fixed override gives the now-opposing Panther Bazoo's name and ignition effect:

```text
After CL2 Enemy Controller then CL1 Panther: controller=1 alias=40133511 copied_effects=1
```

The pinned database says the copy applies “while you control this face-up card,” consistent with the Edison text paraphrased in the report. Player 0 no longer controls it at resolution, so the copy should not apply. This is an existing stock-script defect that the new control-change fix incompletely addresses.

Evidence: `sol-review/fixture.cpp`, `sol-review/logs/chain-control.txt`.

```bash
bash sol-review/build.sh baseline sol-review/fixture.cpp sol-review/fixture-baseline
sol-review/fixture-baseline chain-control trace
```

Add a controller check at resolution and a chained Enemy Controller regression. Keep the three `RESET_CONTROL` resets: they are still needed for control changes after resolution. None of the committed 089 cases chains a control-changing effect.

**BUG 2 — 089F passes when the copied effects survive control changes. Legal Edison game on the mutant.**

`panther_copy_tests.h:271-277` checks the original card identity and `QUERY_ALIAS`, then stops. It never checks the copied effects. My mutant removes `RESET_CONTROL` from only the `CopyEffect` argument, leaving it on the name change and End Phase cleanup effect. The complete committed test 089 still reports **12 PASS, 0 FAIL**.

A separate legal-card fixture demonstrates the error: copy Bazoo, give Panther away with Creature Swap, then use Brain Control to take it back in the same Main Phase. A monster in the player's Graveyard makes Bazoo's retained effect usable.

| Build | Name after regaining Panther | Copied effects remaining | Activation options from Panther |
| --- | ---: | ---: | ---: |
| Fixed r3 and current override | 43237273 | 0 | 0 |
| Copy-reset mutant | 43237273 | 1 | 1 |

Thus the passing mutant has a playable wrong effect, not merely an unused bookkeeping discrepancy.

Evidence: `sol-review/prepare.py`, `sol-review/mutant-proof.cpp`, and logs `mutant-089.txt`, `proof-baseline.txt`, `proof-mutant.txt`.

```bash
python sol-review/prepare.py
bash sol-review/build.sh copy-control-mutant
sol-review/copy-control-mutant/rules /home/mada/ygo-agent/assets/locale/en/cards.cdb /home/mada/ygopro-scripts --test=089
bash sol-review/build.sh baseline sol-review/mutant-proof.cpp sol-review/proof-baseline
bash sol-review/build.sh copy-control-mutant sol-review/mutant-proof.cpp sol-review/proof-mutant
sol-review/proof-baseline
sol-review/proof-mutant
```

089F should identify the copied Bazoo effect before the swap and require its removal afterward. Offering or activating a retained effect after regaining Panther provides an additional behavioral check. The existing “No RESET_CONTROl” mutant changes all three resets simultaneously, so its failure does not establish that each reset is tested.

**CLAIM 3 — Two report statements need more precise wording.**

| Statement | Correction and evidence |
| --- | --- |
| “Of the 26 scripts that copy effects…” in “What this changes” | There are **27** installed card scripts calling `CopyEffect`, `ReplaceEffect` or `MajesticCopy`: 26 using the first two APIs, plus Majestic Star Dragon using `Duel.MajesticCopy`. Five are in the Edison pool. None of the 27 is in code list v1, so the conclusion about those training lists remains supported. Run `python sol-review/audit.py`; see the final lines of `logs/audit.txt`. |
| The first round ran the gates “with the same results” | This is accurate for passing status, not identical certification results: the first round recorded **603 PASS**, 89 scripts, and binary `1ae071be...`; the final round records **605 PASS**, 90 scripts, and `5a715b9b...`. Say “the same gate outcomes; certification expanded from 603 to 605 cases.” Run `python sol-review/evidence.py`, or inspect `git show 0e6ba2c:docs/edison-dark-panther.md`. |

The reviewed sections' other numerical gate/soak claims agree with the evidence I checked: 111 source hashes match this checkout; the fixed executable, module and r3 library match the supplied hashes; the five soak rows' counts and decisions agree; observation comparisons total 15,708 decisions. I also read the existing raw copier logs: all 21 differences are in seeds 80720000-80720099, and all 400 override-only versus fixed records agree. Those are comparisons of the recorded fields, not a proof that every internal game state is identical.

The narrower 089F observation in the report—Panther's name returns after Creature Swap—is supported. Extending that observation to proof that acquired effects ended is not supported, as BUG 2 demonstrates. The general control-condition fix also needs the qualification in BUG 1.

**NOTE 4 — The r3 lifetime repair is correct for the reviewed legal paths; it is not a general defense against arbitrary mutation of registered effects.**

I extracted f969296, applied early-trigger.patch and edison-hardening.patch, and applied the recipe's header inserts under `sol-review/core/`. All resulting headers match the installed r3 package. Removing the three r3 additions from the patch leaves the r2 patch byte-identical. Evidence: `python sol-review/core-audit.py`, `logs/core-audit.txt`.

The relevant lifetime paths are:

- Card registration inserts count-limited effects into `rechargeable`; field-only registration does likewise.
- `RemoveCountLimit` now removes that membership as it clears the flag. Both historical scripts call it after registration, including when their initializers run through `CopyEffect`.
- Card and field-only removal now erase membership unconditionally before queuing deletion. Leaving a card's field-effect range temporarily is different: the card still owns the effect and its recharge membership.
- Turn start deletes queued effects before traversing `rechargeable`. The other immediate deletions in copying/replacement concern `uncopy` effects rejected before registration. Duel destruction/clear discards the whole field.

I found no further legal Edison route leaving a destroyed effect in this set. The new erase calls are valid for absent members and repeated removal.

For an effect that remains alive and flagless, `effect::recharge()` was already a no-op. Therefore removing its membership changes no intended game rule, even in a game that never destroys it. It can change allocation/container ordering; identical future traces are not a construction-level guarantee. A later `SetCountLimit` would fail to restore membership, but neither historical script nor another reviewed pool script performs that sequence. This is the already-reported unsupported API case, not a legal-game BUG.

I also checked the other field-level effect collections and their readers:

| Collection | Removal dependency and conclusion |
| --- | --- |
| `pheff`, `cheff` | Removal depends on current reset bits. A post-registration `SetReset` can invalidate it in principle. The real Divine Neos callback changes an ATK effect whose resets do not contain phase/chain membership, so it does not trigger that failure. |
| `oath` | Removal depends on OATH. `SetProperty` preserves COUNT_LIMIT and FIELD_ONLY through INTERNAL_FLAGS, but does not preserve OATH. The pool's dynamic property setters do not clear OATH on an effect registered in this map. |
| `spsummon_count_eff` | Removal depends on the effect code; no relevant registered-effect code mutation found. |
| Aura/action containers, field/card indexers, `grant_effect` | Type/code mutation could invalidate removal or indexing. No such live mutation route found in the reviewed Edison scripts. |
| Disable-check collections | Hold cards, not the deleted copied effect objects. |

The core's additional `RESET_CHAIN` assignment adds a bit rather than clearing membership. The early-trigger and unchanged r2 changes do not introduce another effect-deletion route. Arbitrary planted scripts can still break these collection assumptions; I did not pursue those cases. The known reason-effect defect is outside this requested re-review and is not evidence that r3 fails to repair `rechargeable`.

The conservative script scan is saved in `audit.py`; its callback/property candidates were inspected alongside the core setters and registration/removal code. It is a lexical audit, not a formal Lua analysis.

**NOTE 5 — The flag's other requested interactions follow the supplied rulings.**

| Situation | Result and basis |
| --- | --- |
| Copy the opponent's Dark Panther | Card flag 43237273 blocks the fresh copied effect; 089C also verifies availability next own turn. Correct under the supplied UDE ruling. |
| Activation negated while Panther stays on the field | Light and Darkness Dragon negates the activation; Panther is not offered again. The cost flag and the original soft count both remain spent. My `negate` fixture confirms this. Correct for a once-per-turn effect use. |
| Negated activation destroys Panther, then it returns | Leaving the field resets the flag and native effect count. It is a new field instance and can use its effect. No new restriction introduced by this flag. |
| Panther copies Phantom, then Phantom's acquired effect copies a Graveyard Panther | One use of each distinct effect is allowed. The next acquired Panther effect sees the already-spent 43237273 flag and is blocked. My `dp-poc` fixture confirms this. |
| Phantom copies a Graveyard Panther, then uses Panther to copy the opposing Panther | Phantom's 30312361 flag and Panther's 43237273 flag track separate uses. The fresh Panther copy is blocked. My `poc-dp` fixture confirms this. |
| Control changes after resolution, including taking Panther back with Brain Control | Name and acquired effects reset; the usage flag survives. The original count limit also survives control changes. My fixed `proof-baseline` fixture observes no activation after taking it back. Correct; the distinct before-resolution case is BUG 1. |
| Flipped face-down and face-up in the same turn | RESETS_STANDARD removes the flag and copied effects; the core recharges the native count on the face-down reset. 089E observes renewed availability. Correct under the supplied second UDE ruling. |
| Two Panthers on one side | Flags are per card, so each can use its effect once. 089D confirms this. |

Commands: build `sol-review/rules-cases.cpp` with `build.sh baseline`, then run the resulting executable with `negate`, `poc-dp`, and `dp-poc`. Results are in `logs/rules-cases.txt`. These use real stock Phantom and negation scripts, with the current historical overrides. I did not fetch or independently authenticate historical rulings; the review uses the supplied UDE/DP03 premises and the pinned database text.

**NOTE 6 — What each committed 089 case proves.**

| Cases | Assessment |
| --- | --- |
| A/B, stock and Edison | Require the expected copied Standby effect, correct name, expected limit/membership, subsequent deletion and restored name. Catch the original r2 bug, but fail first on the target's harmless flagless entry before copying. This proves the stronger invariant, not a direct freed-memory regression. |
| C, stock and Edison | Catch the repeat and a never-expiring flag; check the opponent's independent use and next-own-turn availability. No activation-negation or control-change chain is exercised. |
| D | Checks each card's independent use. Bazoo has no available Graveyard cost in this fixture, so it cannot masquerade as an extra Panther activation. |
| E | Checks renewed availability after a real Book of Moon/Flip Summon sequence. It stops before a second activation; it does not independently prove a second resolution. |
| F | Correct name assertion, insufficient copied-effect assertion: BUG 2. |

`check_rechargeable()` checks membership in the duel's live-effect registry before dereferencing each pointer. That is correct within the helper. It checks only members, not missing membership or ownership of a particular copied ability. It also runs at idle prompts, after turn-start processing has already read the set; it cannot prevent an earlier engine read. On r2 its first-prompt flag assertion avoids that later hazard. The two unconditional removal hunks remain defensive and are not independently exercised by these normal r3 cases.

**What I ran**

- Hand builds with `g++ -std=c++17 -O0 -g`, installed read-only r3 core/Lua/SQLite libraries: the full committed rules suite with my copy-reset mutant; baseline chain-control fixture; baseline and mutant swap/Brain proof fixtures; baseline negation/Phantom fixtures. Build scripts and compiler logs are under `sol-review/`. No xmake, installation, network fetch, or outside-worktree writes.
- Fixed supplied executable, SHA-256 `5a715b9b...`, `--test=089`: **12 PASS, 0 FAIL**, saved in `logs/fixed-089.txt`. My independently built mutant: **12 PASS, 0 FAIL**, despite the demonstrated retained effect.
- Forward-model refusal gate, with TMPDIR inside `sol-review/tmp`: **7 of 7 refused**. Reproduce with `bash sol-review/refusal.sh`; output in `logs/refusal.txt`.
- Fixed override chain-control fixture: wrong copy observed after the steal. Swap/Brain proof: fixed 0 retained effects/options, mutant 1/1. Negation and both Phantom orders: all expected use limits observed.
- All new custom fixture duels used RNG input seed **86650000**, set in the isolated fixture harness. Existing certification gate/mutant cases retain their prescribed fixture seed **20100424**. No new production/environment soak games were run; other seeds mentioned above were read from existing evidence.
- Read-only source/header/hash, script inventory and raw evidence audits: `core-audit.py`, `audit.py`, `evidence.py`, with results under `logs/`. No sanitizer rebuild or re-execution of the previously established crash investigation.
