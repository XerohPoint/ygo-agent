> Investigation report by a Claude subagent, November 10, 2026, kept as written except that ruling and card text quotes are
> paraphrased. Paths under `build/asan/` are in that agent's worktree and are not committed. Its proposed fix is now
> applied; see [docs/edison-dark-panther.md](../../../../docs/edison-dark-panther.md).

# Dark Panther crash and rules check

Date: 2026-11-10. Investigation only. No production code, script, patch or module was changed.

## Answer

**Root cause (high confidence).** The crash is a use-after-free in ygopro-core, caused by an Edison core patch.
`Effect.RemoveCountLimit` comes from `early-trigger.patch`, and the historical Treeborn Frog script uses it. It clears an
effect's `EFFECT_FLAG_COUNT_LIMIT` after `RegisterEffect` has already put the effect in `field::effects.rechargeable`.
The core only removes an effect from that set when the flag is set, so a deleted effect stays in the set as a dangling pointer.
Every later turn start then runs `peffect->recharge()` on freed memory. That loop reads freed memory, and sometimes it writes
one byte into whatever object now occupies the slot. In production, that byte write corrupts a Lua object, which crashes
`lua_close` at the end of the duel.

**Dark Panther is the trigger, not the cause.** Treeborn Frog's own effect is never deleted, so its stale entry is harmless.
The danger starts when Dark Panther copies a face-up Treeborn Frog (`CopyEffect(12538374, ...)`). The copy runs Treeborn's
historical script on Dark Panther, so `RemoveCountLimit` clears the flag on the copied effect. When the copy ends, the copied
effect is reset and then freed at the next turn start, and that is when the dangling pointer appears. Copying Treeborn is legal
play. Any card that copies Treeborn Frog or Dragoon D-END would hit the same bug, because the D-END historical script also calls
`RemoveCountLimit`.

**Rules (separate bug).** The simulator lets Neo-Spacian Dark Panther use its once-per-turn effect without limit when it
copies the opponent's Dark Panther. Each copy registers a new copy effect, and each new effect has an unused count limit.
A UDE ruling says this is illegal: the copied effect is the one Dark Panther just used, so it cannot be activated again that
turn. The "52 activations in 60 decisions" were 32 activations in one turn plus 20 End Phase prompts. Only the first
activation was legal. The End Phase prompts were not activations: they ordered the 32 stacked reset effects. This loop does not
cause the memory bug.

## Base commit and setup

- Worktree created at `dbf5142` (main). It lacked the corpus tools, so I checked out `edison` detached: the worktree is at
  **`3dd4daf`**, the same commit as `git log --oneline -1 edison` (`3dd4daf docs: add Sol's M3 Stage A recount and correct the report`).
  Nothing was committed.
- All build outputs and scripts are under `build/asan/` in this worktree (ignored by git). xmake was not used. The main
  checkout and `~/.xmake` were only read.
- valgrind and gdb are not installed. GCC 11's AddressSanitizer runtime (`libasan.so.6`) is present, so I used that.
- Instrumented module (`build/asan/build.sh`), built from the same sources as production:
  - ygopro-core `f9692965` exported from xmake's cached clone, plus `early-trigger.patch` and `edison-hardening.patch` (r2)
    from this commit, plus the package recipe's two header inserts. Every header matches the certified package
    `5fe29f6a` byte for byte, and every source file matches xmake's last patched build tree.
  - Lua 5.5.1 from xmake's cached tarball, with the recipe's defines (`LUA_COMPAT_5_2 LUA_COMPAT_5_1 LUA_USE_LINUX
    LUA_DL_DLOPEN`). Its headers match the installed package.
  - `ygoenv/ygoenv/ygopro/ygopro.cpp` from this commit.
  - Flags `-g -O1 -fno-omit-frame-pointer -fsanitize=address`. Production instead uses `-O3`, LTO and `-march=native`.
- Duels run through the corpus worker (`tools/edison/corpus/soak_worker.py`) unchanged, inside run trees that hold their own
  module. `run-prod` holds a byte-identical copy of the certified module (sha256 `b8fcd2bc...`).
- Under the production copy, the crash duel's decision trace matches the trace recorded in the m3-corpus replay
  (`2a1090b4...`, 241 decisions).

## 1. Root cause

### Sanitizer output

The crash duel is Side-in seed 88264285, fl41606 against fl41081, uniform play. Under ASan it fails in 10 of 10 runs, always
at the same point: the first turn start after the copy ends (decision 106). The production module could not have shown this,
because there the first freed read is silent and harmful only by chance.

```
==3600275==ERROR: AddressSanitizer: heap-use-after-free on address 0x51700020252c
READ of size 4 at 0x51700020252c thread T3
    #0 effect::is_flag(effect_flag) const core/effect.h:117
    #1 field::process_turn(unsigned short, unsigned char) core/processor.cpp:3757
    #2 field::process() core/processor.cpp:51
    #3 process core/ocgapi.cpp:137
    #4 ygopro::YGOProEnvImpl::next()
0x51700020252c is located 44 bytes inside of 696-byte region [0x517000202500,0x5170002027b8)
freed by thread T3 here:
    #0 operator delete(void*, unsigned long)
    #1 duel::delete_effect(effect*) core/duel.cpp:98
    #2 field::process_turn(unsigned short, unsigned char) core/processor.cpp:3717
previously allocated by thread T3 here:
    #0 operator new(unsigned long)
    #1 duel::new_effect() core/duel.cpp:80
    #2 scriptlib::effect_new(lua_State*) core/libeffect.cpp:20
    ...
    #12 interpreter::call_code_function(unsigned int, char const*, unsigned int, int) core/interpreter.cpp:362
    #13 card::copy_effect(unsigned int, unsigned int, int) core/card.cpp:2032
    #14 scriptlib::card_copy_effect(lua_State*) core/libcard.cpp:2053
SUMMARY: AddressSanitizer: heap-use-after-free core/effect.h:117 in effect::is_flag(effect_flag) const
```

`processor.cpp:3717` deletes everything in `core.reseted_effects` at the start of a turn. A few lines later, `processor.cpp:3756-3758`
runs `for(auto& peffect : effects.rechargeable) if(!peffect->is_flag(EFFECT_FLAG_NO_TURN_RESET)) peffect->recharge();`. Offset 44
in `effect` is `flag[0]`.

### The effect's life, traced

A second ASan build (`core-trace`) adds `@@trace` lines that log effects entering and leaving the set. The same duel shows:

```
turn 0  add_effect 0x5170000f6200 handler 12538374(orig 12538374) copy_id 0 code 4098 type 0x8a -> rechargeable   Treeborn's own effect
        RemoveCountLimit 0x5170000f6200 ... in_rechargeable 1                                                       stays in the set, harmless
turn 16 add_effect 0x517000202500 handler 12538374(orig 43237273) copy_id 2 code 4098 type 0x8a -> rechargeable    Dark Panther copies Treeborn
        RemoveCountLimit 0x517000202500 handler 43237273 copy_id 2 in_rechargeable 1
turn 16 remove_effect 0x517000202500 ... flag clear, LEFT IN rechargeable                                           End Phase: copy ends
turn 16 delete_effect 0x517000202500 STILL IN rechargeable (next loop reads freed memory)                         next turn start
==ERROR: AddressSanitizer: heap-use-after-free ... processor.cpp:3761
```

Code 4098 is `EVENT_PHASE+PHASE_STANDBY`. Type 0x8a is a field optional trigger, which gets the core's default once-per-turn
phase limit in `card::add_effect` (`card.cpp:1777-1782`). The insert is at `card.cpp:1918`. The skipped erase is at
`card.cpp:1994` (`if(peffect->is_flag(EFFECT_FLAG_COUNT_LIMIT)) ...rechargeable.erase(peffect)`); `field.cpp:1284` has the same
pattern. `RemoveCountLimit` lives in `early-trigger.patch` (lines 207-218) and only clears the flag. Its comment says to call it
after `RegisterEffect`, so the effect is always already in the set when it runs.

### Why it corrupts memory

`recharge()` runs `if(is_flag(EFFECT_FLAG_COUNT_LIMIT)) count_limit = count_limit_max;`. On a freed effect, this reads the 4 bytes
at offset 44. If bit `0x4` is set and bit `0x400000` is clear, it copies byte 67 over byte 66. The stale entry stays in the set
for the rest of the duel, so this repeats at every turn start, whatever object reuses the 704-byte slot.

To watch it happen without ASan, I built `core-stale`: production code at `-O2 -g` with no sanitizer, plus a monitor that prints
what the freed slot holds before production's loop touches it. I also added an LD_PRELOAD handler that prints a backtrace on
SIGSEGV. Results for the crash duel, 144 runs with ASLR on:

- **5 runs crashed**, all after the same last decision as production: decision 240, Elemental HERO Gaia's lethal direct attack.
  Four of them ran with the backtrace handler, and all four crashed in `luaC_freeallobjects+0xeb` ← `close_state` ←
  `interpreter::~interpreter` ← `duel::~duel` ← `handle_message`. This is the signature the corpus doc found in the stripped
  production module.
- **Every crash followed a byte-changing stale write, and the fault address is the pointer that write corrupted.** Run 26:

  ```
  @@stale turn 17 0x71a64c1c8210 reused_by_live_effect 0 flag0 0x000071a6 byte66 0x1b byte67 0x4c WRITE changes byte 66
  @@segv signal 11 fault address 0x71a64c4c70f0
    #2 luaC_freeallobjects+0xeb   #3 close_state+0x7d   #4 interpreter::~interpreter()   #5 duel::~duel()
  ```

  The slot had been reused by an object holding heap pointers (`0x000071a6` is the high half of a pointer at offset 40). The
  write turned byte 2 of the pointer at offset 64 from `0x1b` into `0x4c`. Lua's final GC sweep then followed `0x71a64c4c....`.
- Byte-changing writes appeared in 8 of 144 runs, and 5 of those 8 crashed. No run without such a write crashed.
- Whether a write happens depends on what reuses the slot and on bit 34 of the heap address. Both change with ASLR and thread
  timing. This explains why the duel replays with an identical trace but crashes only sometimes. The libc death at decision 124
  that the corpus doc reports fits the same corruption, landing in a malloc chunk header instead.

### Dark Panther's role: trigger, not cause

- **All six recorded crash duels show the same sanitizer error.** That is Side-in 88264285, the pair-main probe 88515054, and
  pair probes 88707769, 88711111, 88715221 and 88721413. Each shows the same `process_turn` heap-use-after-free, and the trace build
  shows a Dark Panther copy of Treeborn left in the set in every one.
- **Fresh seeds, new games** (fl41606 against fl41081, policy by seed, no network):

  | Run | Seeds | Duels | ASan heap-use-after-free |
  | --- | --- | ---: | ---: |
  | Production sources, 3 Dark Panther | 89000000-89000399 | 400 | 91 |
  | Proposed core fix, same duels | 89000000-89000399 | 400 | 0 |
  | Production sources, Dark Panther replaced by Gene-Warped Warwolf | 89001000-89001399 | 400 | 0 |

  So about 1 duel in 4.4 of this pair reads freed memory. Only the rare byte-changing write crashes, which fits the observed
  4 crashes in 24,000 duels.
- **Why the generated-deck probes found nothing.** Those decks put the same target in both seats. The bug needs Dark Panther on
  one side and a face-up Treeborn Frog (or D-END) on the other.
- **Other routes to the same bug.** I worked these out from the scripts and did not run them:
  - Phantom of Chaos copying a Treeborn Frog in its own Graveyard. None of the 4 corpus Phantom lists plays Treeborn.
  - Dark Panther copying Dragoon D-END (`c76263644`), which is in 1 corpus list.
  - The other pool copiers, Divine Neos and Pseudo Space, cannot target either card.
  - Treeborn is in 256 corpus lists and Dark Panther in 7, so every Dark Panther list is exposed against Treeborn lists.

## 2. Minimal reproduction

```bash
W=/home/mada/ygo-agent/.claude/worktrees/agent-a65df511231487b05; A=$W/build/asan
bash $A/build.sh       # AddressSanitizer module from the production sources, about 1 minute, output in $A/out
bash $A/mkrun.sh       # run trees: run-asan (ASan module), run-prod (copy of the certified module)
bash $A/replay.sh asan $A/replay/spec_88264285.json asan1   # exits 1 every run; report in $A/runs/asan1.err
```

The spec replays seed 88264285 with the two Side-in decks copied from the m3-corpus soak (`$A/replay/k88264285_{0,1}.ydk`).
`replay.sh` sets `LD_PRELOAD=libasan.so.6` and `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`. For the traced version:
`python3 $A/mktrace.py`, then `CORE=$A/core-trace OUT=$A/out-trace bash $A/build.sh`, then `bash $A/mktree.sh asantrace
$A/out-trace/*.so`, then `replay.sh asantrace ...`.

## 3. Rules check: Dark Panther once per turn

**What the simulator does.** The script `/home/mada/ygopro-scripts/c43237273.lua` has no historical override. It gives the copy
effect `SetCountLimit(1)`. A count limit belongs to one effect object. On resolution, the script calls
`c:CopyEffect(code, RESET_EVENT+RESETS_STANDARD+RESET_PHASE+PHASE_END, 1)`, which runs the target's `initial_effect` on Dark Panther.

When the target is the opponent's Dark Panther, this registers a **new instance of Dark Panther's own copy effect** with an unused
count limit, so it can be activated again. Each resolution also adds another End Phase reset effect (`e2`). The core keeps
Dark Panther's original effect too, because `CopyEffect` adds effects rather than replacing them.

**Seen in the 52-activation duel** (pair-main 88515054, eager play, turn 7):

- Player 0 used Brain Control on the opponent's Dark Panther. That Dark Panther then targeted the opponent's other face-up
  Dark Panther 32 times in one Main Phase.
- The trace build logged 32 new copy-effect registrations (`copy_id` 3 to 34, type `0x4a` ignition).
- At the End Phase, 32 stacked `e2` reset effects produced 32 ordering prompts: 20 for player 0 and 12 for player 1. These are
  not activations.
- The doc's 52 are player 0's 32 activations plus 20 ordering prompts.
- Per turn, from `build/asan/count_turns.py` on the verbose replay: turn 7 player 0 has 32 activations. Every other turn has at
  most 2, one per Dark Panther, which is legal.

**Card text and rulings for the March 2010 era:**

- Text of the Edison-era printings (POTD-EN005 2006, then DP03-EN007): once per turn, select a face-up monster the opponent
  controls, and Dark Panther gains its name and effects until the End Phase. DP03 reworded the target as a monster the opponent
  controls and added that this applies while you control Dark Panther face-up. I read the DP03 wording from Yugipedia's errata
  markup, so it is approximate. The current text also replaces Dark Panther's own effect with the copied monster's original
  effects. Every version is once per turn.
- UDE ruling (listed by Yugipedia under "Previously Official Rulings", UDE's Rulings):
  - It may target the opponent's Dark Panther, but the copied effect is the one it just used, so it cannot use it a second
    time that turn.
  - If it is flipped face-down after using its effect, the copy ends, and once it is face-up again it may use its effect again
    that turn.
- I found no Konami (tid) FAQ for the Dark Panther-on-Dark Panther case.
  - Konami FAQs on the page cover other cases: copying Phantom of Chaos (tid 41308/41305), Sangan's Graveyard effect after the
    copy ends (tid 50486), and Gemini (tid 50448). None contradicts the UDE ruling.
  - UDE rulings were the official TCG rulings until UDE lost the licence. I found no later override before March 2010, but I
    did not check every Konami TCG ruling from 2009-2010.

**Verdict.**

- The 52 activations are not legal. Only one activation per Dark Panther per turn is.
- Treeborn Frog as a target is legal, so the copy behind the crash is legal play.
- The stock Phantom of Chaos script already guards against this kind of repeat with a card flag in its cost. Dark Panther's
  script has no such guard.

Sources:

- Card text: [YGOPRODeck card page](https://ygoprodeck.com/card/neo-spacian-dark-panther-3684), and Yugipedia
  [card](https://yugipedia.com/wiki/Neo-Spacian_Dark_Panther) and
  [errata](https://yugipedia.com/index.php?title=Card_Errata:Neo-Spacian_Dark_Panther&action=raw) pages.
- Rulings: [Yugipedia Card Rulings: Neo-Spacian Dark Panther](https://yugipedia.com/index.php?title=Card_Rulings:Neo-Spacian_Dark_Panther&action=raw).
- Yugipedia blocks normal page fetches, so I read the raw wikitext through a fetch tool. Rulings and card text are paraphrased here.
  Nothing was downloaded.

## 4. Proposed fix (not applied)

### Memory bug: core patch

The fix has two parts:

1. **`RemoveCountLimit` also leaves the set.** In `scriptlib::effect_remove_count_limit`, add
   `peffect->pduel->game_field->effects.rechargeable.erase(peffect);`. The effect no longer has a limit, so it should not be
   recharged.
2. **Defensive, so later flag changes cannot repeat this.** `card::remove_effect` (`card.cpp:1994`) and `field::remove_effect`
   (`field.cpp:1284`) should erase from `rechargeable` whether or not the flag is set. Erasing a non-member does nothing.

The tested diff against the production core sources is `build/asan/fix.diff` (card.cpp, field.cpp, libeffect.cpp; 3 small hunks).

Files it would touch:

- `repo/packages/y/ygopro-core/edison-hardening.patch`: add the three hunks. It applies after `early-trigger.patch`, so it can
  edit `effect_remove_count_limit`.
- `xmake.lua`: `edison_hardening = "r2"` becomes `"r3"`. The package recipe says to bump this whenever the patch changes,
  because the package hash covers configs and not patch contents.
- A regression test in `tests/edison/`:
  - What it does: Dark Panther copies Treeborn Frog, two turns pass, and the test asserts that every member of
    `effects.rechargeable` is still in `pduel->effects` and carries `EFFECT_FLAG_COUNT_LIMIT`.
  - Files: a new `panther_copy_tests.h`, its call in `rules.cpp`, and `c43237273.lua` added to the script list in `harness.h`.
- Rebuild the production module. Its hash changes from `b8fcd2bc`, so it needs recertification. The hash pins in
  `build_corpus.py`, the certification report and anything else that checks `b8fcd2bc` also need updating.

**Tested in this worktree, ASan build `out-fix` only:**

- All six crash duels are clean.
- 400 fresh pair duels: 0 errors, where production sources give 91.
- Decision traces are identical to the certified production module for:
  - the two recorded crash duels
  - the four pair crash seeds
  - all 309 fresh duels that also finish on the unfixed ASan build
  - all 91 fresh duels that the unfixed ASan build stops on, replayed on the production copy, where none crashed this time

So the fix does not change play in any of these.

A script-only alternative would replace `RemoveCountLimit()` in the Treeborn and D-END overrides with `SetCountLimit(255)` before
`RegisterEffect`. I did not test it, and it leaves the core trap in place for future historical scripts.

### Rules bug: Dark Panther override

- **What:** add a `c43237273.lua` override in `ygoenv/ygoenv/ygopro/historical_scripts.h`, as written in
  `build/asan/proposed/c43237273.lua`.
- **How it works:** the stock script plus a cost of `chk==0: GetFlagEffect(43237273)==0`, then
  `RegisterFlagEffect(43237273, RESET_EVENT+RESETS_STANDARD+RESET_PHASE+PHASE_END, 0, 1)`. This is how the stock Phantom of
  Chaos script blocks the same loop. The flag is on the card, so a copied Dark Panther effect sees it. `RESETS_STANDARD` clears
  it on a flip face-down or when the card leaves the field, which matches the second UDE ruling.
- **Checked:** with the certified module and this script in a script overlay (run tree `run-panther`), duel 88515054 replays with
  1 activation for player 0 on turn 7 instead of 32. Turns with two different Dark Panthers still allow one activation each.
- **Not checked:** the flip face-down then face-up case.
- **Cost:** this changes the module and needs recertification. It would also need a test in `tests/edison/`.

## Limits

- ASan ran at `-O1` without LTO or `-march=native`, and the monitor build at `-O2`. Heap layout differs from production, so crash
  rates are not comparable: 5 of 144 runs here, 2 of 15 for production run alone. The fault site and its cause are the same.
- I did not run D-END, Phantom of Chaos or Divine Neos cases. The extra exposure listed above comes from reading code and lists.
- I did not rerun the full corpus soak with the fix.
- Processes: at most 8 duel processes at once, all at nice 19. Seeds:
  - replays of 88264285, 88515054, 88707769, 88711111, 88715221 and 88721413
  - new games 89000000-89000399 and 89001000-89001399
  - uniform, eager and Heuristic play only, no network

## Main module check

`sha256sum /home/mada/ygo-agent/ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so` gives
`b8fcd2bcf7774ae4edf8a4a800c2c55c006f62cb5cc1ef925fe316365a1ddf6f`, the same before and after this work. The other `.so` in that
directory, `ygopro_ygoenv.so` (`55ad5f03...`, dated July 2024), is also unchanged.
