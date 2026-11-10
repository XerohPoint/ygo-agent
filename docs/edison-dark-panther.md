# Edison Dark Panther crash and repeat fix

Prepared November 10, 2026, on branch `panther-fix` from `edison` at `3dd4daf`. Not pushed. Review status is under [Reviews](#reviews).

**Result: the corpus soak's crash was a heap use-after-free that our own core patch caused, and it is fixed in `edison-hardening` r3. Neo-Spacian
Dark Panther could use its once-per-turn effect again and again by copying the opponent's Dark Panther, and an Edison override now stops that.
The production module is now `75d1967b...`, replacing `b8fcd2bc...`. Certification passes 603 of 603. On the same seeds, the fixed and old
modules play all 3,376 soak duels identically. No result to date changes: no card in code list v1 copies effects, so no M1, M2 or Stage A game
could reach either bug.**

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

## The change

| File | Change |
| --- | --- |
| `repo/packages/y/ygopro-core/edison-hardening.patch` | r3. `RemoveCountLimit` also erases the effect from `rechargeable`. `card::remove_effect` and `field::remove_effect` erase it whatever the flag says. The r2 hunks are byte for byte unchanged |
| `xmake.lua`, `tests/edison/conformance/xmake.lua` | `edison_hardening` r2 to r3 |
| `ygoenv/ygoenv/ygopro/historical_scripts.h` | A Dark Panther override: the stock script plus a cost that requires and sets card flag 43237273, reset by `RESETS_STANDARD` and the End Phase, as the stock Phantom of Chaos script guards its own copy. A copy of the effect checks the same card's flag |
| `tests/edison/panther_copy_tests.h`, `rules.cpp`, `harness.h` | Test 089. The harness pins Dark Panther's script and gains `check_rechargeable()`, which requires every member of the set to be a live effect with a count limit, checking membership before reading any flag |
| `tests/edison/script_consistency_tests.h`, `assets.lock.json` | 89 pinned scripts and 28 overrides; the r3 patch hash |
| `tools/edison/forward_model/build.sh`, `test_build_refusal.sh` | The search build now requires r3 and its library hash, and the refusal test also refuses r2 |

The r3 core sources match, byte for byte apart from comments, the tree the investigation tested under AddressSanitizer. The override matches its
tested script apart from comments.

**Test 089**, each case with a Rule 5 control on the stock scripts:

| Case | Edison | Control |
| --- | --- | --- |
| 089A, 089B: Dark Panther copies Treeborn Frog or D-END, then two turn starts | The copied Standby effect has no limit and is not in the set. After its deletion, every member is live | The copy keeps its default limit and is in the set |
| 089C: Dark Panther copies the opponent's Dark Panther | Not offered again that turn. The opponent's Dark Panther is offered on its own turn | The stock script offers the copy, and it resolves a second time |
| 089D: two Dark Panthers on one side | Each activates once | The same |
| 089E: used, flipped face-down by Book of Moon, Flip Summoned | Offered again | The same |

Two mutants show the cases can fail. Linked against the r2 core, Edison 089A and 089B fail at the first prompt. Without the override, Edison 089C
fails. Every other case passes on both. See [mutants.txt](../tests/edison/reports/dark-panther-2026-11-10/mutants.txt).

## Gates

On module `75d1967b...`, reproduced by a forced rebuild.

| Gate | Result |
| --- | --- |
| Certification | **603 PASS, 0 FAIL**, two identical replay traces, `edison_rules` `1ae071be...`, 111 sources |
| Certification traces against the certified build | All 593 earlier cases identical except 075A-C, whose counts are now 89 scripts and 28 overrides, and 081C, whose text included the run's closing summary line, now followed by 089 |
| Conformance | As recorded. All 114 test files and `results.json` byte-identical to the certified build's. `edison_conformance` is now `23bb2a2c...` |
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

- **Results to date stand.** Of the 26 scripts that copy effects, none is a card in code list v1, so M1, M2 and Stage A could not reach either bug.
  The soak's 21 training lists, and the observation fields, are identical on both modules.
- **The corpus soak's crash is explained.** All six recorded crash duels show the same freed read. Its classification in
  [edison-corpus.md](edison-corpus.md) was right to call Dark Panther the lead and not the cause.
- **Stage B can play the 7 Dark Panther lists,** on this module.
- **The M2 and M3 search binary** `167be8aa...` was built on r2 and stays the record for those runs. `build.sh` now refuses r2, so a new search
  binary on r3 is needed before Stage B uses search. It was not built here.
- **Rules not covered by a fixture:** Phantom of Chaos copying Treeborn Frog or D-END, Phantom of Chaos copying Dark Panther, and a Dark Panther
  that changes control after using its effect. The copier soak plays Phantom of Chaos 100 times without a crash, which is not a rules check.

## Reviews

An independent reviewer and Sol (`gpt-6.1-sol`) review this commit. Sol's first attempt hit its usage limit before it started.

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
```

The other soak stages are `sidein --per-pair 8 --seed 80701000`, `targets --targets tests/edison/reports/deck-labels-2026-11-04/crash-targets.json
--per-target 12 --seed 80702000`, `uniform --count 1000 --seed 80710000`, and `targets --targets
tests/edison/reports/dark-panther-2026-11-10/soak/panther-targets.json --per-target 100 --seed 80720000`. The crash-pair runs use
`tools/edison/corpus/soak_worker.py` in a run tree per module, as the investigation's `replay.sh` does.
