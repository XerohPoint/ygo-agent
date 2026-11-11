# Edison Dark Panther crash and repeat fix

Prepared November 10-11, 2026, on branch `panther-fix` from `edison` at `3dd4daf`. Not pushed. Both reviews are done; see [Reviews](#reviews).

**Result: the corpus soak's crash was a heap use-after-free that our own core patch caused, and it is fixed in `edison-hardening` r3. Neo-Spacian
Dark Panther could use its once-per-turn effect again and again by copying the opponent's Dark Panther, kept a copy after a control change, and
gave the copy to an opponent who took it in the chain; an Edison override now stops all three. The production module is now `7e39a113...`,
replacing `b8fcd2bc...`. Certification passes 607 of 607. On the same seeds, the fixed and old modules play all 3,376 soak duels identically. No
card in code list v1 copies effects, so no M1, M2 or Stage A game could reach these bugs. The first review found a separate use-after-free, already in
the certified engine, that training lists do reach through Gold Sarcophagus; it is [reported below](#found-in-review-not-fixed-here) and not fixed
here. It was fixed afterwards in r5; see [edison-gold-sarcophagus.md](edison-gold-sarcophagus.md).**

The investigation is in [investigation.md](../tests/edison/reports/dark-panther-2026-11-10/investigation.md), and the evidence is in
[reports/dark-panther-2026-11-10](../tests/edison/reports/dark-panther-2026-11-10/).

## The crash

The corpus soak ([edison-corpus.md](edison-corpus.md)) crashed in 1 of 254,016 duels. Its probes then crashed 4 times in 24,000 games of the same pair, always with
Dark Panther in play. An AddressSanitizer build of the production sources fails on every replay of the six recorded crash duels with a
heap-use-after-free read in `field::process_turn`. I reproduced it on the first crash duel.

- **Our patch caused it.** `Effect.RemoveCountLimit` comes from `early-trigger.patch`. The historical Treeborn Frog and D-END scripts call it
  after `RegisterEffect`, which has already put the effect in `field::effects.rechargeable`, the set the turn start recharges. It cleared the
  count-limit flag and left the effect in the set. `card::remove_effect` and `field::remove_effect` only erase an effect from that set while the
  flag is set.
- **Dark Panther only triggers it.** Treeborn Frog's own effect is never deleted, so its stale entry did no harm. When Dark Panther copies a
  face-up Treeborn Frog or D-END, the copy runs the historical script on Dark Panther. The copy is reset in the End Phase and deleted at the next
  turn start, and it stays in the set. Every later turn start then reads freed memory and can write one byte into whatever reuses it. A crash
  needs that byte to land in a live object, which is why the same duel crashed only some of the time.
- **On production modules,** the six crash duels, 10 runs each, died 2 times in 60 on the old module and 0 times in 60 on the fixed one.

## The repeat

The stock Dark Panther script gives its copy effect a count limit. A count limit belongs to one effect object. When Dark Panther copies the
opponent's Dark Panther, `CopyEffect` registers a new copy effect with an unused limit, so the card can activate again, and again after that.
One player in a crash duel used it 32 times in one turn. A UDE ruling, listed by Yugipedia as previously official, says the copied effect is the
one Dark Panther just used, so it cannot be activated again that turn. The same rulings let a Dark Panther that is flipped face-down and face-up
again use its effect again. No Konami FAQ on this case was found. This repeat did not cause the crash.

The independent review found one more Dark Panther defect, in the stock script too. The DP03 text, which applied in Edison, gives the copied name
and effects only while you control Dark Panther, but the script did not end them on a control change. After Creature Swap, the opponent's Dark
Panther kept the copied name until the End Phase.

Sol's review found the other half of that defect. The copy never checked who controls Dark Panther when it resolves, so if the opponent chains
Enemy Controller to take it, the copy applies to the opponent's new Dark Panther. The override now copies nothing unless the activating player
still controls it at resolution.

## The change

| File | Change |
| --- | --- |
| `repo/packages/y/ygopro-core/edison-hardening.patch` | r3. `RemoveCountLimit` also erases the effect from `rechargeable`. `card::remove_effect` and `field::remove_effect` erase it whatever the flag says. The r2 hunks are byte for byte unchanged |
| `xmake.lua`, `tests/edison/conformance/xmake.lua` | `edison_hardening` r2 to r3 |
| `ygoenv/ygoenv/ygopro/historical_scripts.h` | A Dark Panther override: the stock script plus a cost that requires and sets card flag 43237273, reset by `RESETS_STANDARD` and the End Phase, as the stock Phantom of Chaos script guards its own copy. A copy of the effect checks the same card's flag. The copied name, the copied effects and the End Phase reset also reset on a control change (`RESET_CONTROL`); the flag does not, so the effect stays used that turn. Nothing is copied unless the activating player still controls Dark Panther when the effect resolves |
| `tests/edison/panther_copy_tests.h`, `rules.cpp`, `harness.h` | Test 089. The harness pins Dark Panther's and Creature Swap's scripts, reads `MSG_SWAP`, and gains `check_rechargeable()`, which requires every member of the set to be a live effect with a count limit, checking membership before reading any flag, and `card_effects()`, the effects still attached to a card. Its response buffer is now the core's `SIZE_RETURN_VALUE`, 512 bytes; it was 64, and `set_responseb` read past it |
| `tests/edison/script_consistency_tests.h`, `assets.lock.json` | 90 pinned scripts and 28 overrides; the r3 patch hash. The lock pins the stock Dark Panther file; the override is covered by the source hashes in the certification report |
| `tools/edison/forward_model/build.sh`, `test_build_refusal.sh` | The search build now requires r3 and its library hash, and the refusal test also refuses r2 |

The r3 core sources match, byte for byte apart from comments, the tree the investigation tested under AddressSanitizer. The override is the
tested script plus `RESET_CONTROL` in three resets and the check at resolution that its player controls the card.

**Test 089**, each case with a Rule 5 control on the stock scripts:

| Case | Edison | Control |
| --- | --- | --- |
| 089A, 089B: Dark Panther copies Treeborn Frog or D-END, then two turn starts | It takes the copied name. The copied Standby effect has no limit and is not in the set. After its deletion, every member is live and the name is back | The copy keeps its default limit and is in the set |
| 089C: Dark Panther copies the opponent's Dark Panther | Not offered again that turn. The opponent's Dark Panther is offered on its own turn, and the used one on its controller's next turn | The stock script offers the copy, and it resolves a second time |
| 089D: two Dark Panthers on one side | Each activates once | The same |
| 089E: used, flipped face-down by Book of Moon, Flip Summoned | Offered again | The same |
| 089F: copies Bazoo, then Creature Swap gives it to the opponent | Its name is Dark Panther again under the opponent. The name change, the copied effect and the End Phase cleanup have left the card; only the usage flag stays | The stock script keeps all three, and Bazoo's name |
| 089G: targets Bazoo, and the opponent chains Enemy Controller, Tributing Blue-Eyes, to take Dark Panther | Nothing is copied: the name stays Dark Panther, and the usage flag is all it gained | The stock script gives the opponent's new Dark Panther Bazoo's name and effect |

Eight mutants show the cases can fail, each one edit to this commit:

| Mutant | Fails |
| --- | --- |
| Linked against the r2 core | Edison 089A and 089B, at the first prompt, on Treeborn Frog's own flagless entry |
| No override | Edison 089C, 089F and 089G |
| The flag never resets at the End Phase | Edison 089C, on the next turn. The first review found that the first version of 089 passed this mutant |
| No `RESET_CONTROL` | Edison 089F |
| `RESET_CONTROL` dropped from the name change only | Edison 089F |
| `RESET_CONTROL` dropped from the copy only | Edison 089F. Sol found that 089F as of `c9efc60` passed this mutant |
| `RESET_CONTROL` dropped from the End Phase cleanup only | Edison 089F |
| No controller check at resolution | Edison 089G |

Every other case passes on each. See [mutants.txt](../tests/edison/reports/dark-panther-2026-11-10/mutants.txt), made by
[mutants.py](../tests/edison/reports/dark-panther-2026-11-10/mutants.py). 089A and 089B check the set's
invariant, not freed memory, so on r2 they fail before any copy is deleted. The two `remove_effect` hunks are a safeguard that no case reaches: in r3
nothing in the set can lack the flag, and the first review's build without them passed every case 089 then had.

## Gates

On module `7e39a113...`, reproduced by a rebuild without xmake's object cache. Two earlier rounds ran the same gates with the same outcomes: on
`75d1967b...` before the first review's fixes, when certification had 603 cases, and on `eca27bd5...` before Sol's, with 605. The soaks, the
observation fields and the crash pair gave the same numbers in all three rounds, and the copier soak's 400 games are identical between the second
and third, so the controller check changed none of them.

| Gate | Result |
| --- | --- |
| Certification | **607 PASS, 0 FAIL**, two identical replay traces, `edison_rules` `fa236666...`, 111 sources |
| Certification traces against the certified build | All 593 earlier cases identical except 075A-C, whose counts are now 90 scripts and 28 overrides, and 081C, whose text included the run's closing summary line, now followed by 089 |
| Conformance | As recorded. All 114 test files and `results.json` byte-identical to the certified build's. `edison_conformance` is now `091eca63...` |
| `test_learner.py` | 13 of 13, every detail identical |
| Observation fields, 64 duels, uniform policy, seed 80730000 | No field differs in 15,708 decisions |
| Forward-model refusal test | All 7 configurations refused, including r2 |

Soak, fresh seeds, each run on both modules:

| Stage | Seeds | Duels | Decisions | Crashes, diagnostics, violations | Differ from the old module |
| --- | --- | ---: | ---: | ---: | ---: |
| 21 lists, Main and Extra | 80700000-503 | 504 | 114,423 | 0 | 0 |
| 21 lists, whole Side moved in | 80701000-503 | 504 | 110,041 | 0 | 0 |
| 114 confirmed crash cards | 80702000-3367 | 1,368 | 201,234 | 0 | 0 |
| Uniform decks, whole pool | 80710000-999 | 1,000 | 152,171 | 0 | 0 |
| Copiers and their targets: Dark Panther, Phantom of Chaos, Treeborn Frog, D-END, 100 each | 80720000-399 | 400 | 59,799 | 0 | 21 |

All 21 differences are in the Dark Panther block. The old module with only the override added as a script file plays all 400 duels exactly as the
fixed module does, so the override explains every difference and the core change explains none.

The crash pair, fl41606 against fl41081, on the production modules:

| Run | Old module | Fixed module |
| --- | --- | --- |
| Six recorded crash duels, 10 runs each | 2 died | 0 died. Traces identical to the old module's except duel 88515054, the 32-activation duel: 12 Dark Panther activations in all, against 74 |
| New duels, seeds 89002000-89002399 | 400 finished | 400 finished. One trace differs, 89002023, with 9 Dark Panther activations against 17 |

See [pair-results.json](../tests/edison/reports/dark-panther-2026-11-10/pair-results.json).

## What this changes

- **Results to date stand.** Of the 27 scripts that copy effects, 26 through `CopyEffect` or `ReplaceEffect` and Majestic Star Dragon through
  `MajesticCopy`, none is a card in code list v1, so M1, M2 and Stage A could not reach either bug.
  The soak's 21 training lists, and the observation fields, are identical on both modules.
- **The corpus soak's crash is explained.** All six recorded crash duels show the same freed read. Its classification in
  [edison-corpus.md](edison-corpus.md) was right to call Dark Panther the lead and not the cause.
- **Stage B can play the 7 Dark Panther lists,** on this module.
- **The M2 and M3 search binary** `167be8aa...` was built on r2 and stays the record for those runs. `build.sh` now refuses r2, so a new search
  binary on r3 is needed before Stage B uses search. It was not built here.
- **More copy routes reached the crash than the investigation found:** Phantom of Chaos banishing Treeborn Frog, D-END or a Graveyard Dark
  Panther; Divine Neos banishing Dark Panther and then copying Treeborn Frog; Majestic Star Dragon cloning Dark Panther; and copies ended early by
  Book of Moon or Dark Hole. The reviewer's fixtures read freed memory on r2 by each route and none on r3. Those fixtures, and its rulings fixtures
  for Phantom of Chaos and control changes, are in the reviewer's worktree and are not committed; 089 covers the plain route and the control change.

## Found in review, not fixed here

The first finding below was fixed afterwards in `edison-hardening` r5; see [edison-gold-sarcophagus.md](edison-gold-sarcophagus.md).

**A second use-after-free, already in the certified engine, that legal games reach through Gold Sarcophagus.** It is separate from Dark Panther
and from our patches, and neither commit touches it.

- **What happens.** Gold Sarcophagus's delayed effect adds a card to the hand, and the card keeps that effect as its `reason_effect`. The effect
  expires and is deleted at a later turn start. A later move that does not overwrite the field passes the freed effect to Lua as `re`, in an
  `interpreter::push_param` read. The reviewer traced this to Gold Sarcophagus, and once to Jinzo - Returner's player effect.
- **Training lists reach it.** Gold Sarcophagus is in code list v1, in `g_gigavise`, `g_lightsworn_fairy` and `g_zombie_teledad`, and
  `g_diva_hero` plays Jinzo - Returner. Under AddressSanitizer on the certified sources, 3 of 504 new duels of those four lists against all 21
  lists read freed memory this way, all three with `g_gigavise`
  ([gold-sarcophagus-v1.json](../tests/edison/reports/dark-panther-2026-11-10/gold-sarcophagus-v1.json)). I did not trace which effect each read.
  The reviewer found 3 in 1,600 corpus duels, and 108 corpus lists play Gold Sarcophagus.
- **What it does in production is not known.** It reads 4 bytes, a Lua reference, from freed memory. If the slot was reused, `re` can be another
  object, so a script may see the wrong reason effect or raise an error. No soak printed a diagnostic, replays of past results were
  deterministic, and the fixed and old modules play these duels identically, but that does not show the rulings in them were right.
- **So M1, M2 and Stage A games could reach it,** in a small share of games with those lists. Their results stand as measured on that engine.

Two smaller points from the review: production's response buffer, `resp_buf_[128]` in `ygopro.h`, is also shorter than the 512 bytes
`set_responseb` copies, so the core reads past it into the object's next fields and uses only the counted bytes; and the other field-level sets
(`pheff`, `cheff`, `oath`, `spsummon_count_eff`) have the same weakness as `rechargeable`, removal decided by fields a script could change after
registration, which no pool script does.

## Reviews

**Independent review** of `cd8b92e` and test 089 in `0e6ba2c`, by a Claude subagent with sanitizer builds of r2 and r3, a 24-scenario fixture
driver and 3,300 new duels: [review-claude.md](../tests/edison/reports/dark-panther-2026-11-10/review-claude.md). Its verdict: both fixes are safe
to certify, with no remaining use-after-free from copying and no change in play. What was done with each finding:

| Finding | Class | Done |
| --- | --- | --- |
| Gold Sarcophagus use-after-free, already there | GAP | Confirmed on the investigation's own r2 sanitizer build, measured on the training lists, reported above. Not fixed here; fixed afterwards in r5 ([report](edison-gold-sarcophagus.md)) |
| The copy stays after a control change | RULING | Fixed: `RESET_CONTROL`, and test 089F |
| More copy routes reach the crash | NOTE | Recorded above. r3 closes them all |
| `cd8b92e` alone left r2 pins | GAP | Already closed by `0e6ba2c` |
| No change in play by construction for the rules, not for heap addresses | NOTE | Agreed. Its 1,900 duels and mine compare identical |
| A never-resetting flag passed 089 | NOTE | Fixed: 089C now checks the controller's next turn, and the mutant fails it |
| The harness reads past a 64-byte response buffer | NOTE | Fixed in the harness. Production's buffer is reported above |
| The lock pins the stock Dark Panther file | NOTE | Stated in the change table |
| Other field-level sets share the weakness | GAP | Reported above. No pool script reaches it |
| `SetCountLimit` after `RemoveCountLimit` would not recharge on r3 | GAP | No script does this |

**Sol** (`gpt-6.1-sol`) reviewed `c9efc60`: [review-sol.md](../tests/edison/reports/dark-panther-2026-11-10/review-sol.md). Its first attempt hit
its usage limit. Its second stopped after its first reads, when OpenAI's filter flagged the brief's and the report's memory-error detail as a
possible cybersecurity risk. For the third, its brief left the memory-checker work to the investigation and the first review, and asked for the
engine fix by reading, the rulings, the tests and the report's later sections. Its verdict: r3 is safe to merge; the override was not, until two
fixes. Its fixtures and mutant builds are in its worktree and are not committed. What was done with each finding:

| Finding | Class | Done |
| --- | --- | --- |
| Enemy Controller chained to the copy takes Dark Panther, and the copy then applies to the opponent's Dark Panther | BUG | Confirmed: the new 089G fails on the `c9efc60` override. Fixed: the copy needs its player to control Dark Panther at resolution |
| 089F passed a mutant whose copied effects outlive the control change | BUG | Confirmed: that mutant passes 089F as of `c9efc60`. 089F now checks every effect the copy attached, and each `RESET_CONTROL` has its own mutant |
| 26 scripts copy effects | CLAIM | Fixed: 27, counting Majestic Star Dragon's `MajesticCopy`. None is in code list v1 |
| The first round had "the same results" | CLAIM | Fixed: the same outcomes; certification then had 603 cases |
| r3 is right for the legal paths; the other field sets depend on fields a script could change | NOTE | Agrees with the first review. No pool script does it |
| Negation, Phantom of Chaos in both orders, Brain Control after use, flips and two Dark Panthers follow the rulings | NOTE | Recorded |
| 089E stops at the second offer and does not resolve it | NOTE | Left as is; the offer is what the ruling is about |

Sol's follow-up on `df801be` ([review-sol-2.md](../tests/edison/reports/dark-panther-2026-11-10/review-sol-2.md)) found both fixes hold: its
own fixture now copies nothing after the steal, its copy-only mutant fails 089F, and a mutant without the controller check fails 089G. It found
no way for 089F or 089G to pass for the wrong reason, and its negation, Phantom of Chaos and Brain Control fixtures give the same results as
before. Its verdict: the override is safe to merge. No new findings.

Neither review covered a target that changes control before the copy resolves, for example Dark Panther's player chaining Enemy Controller to
take Bazoo. The override, like the stock script, still copies it then. That was not checked against a ruling.

## Reproduce

```bash
export PATH="$HOME/.local/bin:$PATH" OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
export http_proxy=http://127.0.0.1:9 https_proxy=http://127.0.0.1:9   # xmake fails silently while it tries the network
W=$PWD
xmake f -P "$W" -o "$W/build" -m release -y          # installs ygopro-core r3 from the cached clone
xmake b -r -P "$W" -j 8 ygopro_ygoenv
xmake b -r -P "$W" -j 8 edison_rules
(cd tests/edison/conformance && xmake f -P . -o ../../../build/edison-conformance -m release -y && xmake b -P . -j 6)
python tests/edison/run.py
python tests/edison/conformance/run_conformance.py --output build/recert/conformance
python tools/edison/hardening/compare_traces.py --certification OLD_CERT build/edison-certification \
  --conformance OLD_CONFORMANCE build/recert/conformance --output build/recert/compare-traces.json
python tools/edison/hardening/soak.py --stage a2 --per-pair 8 --seed 80700000 --output build/recert/soak-new/lists [--so OLD_MODULE]
python tests/edison/prompt_audit.py compare-soak build/recert/soak-old build/recert/soak-new
python tests/edison/prompt_audit.py compare-fields OLD_MODULE NEW_MODULE --seed 80730000 --games 16
bash tools/edison/forward_model/test_build_refusal.sh
python tests/edison/reports/dark-panther-2026-11-10/mutants.py      # test 089 on the eight mutants, built by hand
```

The other soak stages are `sidein --per-pair 8 --seed 80701000`, `targets --targets tests/edison/reports/deck-labels-2026-11-04/crash-targets.json
--per-target 12 --seed 80702000`, `uniform --count 1000 --seed 80710000`, and `targets --targets
tests/edison/reports/dark-panther-2026-11-10/soak/panther-targets.json --per-target 100 --seed 80720000`. The crash-pair runs use
`tools/edison/corpus/soak_worker.py` in a run tree per module, as the investigation's `replay.sh` does.
