# Independent review of 96b2ca9

No REACHABLE numerical, packing, arena-state or additional-information defect was found in the tested production observations. I found two TEST defects and two PLANTED-ONLY exceptions to the unaudited input contract. This is evidence for the port on the tested inputs, not a proof for every possible game or configuration.

## Findings

1. **TEST — Failed differential gates do not fail the test command. High confidence.**

   `tools/edison/search/run_differential.py:456`, `:461`, `:464` and `:474` collect or serialize failures, but `main()` reaches `:478` and returns normally. In particular, mutation failures are not even passed through `gate_failures` for the final result. Subprocess launch/parse failures can fail the command; an ordinary numerical or byte mismatch cannot. Loader failures and missed negative controls likewise remain report data.

   I exercised the actual `main()` with only its expensive data/forward producers replaced by deterministic stubs, using `build/reviewer/audit_probe.py`. Every checkpoint reported **123 packed-card mismatches**, value error **1.0**, logit error **1.0**, and failure of every numerical gate. It wrote those failures and returned normally; the Python command exited **0**. See `build/reviewer/audit.log` and `build/reviewer/suite_exit_proof/summary.json`. This reproduction tests orchestration, not a real failing forward pass. A CI job that relies on the documented command's exit status will accept a broken port.

2. **TEST — The near-tie check cannot distinguish rounding from an arbitrarily wrong policy. High confidence.**

   `tools/edison/search/run_differential.py:181` tests whether `python_gap > 2 * worst_logit_error`, with both quantities derived from the same two output vectors by `tools/edison/search/net_diff.cpp:250`.

   Let `p` be Python's maximum, `q` native's maximum, and `E = max_i |python_i - native_i|`. Since `native_q >= native_p`, necessarily `python_p - python_q <= 2E`. The check therefore follows from choosing maxima, even when the implementation is completely wrong. For example, Python logits **[100, -100]** and native logits **[-100, 100]** have Python gap **200** and worst error **200**. Calling the real `near_tie_check` with these scores returns **not_near_ties = 0**. Reproduced in `build/reviewer/extras.py`; result in `extras.json`.

   This does not invalidate the two small gaps reported in the author's corpus. It means this check adds no independent protection and cannot justify treating future mismatches as rounding. `greedy_mismatch_count` is also absent from `COUNT_GATES` (`run_differential.py:43`). Numerical gates remain the effective protection.

3. **PLANTED-ONLY — Unaudited evaluation can return a value for out-of-range action fields, and full evaluation accepts zero options. High confidence.**

   `tools/edison/search/edison_net.h:1275` never calls the action embedding checks in `logit_head`. `pack`, at `:849`, rejects negative counts and counts above 256 but accepts zero. These are exceptions to the broad promise at `:21` that unaudited bad observations return an error.

   Concrete reproduction: take my real **game 85200000, decision 0**, checkpoint **1702**, with **10** options. Set the first offered action's ID bytes to **[255,255]** (ID 65535, above 509). `evaluate_value(..., false, ...)` returns **status 0, value -0.12491633743047714**. Audited value evaluation returns **8**; unaudited full evaluation returns **503**. Setting its message embedding index to **255** similarly succeeds on unaudited value evaluation, but returns **203** with audit and **503** on unaudited full evaluation. Setting `num_options = legal_options = 0` returns **status 0** and the same finite value from **both** unaudited entry points. An invalid sequence value in a dropped padding row is also ignored by both unaudited entry points, while audit rejects it.

   See `build/reviewer/fail_closed_details.py` and `.json`. The certified producer did not emit any of these malformed inputs, so I classify this as PLANTED-ONLY, not a demonstrated game failure. The value path's independence from actions is intentional; the unconditional input-refusal claim is stronger than the implementation. With audit enabled, these cases refuse correctly.

4. **PLANTED-ONLY — An extreme invalid hint causes signed overflow before embedding-range refusal. High confidence.**

   `tools/edison/search/edison_net.h:1158` computes the signed expression `d[1] - kHintMin + 1` before `put` checks the resulting index. With audit disabled and `decision[1] = INT32_MIN`, this is undefined C++ arithmetic rather than a defined refusal path.

   I compiled `build/reviewer/bad_hint.cpp` with GCC ASan/UBSan and ran it against checkpoint 1702. With `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, it exits **1** at that line: **signed integer overflow: -2147483648 - 499 cannot be represented in type 'int'**. See `build/reviewer/bad_hint.log`. The ordinary optimized build happened to return **503** for this input; that does not remove the undefined behavior. Audit rejects the input first. Real hints from the certified encoder cannot have this value.

## 1. Independent functional comparison

I wrote `build/reviewer/review.py` and `bridge.cpp`. I did not use `collect_decisions.py`, `reference.py`, `dump_format.python_pipeline`, `net_diff`, or the author's reference outputs for this comparison. I used the actual production `obs.audit`, `normalize_history_turn`, `pack`, and `actor.NetPolicy.infer`; the latter invokes JAX through the production padding and batching path. Checkpoint export used `actor.load_checkpoint`, the arrays from the source msgpack, `net.py`'s bin constants, and the port's file serializer. Thus this is an independent differential harness, not an independent checkpoint-file encoder.

I ran **42 completed new games**, using **only game seeds 85200000–85200041**. All 21 lists appeared in both seats, paired seven positions apart in the sorted registry rather than the author's neighboring-list schedule. Each pairing had one heuristic/heuristic game and one heuristic/random game. The certified imported module's SHA-256 was **b8fcd2bcf7774ae4edf8a4a800c2c55c006f62cb5cc1ef925fe316365a1ddf6f**. There were **8,487 decisions**, **399** active selection contexts, **356** positive sources, **50** decisions with already-selected cards, **4** with mandatory cards, **6,880** full histories, and **46** positions above 72 kept rows. Maximum kept rows/options were **77/20**; unknown-ATK rows occurred **1,950** times.

For every decision and checkpoint, I compared native output to production JAX in batches of 16, including mixed row lengths and action counts. Packed card/action/history/decision/global arrays and row/option counts matched exactly.

| Checkpoint | Maximum value difference | Maximum logit difference | Greedy differences |
| --- | ---: | ---: | ---: |
| 1702 | 1.944602e-6 | 3.967285e-4 | 0 |
| 1703 | 4.205853e-6 | 7.324219e-4 | 0 |
| 1704 | 2.175570e-6 | 1.220703e-4 | 0 |

Results: `build/reviewer/comparison.json`; collection metadata and log: `games.json`, `collect.log`. An initial harness run initialized Lua before changing to the scripts directory and emitted native diagnostics. I discarded that data, corrected initialization, and reran the same seeds. The retained run has no native diagnostics.

To cover sparsely exercised branches and actual actor batching, I independently made **258 additional cases** in `extras.py`: real high-row/high-option/source/selected/mandatory positions plus constructed boundaries at **0, 1, 47, 48, 49, 71, 72, 73, 111, 112, 113, 159, 160** kept rows and **1, 16, 17, 64, 65, 255, 256** options. These include source/reference 160, an empty first history row with later populated rows and clipping on both sides, and selection flags retaining hidden private rows. Batch lengths **1, 3, 4, 16, 17, 64** exercise padding into the 1/4/16/64 actor buckets. Every case passes Python audit and normalization.

All three checkpoints matched packing in these cases. Maximum value/logit differences respectively were **1.280569e-6 / 1.296997e-4**, **3.784895e-6 / 4.768372e-5**, and **2.190471e-6 / 6.103516e-5**. See `extras.json`. These constructed observations are explicitly not evidence that real games reach 160 rows or 256 options.

The author's controls and reports are useful evidence for many wiring errors, but do not establish all perturbations are detectable. The post-calibrated logit maximum gate is **2.5e-3** (`run_differential.py:40`); the author's acknowledged **+1e-2** change to one `logit_hidden` weight produces about **2.03e-3** maximum error and passes. I read that evidence rather than rerunning the weight sweep or float64 noise-floor experiment. I found no new REACHABLE functional discrepancy; I would not describe the tests as a proof of exact equality or comprehensive policy-equivalence protection, particularly given findings 1–2.

## 2. Arena safety and state retention

For all **8,487 decisions × 3 checkpoints**, I compared evaluation after scratch mappings filled with **0x00**, **0x55**, **0xA5**, and/or **0xFF**, mixed with ordinary reused scratch. I then evaluated the full corpus in reverse order and compared each result to its earlier result. There were **zero** bit differences in values or logits. Value-only and full values were also bit-identical throughout.

I compiled my independent wire-format runner with **GCC, -O1 -g -march=native -fsanitize=address,undefined -fno-omit-frame-pointer**. All **8,487 real + 258 additional positions**, on each of the three checkpoints, completed with **zero refusals and no sanitizer diagnostics**. The deliberately invalid hint is the separate UBSan failure in finding 4.

My `arena_probe.cpp` uses the real `forward_model/arena.h`, loads weights and scratch with allocation interception active, puts raw observation storage inside the arena, and evaluates all **258 additional positions** using checkpoint 1702. Under write tracking it dirtied **zero arena pages**; allocator top/live counts remained unchanged. Scrubbing the arena, restoring the snapshot, and evaluating with differently poisoned scratch produced identical values/logits. **258 positions, zero failures.** See `safety.log` and `run_safety.py`.

Source inspection found direct anonymous mmap for weights/scratch, no allocator or stdio call in the inference path, no retained raw-observation pointers, and complete writes of the intermediates subsequently consumed on these paths. I found no serious arena defect. I did **not** run an actual duel and native inference together inside an arena worker. ASan does not place redzones between the manually partitioned scratch buffers, and poisoning is not a complete uninitialized-read proof; no MemorySanitizer/Valgrind run was available.

## 3. Audit and refusal behavior

I independently mutated the new observations **25,000** times, including boundary byte values, negative/extreme decision integers, source/action row references, counts, legal counts and overflow. **6,477** passed audit; **18,523** were refused. Native/Python verdicts matched in **all 25,000 cases, including exact rule and column**, with Python indexing errors mapped to native row/source errors. See `audit_probe.py`, `audit_rules.json`, and `audit.log`. This checks raw audit; history normalization parity was checked separately in the functional/boundary runs.

An offered action reference **161** returns native **500** without audit and **9** with audit. Counts **257** return **502** without audit and **1** with audit. Negative counts also refuse. Native output packing can retain at most 160 rows; `RawObs` supplies fixed-size buffers, not a caller-supplied row count or buffer lengths. The API cannot validate the actual extent of an incorrectly supplied pointer. I found no out-of-bounds access on supported buffers.

I reproduced the author's negative-source observation on my active **record 36**: setting `decision[3] = -1` passes both raw audits (**native 0**), while native evaluation returns **501**. This is a deliberate stricter native packing result, not a reachable parity failure. The producer's `SpecInfo.index` is **uint16_t** (`ygoenv/ygoenv/ygopro/ygopro.h:1247`); `_set_obs_cards` starts at zero, capacity-checks, and assigns **++offset** (`:2991`–`:2996`). `_set_obs_decision` copies that index only for an existing visible source (`:2647`), otherwise leaving zero. There is no negative sentinel on this path. No negative source occurred in the new games. I found no normal-play path to a negative source in the certified encoder; it requires an artificial observation or an independent corruption defect.

Findings 3–4 are the material exceptions when audit is off. With audit on, the tested malformed inputs refuse correctly.

## 4. Information available to the policy

I found no added information channel relative to the original Python network. `RawObs` contains the same six whitelisted observation arrays and option-contract scalars. Native evaluation does not query duel state, opponent deck, `mask_`, `info:deck`, or allocator contents. Source and selection flags, history pooling, row retention, and pointer remapping match Python on the tested inputs. Retaining a selected hidden row is also what the original Python packer does.

This conclusion assumes the certified observation producer and its visibility rules. It does not certify a future determinizer or canonicalization of search prompts. Turning audit off also removes privacy-contract checks already explicit in `obs.audit`; I found no demonstrated additional disclosure from valid observations.

## Execution scope and reproduction

All builds and outputs were confined to this worktree. No xmake invocation or rebuild under the main `/home/mada/ygo-agent` checkout occurred. Ignored scripts/database/native-module assets were linked/copied as instructed. Main checkpoints and the author's raw-data directory were read only. The numerical comparison did not consume the author's corpus. No production code was changed.

Runs were pinned to CPUs **0–2**, with BLAS/OMP threads set to **1**. Test scripts/binaries, logs and generated data are retained under `build/reviewer/` (git-ignored). To repeat from this worktree after compiling `bridge.cpp` as `bridge.so`:

```bash
PY=/home/mada/ygo-agent/.venv/bin/python
export PYTHONDONTWRITEBYTECODE=1 OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 JAX_PLATFORMS=cpu
# Reuses only the stated game seeds; collection must initialize from scripts/ as implemented.
taskset -c 0-2 "$PY" build/reviewer/review.py collect
taskset -c 0-2 "$PY" build/reviewer/review.py check
taskset -c 0-2 "$PY" build/reviewer/extras.py
taskset -c 0-2 "$PY" build/reviewer/run_safety.py
taskset -c 0-2 "$PY" build/reviewer/audit_probe.py
taskset -c 0-2 "$PY" build/reviewer/run_bad_hint.py
taskset -c 0-2 "$PY" build/reviewer/fail_closed_details.py
```

The first bridge build was `g++ -std=c++17 -O3 -march=native -fPIC -shared -Itools/edison/search build/reviewer/bridge.cpp -o build/reviewer/bridge.so`. The sanitizer runner was built from the same file with `-DREVIEW_MAIN` and the sanitizer flags above, output `build/reviewer/sanitize`. The safety driver builds only the independent arena probe; it expects that sanitizer runner to exist. `audit_probe.py` intentionally injects failing scores for the exit-status reproduction; its synthetic score report is not a measurement of the real network.

