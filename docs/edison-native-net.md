# Native forward pass for the Edison network

A C++ port of `tools/edison/learner/net.py` and of `row_lookup`, `pack`, `normalize_history_turn`
and `audit` in `obs.py`, for use inside search workers, where JAX cannot run. It matches JAX within
float32 rounding on 58,804 real decisions, and pack plus value takes about 87 µs at the median and
255 µs at p99. Decisions over about 72 rows go past the 0.3 ms target at p99.

Nothing here has run inside a real duel in the arena yet. See [What was not verified](#what-was-not-verified).

## What exists

Everything is in `tools/edison/search/`.

| File | Purpose |
| --- | --- |
| `edison_net.h` | The port. Raw observation arrays in, value and per-option logits out |
| `export_net.py` | Checkpoint to the flat `.ednet` file the header loads |
| `collect_decisions.py` | Plays full games and records every raw observation |
| `reference.py` | Runs the actual Python functions on those observations |
| `mutate_decisions.py` | Audit mutations, and shapes real play did not reach |
| `net_diff.cpp` | Compares native output with the Python output |
| `run_differential.py` | Runs the differential, controls and loader checks |
| `noise_floor.py` | Measures native and JAX float32 error against a float64 run |
| `net_arena_test.cpp` | Tests the arena contract against the real `arena.h` |
| `net_load_test.cpp` | Loader probe for corrupted files |
| `net_bench.cpp`, `run_bench.sh` | Latency |
| `evidence/` | The measured results quoted below |

### Using it

The usage and the arena contract are at the top of `edison_net.h`. In short:

- `load` and `create_scratch` may run with the arena active or inactive, and before or after `arena_map`.
- Nothing calls malloc, new or stdio, in loading or per call. Weights and scratch come from direct `mmap`.
- Weights are read-only after load. Load once before `fork` to share the pages.
- Each process or thread needs its own `Scratch`. A `Net` can be shared.
- `load` needs the expected `params_sha256`, `codes_sha256` and config. Any mismatch refuses the file.
- The audit is a per-call switch. With it off, row range and embedding range checks still run, so a bad
  observation returns an error. Python returns NaN there.

## Measured numbers

### Data

- 336 full games in 168 cells, with no void cells, and 58,804 decisions.
- All 21 lists in both seats. The smallest count for a list in one seat is 894 decisions.
- Certified native module, sha256 `b8fcd2bcf7774ae4edf8a4a800c2c55c006f62cb5cc1ef925fe316365a1ddf6f`.
- Seeds 85000000-85000335.

| Shape in real play | p50 | p90 | p99 | p99.9 | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| Kept rows | 36 | 61 | 84 | 95 | 98 |
| Options | 3 | 6 | 12 | 19 | 36 |

47,581 decisions have all 16 history rows filled, and 48,062 are at or past a seat's 17th decision in its
game, where the ring has probably wrapped. 6,271 have an active selection context.

### Packed bytes and audit

| Check | Result |
| --- | --- |
| Normalized history, packed cards, actions, decision, global, on 58,804 decisions | 0 mismatches |
| Audit verdicts on real observations | 0 mismatches, all pass |
| Audit verdicts on 22,120 mutated observations, exact rule and column | 0 mismatches |
| Mutations confirmed to hit their rule in Python | 66 mutation kinds, 20 samples each |
| Packed bytes on mutated observations that Python accepts | 0 mismatches |
| Value-only against full value, bit for bit | 0 mismatches |

The mutated set also holds 20,000 random single-field changes. Three listed bounds cannot be broken on a
uint8 column, so they have no mutation: action column 6, history column 6 and history column 12.

### Value and logits, float32

| Checkpoint | Value max | Value p99.9 | Logit max | Logit p99.9 | Greedy mismatches |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1702 | 4.53e-6 | 1.73e-6 | 1.16e-3 | 1.07e-4 | 0 |
| 1703 | 8.12e-6 | 3.10e-6 | 1.46e-3 | 1.18e-4 | 2 |
| 1704 | 5.28e-6 | 2.06e-6 | 2.75e-4 | 6.10e-5 | 0 |

**Logit max is above the expected 1e-3 on two checkpoints, and it is rounding.** Logits reach 2,135, where one
float32 ulp is about 2.4e-4. On each checkpoint's 200 worst decisions, against a float64 run of `EdisonNet`:

| Checkpoint | JAX float32 error, max | Native float32 error, max | Largest logit |
| --- | ---: | ---: | ---: |
| 1702 | 9.96e-4 | 3.59e-4 | 369 |
| 1703 | 1.37e-3 | 1.31e-3 | 2,135 |
| 1704 | 2.56e-4 | 3.38e-4 | 272 |

**Native rounding is a little worse than JAX's on a typical decision.** On 2,000 random decisions its mean
logit error against float64 is 1.0e-5 to 1.9e-5, against 7.6e-6 to 1.1e-5 for JAX, and it is at least as close
on only 26-33% of them. Value error is the same for both, about 2e-7. I did not try to reduce this.

**Both greedy mismatches are near-ties.** Both are on checkpoint 1703:

| Record | Python's lead over native's pick | Native's lead over Python's pick | Largest logit error there |
| --- | ---: | ---: | ---: |
| 38534 | 1.5e-5 | 4.6e-5 | 4.6e-5 |
| 42178 | 9.2e-5 | 1.5e-5 | 6.1e-5 |

### Other shapes

| Run | Records | Result |
| --- | ---: | --- |
| Limits not seen in play: 160 rows, 256 options, both, no rows, plus unfilled history and rare row rules, per checkpoint | 700 | All gates pass. Value max 4.6e-6, logit max 5.8e-4 |
| Random net: 96 channels, 3 layers, head size 24 | 3,000 | Value max 5.5e-7, logit max 7.7e-7 |
| AVX2 with FMA, 1702 | 8,000 | All gates pass. Value max 4.6e-6, logit max 3.7e-4 |
| Baseline x86-64, no FMA, 1702 | 8,000 | All gates pass. Value max 5.5e-6, logit max 3.7e-4 |

### Negative controls

The gates are in `run_differential.py`. I set them from the correct build's measured rounding, after
measuring it, at about twice its worst case. They are not predeclared. Checkpoint 1702:

| Fault | Caught by | Value max | Logit max |
| --- | --- | ---: | ---: |
| **Wrong GELU: exact erf** | Forward gates | 6.45e-3 | 0.161 |
| **Wrong LayerNorm epsilon: 1e-5** | **Missed on real weights.** Caught on a shrunk net, below | 4.53e-6 | 1.16e-3 |
| **Shuffled rows, references remapped** | **Packed bytes only**, on all 58,804 decisions | 4.05e-6 | 1.14e-3 |
| Shuffled rows, references not remapped | Packed bytes and forward gates | 1.65 | 232 |
| **Off-by-one action row in pack** | Packed bytes and forward gates | 4.53e-6 | 133 |
| Off-by-one row in forward only | Forward gates | 4.53e-6 | 133 |
| Pointer 0 reads the token, not `no_row` | Forward gates | 0.692 | 238 |
| Thermometer unknown bit dropped | Forward gates | 0.837 | 15.2 |
| Hint index misses its +1 | Forward gates | 0.933 | 90.0 |
| Action pool sum, not mean | Forward gates | 4.53e-6 | 536 |
| Token included in the state pool | Forward gates | 0.139 | 16.9 |
| Is-source flag not set | Packed bytes and forward gates | 0.364 | 20.7 |
| History normalization skipped | Mutated set only | 4.53e-6 | 1.16e-3 |
| Selected rows not kept | Mutated set only | 4.53e-6 | 1.16e-3 |
| Audit skips finish and cancel flags | Mutated set only | 4.53e-6 | 1.16e-3 |

**Perturbed weights.** Each row changes one element:

| Weight | Change | Result |
| --- | ---: | --- |
| `value_out/kernel[0,0]` | +1e-3 | Caught, value max 1.66e-4 |
| `Block_1/Attention_0/Dense_0/kernel[3,5]` | +1e-3 | Caught, value max 2.06e-5 |
| `Encoder_0/Dense_1/kernel[40,7]` | +1e-3 | Caught, value max 1.63e-4 |
| `Encoder_0/LayerNorm_0/scale[12]` | +1e-3 | Caught, value max 3.17e-3 |
| `Encoder_0/no_row[5]` | +1e-3 | Caught, value max 1.52e-3 |
| `logit_hidden/kernel[10,10]` | +1e-3 | **Missed** |
| `logit_hidden/kernel[10,10]` | +1e-2 | **Missed**, logit max 2.03e-3 against a gate of 2.5e-3 |
| `logit_hidden/kernel[10,10]` | +1e-1 | Caught, logit max 2.01e-2 |

Noise of 2e-3 on every element of one array, on 4,000 decisions, was caught for 89 of 90 arrays. The miss was
`const/bin_widths`, whose values are in the hundreds, and noise of 1.0 caught it. An earlier version of this
sweep used a uniform shift and missed 10 arrays, because a LayerNorm's mean subtraction removes a uniform shift.

**What the controls show.**

- **Rows are permutation-equivariant,** so value and logits cannot see a consistently shuffled row order. Only the byte comparison can.
- **LayerNorm epsilon is unobservable on these weights.** Sampling 1,000 decisions, the smallest input variance
  at the 3 LayerNorm sites I could capture (of 5) is 116, so 1e-5 against 1e-6 is far below rounding. To test it,
  `small_activation` reruns JAX and native on checkpoint 1702 with its row and token outputs shrunk 1000 times.
  There the correct build has value max 3.18e-5 and the wrong epsilon 0.602. This net has its own gates.
- **Real play does not exercise everything.** Three faults are caught only by constructed observations.

### Loader

All 18 cases behaved as expected: the good file loads, and these 17 are refused: truncated, extended, payload byte
flip, header byte flip, bad magic, bad version, wrong or empty expected `params_sha256`, wrong expected `codes_sha256`,
wrong expected config, empty file, missing file, and renamed, reshaped, missing, extra or duplicate array.

### Arena

`net_arena_test` passes all 12 checks, including two controls that show its probes can see an allocation and a write.

- Loading and evaluating with the arena active leave its bytes, top and live block count unchanged.
- With the arena write-protected under `arena.h`'s tracking, evaluation dirties no page.
- After scrubbing and restoring the arena, weights are unchanged and outputs are bit-identical.
- The next allocation address after a restore and evaluation is unchanged.
- A write to the weights faults.

### Latency

Single thread, batch 1, pinned to one CPU, AVX-512, on a Ryzen 7 9800X3D under WSL2. 20,000 real decisions, 3 passes,
3 runs. Mean 39 rows and 3.6 options. Load average was 2.7-3.8 with other work on the machine, so treat these as approximate.

| Step, µs | p50 | p90 | p99 |
| --- | ---: | ---: | ---: |
| Audit | 2.9 | 4.1 | 5.2 |
| Normalize and pack | 0.6 | 0.8 | 1.0 |
| Value forward | 86 | 158 | 254 |
| **Normalize, pack and value** | **87** | **158** | **255** |
| Full, including normalize and pack | 88 | 161 | 258 |
| JAX inline value, for comparison | 186 | 301 | 451 |
| JAX inline full, for comparison | 210 | 329 | 474 |

The JAX rows use 3,000 decisions, exclude Python's audit and pack, and were measured once. Run 3 is shown here by row count:

| Kept rows | Decisions per pass | Pack + value p50 | p90 | p99 | Full p50 | p90 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Up to 16 | 572 | 29 | 38 | 61 | 30 | 39 | 63 |
| 17-24 | 2,152 | 49 | 59 | 85 | 51 | 62 | 93 |
| 25-32 | 4,862 | 64 | 78 | 113 | 65 | 79 | 121 |
| 33-48 | 7,782 | 92 | 113 | 176 | 93 | 115 | 168 |
| 49-72 | 3,953 | 142 | 182 | 281 | 143 | 184 | 278 |
| 73-112 | 679 | 218 | 262 | 389 | 220 | 267 | 406 |
| 160, constructed | 200 | 566 | 652 | 863 | 627 | 706 | 943 |

**Against the 0.3 ms target:** met through p99 overall, and at p99 up to 72 rows. Decisions over 72 rows are about
3% of the real mix, and their p99 is about 390 µs. The largest row count in real play was 98.

Full is measured just after value on the same decision, with a warm cache, so its small lead over value is not a
reliable cost for the action head. Occasional single calls reached about 15 ms, which I read as preemption but did not confirm.

## Findings outside this code

- **`normalize_history_turn` is a no-op in real play on this core.** It changed no bytes in 58,804 decisions.
  `ygopro.h` writes the newest row first and empty rows last, and rewrites column 12 as turns-ago itself. So a row
  is only present under an empty first row in constructed data, and the `obs.py` docstring saying unfilled rows
  come first does not describe this core. The port still matches on constructed cases.
- **Python's audit accepts a negative decision source,** and `pack` then wraps it around the end of its index
  table. Native audit returns the same verdict, to keep parity, but native `pack` refuses it. 1 of the mutated
  observations reached this. I did not change `obs.py`.

## What was not verified

- **No real duel ran in the arena.** The arena test uses a synthetic heap and real observations. The forward model
  directory is being moved by other work, so joining them is the next step.
- **No multi-worker timing.** Every number is one thread. Scaling across 8 workers, with shared caches, was not measured.
- **Shapes above 98 rows or 36 options** come only from constructed observations, not real play.
- **Lower instruction sets** ran on 8,000 decisions and one checkpoint, with no timings.
- **Compilers.** GCC 11 only. clang is not installed here, and the header's vector conditionals are untested on it.
- **Batched JAX.** The reference is batch 1 through `pad_batch`. Actors usually run larger batches, which I did not compare.
- **The source hash.** The file's own hashes catch corruption, and the loader checks the claimed `params_sha256`
  against what the caller expects. It cannot recompute that hash without the msgpack, so it trusts `export_net.py`.
- **Small single-weight changes.** One element of `logit_hidden` changed by up to 1e-2 stays within the gates.

## Reproduce

Link the git-ignored assets as in the Reproduce section of `docs/edison-forward-model.md`, then run the
commands below, about 30 minutes in all at up to 4 threads. Output goes to `build/edison-native-net/`.

~~~bash
W=/path/to/worktree
PY=/home/mada/ygo-agent/.venv/bin/python
mkdir -p "$W/build/edison-native-net/data"
bash "$W/tools/edison/search/build.sh" faults
for k in 0 1 2 3; do
  JAX_PLATFORMS=cpu OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 TF_CPP_MIN_LOG_LEVEL=2 \
    XLA_FLAGS="--xla_cpu_multi_thread_eigen=false intra_op_parallelism_threads=1" \
    "$PY" "$W/tools/edison/search/collect_decisions.py" --games 2 --seed 85000000 --shard $k --shards 4 \
    --output "$W/build/edison-native-net/data/decisions-$k.bin" &
done
wait
"$PY" "$W/tools/edison/search/run_differential.py"   # writes summary.json
bash "$W/tools/edison/search/run_bench.sh" 5         # pins to CPU 5
~~~

`net_arena_test` takes the net file, both hashes and a decisions file. A smoke run also used seeds
85090000-85090011, and its games are in no result here.

## Independent review

[Sol's review](../tools/edison/search/evidence/review/reviewer_report.md) of 96b2ca9 wrote its own differential on 42 new games (seeds 85200000-85200041, lists paired differently, through the production JAX path in batches of 16). Over 8,487 decisions on all 3 checkpoints it found packed arrays identical, value error at most 4.2e-6, logit error at most 7.3e-4 and 0 greedy differences. Sanitizers and arena probes were clean. It still did not run a real duel in the arena. Its findings, all now fixed:

- **The differential command exited 0 on failed gates.** It now exits 1. I confirmed this by tightening one gate.
- **The near-tie check could not fail,** because a gap never exceeds twice the error from the same two vectors. It now uses a fixed 5e-4 gap. That gap is set after the fact, from the two real mismatches.
- **Zero options returned a value,** and **an extreme invalid hint overflowed a signed int** with the audit off. Both now refuse. Neither occurs in real observations.
- **Unaudited value evaluation ignores action fields.** This is by design, and the header comment now says so instead of promising refusal.
