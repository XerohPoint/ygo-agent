<!-- Sol (gpt-6.1-sol) follow-up on df801be, Nov 11 2026. Converted to UTF-8: the Windows CLI garbled its dashes and quotes, restored here by context. Paths under sol-review/ are in Sol's worktree and are not committed. -->

The Dark Panther override at df801be is now safe to merge for the reviewed Edison cases; both previous BUG findings are closed.

Reviewed the requested code diff, the report's Reviews section, and the eight-mutant definitions/results. No new BUG, GAP, RULING or CLAIM finding in this follow-up.

**NOTE — Both fixes hold in independent reruns.**

After refreshing the isolated headers from df801be, I rebuilt my original chain-control fixture. Enemy Controller still takes Panther at Chain Link 2, but Panther's Chain Link 1 now copies nothing:

```text
After CL2 Enemy Controller then CL1 Panther: controller=1 alias=43237273 copied_effects=0
```

My copy-only mutant, which removes RESET_CONTROL only from CopyEffect, now fails Edison 089F with “A copied effect outlived the control change”: **13 PASS, 1 FAIL**. Removing only the new controller check fails Edison 089G with “The copied name applied under the opponent”: **13 PASS, 1 FAIL**. The supplied fixed executable passes **14 PASS, 0 FAIL**.

Evidence and commands, run from the worktree root in WSL:

```bash
bash sol-review/build.sh baseline sol-review/fixture.cpp sol-review/round2/chain-control
sol-review/round2/chain-control chain-control trace
bash sol-review/build.sh copy-control-mutant tests/edison/rules.cpp sol-review/round2/copy-only-mutant
sol-review/round2/copy-only-mutant /home/mada/ygo-agent/assets/locale/en/cards.cdb /home/mada/ygopro-scripts --test=089
sol-review/round2/noctlcheck/rules /home/mada/ygo-agent/assets/locale/en/cards.cdb /home/mada/ygopro-scripts --test=089
```

Logs: `sol-review/round2/chain-control.txt`, `mutant-089.txt`, `noctlcheck-089.txt`, and `fixed-089.txt`.

**NOTE — 089F's ownership filter does not hide the copied effects in this fixture.**

The flag, name change, Bazoo's copied ignition effect and cleanup all have Panther itself as owner and handler. Changing controller does not change those pointers. Bazoo registers exactly one effect through Effect.CreateEffect(c), so its copied effect cannot be silently excluded by the external-owner filter. The pre-swap assertions also require a name change, a nonempty copied-effect group, cleanup and the historical flag.

The core's control effect instead has the control-changing Spell as owner and Panther as handler. Skipping that external effect is appropriate. After the swap, the helper scans the same card's indexer, and the assertions require every added category except the original usage flag to be absent. There is no turn-start deletion/allocation interval here that could confuse the saved pointer comparisons. The copy-only mutant's failure confirms the previously missing assertion now runs.

This is a precise check of this Bazoo fixture, not a general ownership classifier for every possible copied card script.

**NOTE — 089G cannot pass by omitting the Enemy Controller chain.**

It requires selection of Enemy Controller, its control-mode option, Blue-Eyes as Tribute, Panther as target, a placement on player 1's field, two chained and two solved links, and Panther actually controlled by player 1 at the checkpoint. Negated or disabled links are rejected. The fixed result also requires the usage flag, so skipping Panther's activation does not satisfy the test.

The stock control produces Bazoo's name and copied effect through the same setup. Independently, my original fixture still reaches the steal, and the missing-controller-check mutant fails 089G. I found no false pass for the specified legal route.

**NOTE — The controller check preserves the NOTE 5 interactions.**

The check compares the current controller with the activating player, not the card's owner. A borrowed Panther can therefore copy normally while its activating player controls it. The usage flag is paid before resolution and remains spent when the new check prevents copying.

I rebuilt and reran the negation fixture, both Phantom of Chaos copy orders, and the copy/Creature Swap/Brain Control-back fixture: their prior results are unchanged. Test 089 also passes the Panther-repeat/next-turn, face-down/face-up, and two-Panther cases. Destruction and return already break effect relation; an activation that is negated does not execute the operation. No unwanted change found in those cases. The separate target-control-change question acknowledged in Reviews remains outside this follow-up ruling check.

**What I ran**

Hand builds against the installed read-only r3 core, refreshed df801be headers, and existing review fixture sources. The two mutants fail their intended cases; the fixed 089 gate passes all 14. Negation and both Phantom orders remain limited correctly; Brain Control back leaves zero copied effects and zero activation options.

New logs/builds are under `sol-review/round2/`. The executable hash is `fa23666642c0745e3d2e1f2b1491d2a3ade8565bb928eb3378cc05cfeba6a364`; the module hash is `7e39a1138658050c75dc99996834ade20865a173ffbd765564cdcd11dca063ea`. I verified both; I did not run production-module games or the full certification suite.

Custom fixture seed: **86650000**. Existing certification/mutant fixture seed: **20100424**. Python ran through `/home/mada/ygo-agent/.venv/bin/python` with both thread limits set to 1. No xmake, network access, installation, outside-worktree writes or commits.
