# Edison trigger order

Prepared November 4, 2026, on `edison` at `ce823ec`, in an isolated worktree. Main was then rebuilt and certified with the same hashes.

**Result: SEGOC steps now come first under Rule 1, and chronology applies only inside one step (D1). Conformance 241 and
242 pass. Fixture 235A now asserts the owner's D4 ruling, with no core change.** Certification is 567 PASS, 0 FAIL with identical
replays, and every Rule 5 trace is byte-identical. Evidence is in
[reports/trigger-order-2026-11-04](../tests/edison/reports/trigger-order-2026-11-04/).

## Owner rulings

- **D1.** P0 Soul Exchanges P1's Sangan and Tributes it for Caius: Caius is Chain Link 1, Sangan is Chain Link 2.
- **D4.** After a Summon whose optional trigger is declined (the Swap Frog case), Ignition Effects are still offered with
  priority. The simulator already did this. This overrides the Armageddon Knight example in Rule 6, and the Swap Frog and Alien
  Dog rulings agree with it.
- **Dark World Dealings** (conformance 242). With earlier optional Watapon and later mandatory Broww, Broww is Chain Link 1. The owner
  confirmed this after the change landed, and it matches edisonformat.com's Rule 7. Groups 005, 006, 017, 018 and 033 rest on it.

## The rule as implemented

| SEGOC step | Who | Trigger type |
| ---: | --- | --- |
| 1 | Turn player | Mandatory |
| 2 | Non-turn player | Mandatory |
| 3 | Turn player | Optional |
| 4 | Non-turn player | Optional |

Inside one step, the player is offered only their earliest pending batch. So earlier events go on the Chain first, and the
player orders only triggers from one simultaneous batch. Declining ends that batch for good, and the player's later batches in
that step are still offered.

The change is in `field::process_point_event`. A `keep_earliest_batch` filter runs after each step's validity pass and the
`earliest_group`, `has_group`, `early_trigger_group` and step 7 restart are gone. Validity is now checked for every pending trigger
on each pass, as upstream does. Rule 5 takes the upstream path: the filter and the decline branch are gated on `duel_rule == 1`.
Hand triggers keep their `check_nonpublic_trigger` handling. 243E and 243F show they order and decline correctly.

**The rework is in `edison-hardening.patch`, not `early-trigger.patch`.** `early-trigger.patch` is the source of pinned variant
`bedb42dd`, its lock is outside `update_locks.py`, and the recipe versions only the hardening patch. The cost is that the hardening
patch removes code the earlier patch added, which reads less directly than editing it in place.

## Fixtures

Running the old fixtures on the new core gave exactly 19 failures, all in the groups below, and running the new fixtures on the
old core gave the same 19. See [negative-control.json](../tests/edison/reports/trigger-order-2026-11-04/negative-control.json).

| Group | Change | Why |
| --- | --- | --- |
| 004C | Caius CL1, opponent's Sangan CL2; relabeled | Owner's D1 ruling: Step 1 before Step 2 |
| 004B | None | Both triggers are Step 1, so chronology still decides |
| 005A-B, 017E-F | Broww CL1, then the Watapon decision | Step 1 before Step 3. 017E-F are "controls" that run under Edison |
| 006A-B | Same, with Watapon drawn by Jar at CL2 | Same steps; the across-link setup changes nothing |
| 017A-D | Same, with Watapon private; relabeled | Same steps. Watapon stays in the optional queue, offered once |
| 018A-D, 033A-D | Broww CL1, Watapon CL2; relabeled | P1's Broww is Step 2 before P0's Watapon in Step 3; P0's Broww is Step 1 before P1's Watapon in Step 4 |
| 235A | Asserts Bazoo's Ignition **is** offered; renamed | Owner's D4 ruling |
| 235B | None | The trigger was accepted, so there is no priority |

**005, 006, 017, 018 and 033 no longer separate Edison from Rule 5**, because both rules now give the same order. So I added
conformance fixtures 243A-G. On the Rule 5 side, 243A-F record "Rule 5 differs".

| Fixture | Asserts |
| --- | --- |
| 243A | Earlier Watapon is offered alone, CL1; later Parl CL2 |
| 243B | Decline Watapon: it is not offered again; Parl is still offered, CL1 |
| 243C | Accept Watapon, decline Parl |
| 243D | Non-turn player: P0's Broww CL1 (Step 1), P1's Watapon CL2 and Parl CL3 (Step 4). This is the only 243 fixture that fails on the old core |
| 243E, 243F | As 243A and 243B with a private hand trigger |
| 243G | Two Sangans destroyed together are both offered in one forced prompt |

Parl, Hermit of Dark World is not in the Edison pool. It was the available optional discard trigger, and conformance may load any card.

## Rule 5 and other Edison behavior

[trace-comparison.json](../tests/edison/reports/trigger-order-2026-11-04/trace-comparison.json) compares each case's trace before and
after the change.

| Suite | Rule 5 cases byte-identical | Edison cases byte-identical | Edison traces changed |
| --- | ---: | ---: | --- |
| Certification | 242 of 242 | 306 of 325 | The 19 above |
| Conformance | 142 of 142, plus 7 new | 140 of 142, plus 7 new | 241, 242 |

This is the fixtures' Rule 5 coverage plus reading the gated code, not a proof over all games.

## Gates

| Gate | Result |
| --- | --- |
| Baseline before the change | 567 PASS; `edison_rules` `d7a6be97...`, production `038e6dd1...`, as recorded |
| Certification | **567 PASS, 0 FAIL**: 513 + 10 + 2 + 4 + 3 + 4 + 8 + 4 + 19; identical replay traces |
| Conformance | 114 tests, 149 cases: 139 pass, 10 fail as recorded, 0 setup failures, 149 controls clean, no replay differences |
| Remaining failures | Unchanged: D6 261, 262B; D9 206; D11 216, 218, 219A, 219B, 219E, 223; D12 291 |
| 082D | **Passed with no re-pin.** `first_divergence.py` finds its 4 seeds identical in full decision traces under the heuristic and random policies. The learned policy's games are checked only by 082D's own fields |

Fresh soak seeds, final build:

| Stage | Seeds | Duels | Decisions | Crashes | Diagnostics | Contract violations |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 21 lists, Main and Extra | 76010000-503 | 504 | 113,927 | 0 | 0 | 0 |
| 21 lists, whole Side moved in | 76011000-503 | 504 | 108,363 | 0 | 0 | 0 |
| 114 confirmed crash cards | 76012000-3367 | 1,368 | 206,626 | 0 | 0 | 0 |
| Uniform decks, whole pool | 76020000-999 | 1,000 | 153,880 | 0 | 0 | 0 |

`soak.py`'s seed guard now also accepts the 76000000 block.

## Hashes

| Item | Before | After |
| --- | --- | --- |
| `edison_rules` | `d7a6be97...` | `6df1bd58...` |
| Production module, built and imported | `038e6dd1...` | `67423a99...` |
| `edison_conformance` | `7758a1fe...` | `ac41e4d3...` |
| Core variant | `aa797b55...` (`r1`), `e8978744...` | `5fe29f6a...` (`r2`), `e3adf73d...` |
| `core_hardening_patch_sha256` | `474d8522...` | `9adf1905...` |

`update_locks.py` changed only `core_hardening_patch_sha256` in `tests/edison/assets.lock.json`. The other locks and
`early-trigger.patch` are unchanged. `bedb42dd` and `aa797b55` are byte-identical, as are the two unpatched variants. Certification
fingerprints stay at 105 sources, and seven changed: `xmake.lua`, `rules.cpp` and five fixture headers. See
[hashes.json](../tests/edison/reports/trigger-order-2026-11-04/hashes.json).

## What a trained model would notice

No shapes changed. The prompts come in a different order when triggers span batches. On seeded fixture games, 79 of 80 full decision
traces are identical with the old module ([first-divergence-summary.json](../tests/edison/reports/trigger-order-2026-11-04/first-divergence-summary.json)).
In the other game, the first difference is a trigger-order point: Blackwing - Shura destroys Dupe Frog in battle, and Shura's
turn-player trigger is now offered before the opponent's Dupe Frog, where Dupe Frog used to go first. The sample is fixture decks only.
No training or win-rate study ran.

## Not verified, and open points

- **Evaluations and tools on the old order.** The re-baseline in the learner report ran on the `r1` binary. `tools/edison/forward_model/build.sh`,
  `pool_audit/build_probe.sh` and `pool_audit/build_audit_module.py` still link `bedb42dd`, which has the old order and no hardening fixes.
- **Batches are engine flushes, not rules timing.** One player's triggers from one physical moment that land in different flushes now
  get a forced order, with no choice. Shura and Dupe Frog show such events do fall in different flushes. They are opposing players, so the
  steps decide. I did not audit the same-player case.
- **Forcing a reinstall of `aa797b55` now would apply the r2 patch under the `r1` label**, because the recipe has one patch file. This
  was already the design. The installed copy is intact, and I used it for the negative control.
- **`r2` was installed once.** Its hash was not repeated by a clean reinstall.
- **Rule 6 Example 3 was not read locally.** 235A's note relies on the supervisor's description of it.
- **Watapon's eligibility** after the later discard is unchanged and assumed: its script has the delay flag, as test 005 already records.
- **The conformance matrix was not rebuilt**, so R7.2, R7.3 and the D4 row still show the old status.
- **Phase-start triggers.** 243 covers point-event triggers. Standby and End Phase trigger ordering has no new fixture here.
