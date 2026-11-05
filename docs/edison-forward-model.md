# Edison forward model

## November 5, 2026: the forward model on the certified rules build, after review

Branch `m2-forward-model`. Sol reviewed `fae7262` and then `9380186`, and both reviews found real problems. This section replaces those commits' reports, and every number in it comes from the final build, unless it is marked as a replay of the older corpus. Nothing here is search. The older spike below keeps its own measurements, labelled as old core.

**Result: the forward model runs on today's certified rules, on all 21 gauntlet lists. It matches production at every decision, restores byte-exactly, and deals only what the public history allows.** One deal seed gives one set of hidden cards across true worlds with one public history, in all 5,440 of 5,440 dealt pairs of the wider leak test. It does not always give one future. A random pick over a group reads the true load order, so one seed can discard another card: 119 of 2,265 compared worlds on Sol's Thestalos line. **The defensible claim is about seeds as samples: over deal seeds, tests found no dependence on load order,** and the same tests catch a planted first-card pick at p below 1e-100. See [What holds across true worlds](#what-holds-across-true-worlds). A hand trap declined in private no longer shows: Gorz differs in 0 of 1,842 compared decisions, against 915 on the old rule. The suite now has a verdict: `run_all.sh` returns the exit code of a checker, and the checker is itself tested on 65 planted faults.

**What is not resolved:** the deal still assumes the opposing decklist is known, a refusal can still reveal a little, one seed does not name one future across load orders, 2.1% of positions refuse every deal, and deals ignore what an opponent's passes imply. See [Still open](#still-open).

### What the review found

| # | Finding | Reproduced? | Now |
| --- | --- | --- | --- |
| 1 | A privately declined Gorz leaves a chain entry on the hand card, so deals refuse in that world only | Yes: 20 of 20 against 0 of 20 | Fixed. The entry is stale, so the deal clears it |
| 2 | R4's pairs share one private timeline, so it could not see finding 1 | Yes, by reading | R4W added: 2,770 accepted worlds, 1,017 with a private difference |
| 3 | Deals break a hard rule: a Level 6-10 monster in a slot Normal Set with no Tributes | Yes: 467 of 1,000 | Fixed: 0 of 1,000 on the same line. Control 467 of 1,000 |
| 4 | Exact option order depends on true history | Yes | Mostly fixed: 3,494 of 3,494 in R4, 5,436 of 5,440 in R4W now match in every byte. Every remaining pair is verified as load order, which is left in place. See [What holds](#what-holds-across-true-worlds) |
| 5 | The hang watchdog stops nothing while the arena is off | Yes: not stopped in 5 s | Fixed. Only glibc's allocator defers a jump now |
| 6 | A marker hides behind a flush, and one printed before a throw is emitted later | Yes, both | Fixed: stdout is our own stream, captured during a guard. The `size_t` underflow went with the old pointer arithmetic |
| 7 | Gates report failures without failing, and a capped rollout counted as finished | Yes, by reading | Fixed: `check_results.py`. A world finishes only on `kTerminal` |
| 8 | The example scores production's decision limit as a loss | By reading | Fixed in the example and the header's sketch |

**The second review** (of `9380186`) found three things, all reproduced:

| # | Finding | Now |
| --- | --- | --- |
| 1 | The same seed discards another card and changes Life Points across load orders, on a natural line | True, and left in place. Measured below, and the contract now says it. Sol's seed 85550000 is outside this round's block, so I rebuilt the line on 85660000: 119 of 2,265 |
| 2 | The checker read no continuation observation, winner or coverage counter | Fixed. All six of Sol's mutations exit nonzero, and are in `test_checker.py`. Five of the six pass the old checker; the sixth, which it always caught, is Sol's control |
| 3 | `KNOWN` was a number, so an unrelated failure under it passed | Fixed. Each pair that fails any comparison must be listed and verified as load order. See [The suite's verdict](#the-suites-verdict) |

R4W found two bugs neither review did: a dealt monster's attack counts (below), and an effect-id limit that read the opening hands (below).

### The pin

| Item | Value |
| --- | --- |
| Core package | `5fe29f6a...`, `edison_early_trigger = true`, `edison_hardening = "r2"` |
| Core library | `e3adf73d...` |
| Production module compared against | `b8fcd2bc...`, the certified build |
| Header | `ygoenv/ygoenv/ygopro/ygopro.h` at `b27c798`, unmodified |

`build.sh` finds the package by its manifest and checks the library's hash. It refuses anything else and more than one match. `test_build_refusal.sh` shows 6 of 6 cases refused. No production source, core, patch, `xmake.lua` or `tests/edison` file changed.

### Corpus

Production's own heuristic in both seats, through the certified module. Each of the 21 registry lists against its next neighbour, in both seats: 42 cells, 16 games each, 672 games, 120,811 decisions. Seeds **85600000 + cell × 1000 + env, so 85600000-85641015**. Fixtures use 85650002 (the Set line), 85650000 (the API check), 85650100-85650267 (hand traps) and 85660000-85660002 (load order). `collect_corpus.py` refuses anything outside 85600000-85699999. This is a new corpus for this round, so every exception on it is unseen. Position sampling uses other constants, which are not game seeds.

### Production equivalence

| 672 games, 120,811 decisions, 21 lists | Differences |
| --- | ---: |
| Acting player | 0 |
| Option count | 0 |
| Winner, win reason | 0 |
| `cards_`, `global_`, `decision_`, `selection_` | 0 |
| Offered `actions_` | 0 |
| `h_actions_` | 0 |
| Games with any difference | 0 |
| Core or Lua diagnostics | 0 |

Every field's sha256 is compared at every decision of every game. `mask_` is not compared, as `oppo_info` is off and production never writes it. Whole-game digests agree across two plain runs, a scrambled heap, the arena allocator, and encoder on or off, and the checker now fails if they do not.

Since the label fix, a history row takes its card id from the id `WriteState` stamps on the action, so stepping with only `msg_` stamped writes different history. `FmEnv::stamp()` applies the rule production uses for lone options. Against `WriteState` it gave the same id for every option at every decision: 0 differences.

### Snapshot correctness

The check from [Correctness](#correctness) below, on all 21 lists: every option at each branch point, continued by one seeded random policy to the end, against a straight run in a fresh process. Each digest also carries the root's own full state, so a missed rollback shows before anything moves.

| Check | Result |
| --- | --- |
| Branch points / options / reference decisions | 504 / 1,682 / 265,415 |
| Truncated, failed positions, diagnostics | 0, 0, 0 |
| Arena restore vs straight run: decision chain, deep hash, every observation byte | **0 mismatches** |
| Options in reverse order | **0 mismatches** |
| Garbage above the allocator top | **0 mismatches** |
| Fork vs straight run; replay repeated; replay in a scrambled heap | **0 mismatches** |
| Return to the position and finish the recorded game | **0 mismatches** |
| Everything outside the core (`env_hash`, below) | **0 mismatches** |
| **Whole arena, byte for byte** | **0 bytes differ in 4.36 GB, over 1,682 comparisons** |

**How rollback of state outside the core is proved:**

- The observation chain includes history bytes at every decision.
- `env_hash` folds in both history rings and cursors, reveal memory, the Deck binder's ranks, decision context, multi-select and prompt state, the environment's random streams and the message buffer.
- The whole-arena comparison needs no list of what matters. Two children start from one root. One runs an option straight. The other runs other options first, restores, then runs it. Every byte of every object, live or freed, allocator included, must match.
- Statics: three playouts change one thing in the program's 2.7 MB of `.data` and `.bss`, the test bed's own `g_query_buf` scratch (3236 bytes). `check_statics.py` fails on any other.

**Negative controls.** Each plants a real miss in the rollback, on 126 branch points, 455 options:

| Planted | Arena mismatches | Decision | Deep | Observation bytes | Other checks |
| --- | ---: | ---: | ---: | ---: | --- |
| History rings not rolled back | 329 | 0 | 329 | 329 | Reverse 455, return 126, bytes 31, failed positions 0 |
| History cursors not rolled back | 327 | 0 | 327 | 319 | Reverse 453, return 126, bytes 454, failed positions 0 |
| A stale reveal key left behind | 455 | 0 | 455 | 181 | Reverse 455, return 126, bytes 1, failed positions 0 |
| A stale Deck binder rank left behind | 455 | 0 | 455 | 0 | Reverse 455, return 126, bytes 0, failed positions 0 |
| Core random stream one step on | 455 | 407 | 455 | 411 | Reverse 455, return 86, bytes 0, failed positions 45 |

### Determinizer

`fm_deal.h`. A deal's inputs are the slot layout, a public memory, an explicit assumed list and a seed. The dealt codes come from `assumption.opponent_main` minus every opposing Main Deck card outside the dealt slots. Hidden codes are read only to pick which object carries a code and to refuse.

**What a deal assumes, and what it does not re-deal:**

| Object | Treatment | Reported as |
| --- | --- | --- |
| Viewer's hand, set cards | Known, untouched | |
| Viewer's Deck | Re-dealt, from its own multiset | `moved` |
| Opposing Main Deck | **Assumed known**, `DealAssumption::opponent_main`. A list that cannot be true is refused | `kListMismatch` |
| Opposing Deck, hand, face-down zones | Re-dealt from that list, under the hard rules below | `moved` |
| Cards the viewer knows, such as a searched card shown and still in hand | Pinned | `known_pinned` |
| Opposing Extra Deck | **Not re-dealt.** Sound only under the same assumption | `opponent_extra_not_redealt` |
| Face-down banished | **Not re-dealt.** An unseen opposing one would bias the pool, so the deal is refused | `facedown_banished_not_redealt`, `kFaceDownBanished` |
| Core and environment random streams | Reseeded | |
| Other seat's history rows | Private card ids zeroed | |

**Hard rules (finding 3).** How a face-down monster got there, and how many Tributes it took, are public. The deal reads them from the slot: `summon_info` says Normal Summon type, and `material_cards` holds the Tributes, which now move with the slot.

| Slot | What may be dealt there |
| --- | --- |
| Normal Set, no Tribute | A monster that can be Normal Set, Level 4 or lower. Also Blackwing - Sirocco the Dawn if its Set procedure was open: no monsters of your own and one of theirs at the last decision |
| Normal Set, 1 Tribute | A monster that can be Normal Set, Level 5 or 6 |
| Normal Set, 2 Tributes | A monster that can be Normal Set, Level 7 or higher |
| Other face-down monster | Any monster. None occurred unrevealed on these lists |
| Spell and Trap Zone | A Spell or Trap that is not a Field Spell |
| Field Zone | A Field Spell. None on these lists |

"Can be Normal Set" excludes `TYPE_SPSUMMON`, Ritual and Extra Deck monsters, as `card::is_summonable_card` does. **What else I checked for this class of rule**, by a script scan of all 223 codes that `check_set_rules.py` now enforces:

- **Public Level changes.** No card on these lists changes a Level in hand. `EFFECT_CHANGE_LEVEL` is on Tragoedia and Copy Plant only, on the field. So the printed Level applies, and a deal never sees a modified hand Level.
- **Tribute changes.** No `EFFECT_DECREASE_TRIBUTE_SET`, `EFFECT_DECREASE_TRIBUTE`, double or triple Tribute, or extra Set count.
- **Set procedures.** Only Sirocco. Evil HERO Malicious Edge has a Summon procedure and no Set one.
- **Cards that cannot be Set.** No `EFFECT_CANNOT_SSET` or `EFFECT_MONSTER_SSET`. `EFFECT_CANNOT_MSET` is on Hidden Armory and Miracle Fertilizer only, as a this-turn rule on the player.
- **Face-down by effect.** Book of Moon and Flip Flop Frog turn a face-up card, which stays tracked. Mega Hamster reveals what it Summons, which the message tap records. Ultimate Offering is an ordinary Normal Set.
- **Not constrained:** hand and Deck. Nothing public says what they hold, beyond the list.

If a list gains a card that breaks these facts, `check_set_rules.py` fails.

**Keeping the deal uniform.** Slot rules can overlap, so each constrained slot now draws in proportion to how many ways the remaining slots can still be filled, then the rest are shuffled. Nothing is rejected and retried.

| Check | Result |
| --- | --- |
| The Set line from the review, 1,000 deals | 0 above Level 4, 0 rule violations, 0 refused |
| **Control:** the old rule on the same line | 467 above Level 4, 467 rule violations |
| R2 deals checked by an independent reading of the rules | 0 violations in 11,844 |
| **True world rejected**, recorded play: every face-down slot, both viewers, every decision | 0 of 553,228 slot views over 120,811 decisions |
| **True world rejected**, random play | 0 of 395,972 slot views over 161,490 decisions |
| Of those random-play views: no Tribute / 1 / 2 / Sirocco's procedure open | 78,896 / 13,031 / 4,838 / 17,133 |
| Uniformity, a free hand slot, against exact odds counted by brute force | Chi-square 3986 on 3,865 degrees of freedom (ratio 1.031), 167 positions, 2 past 3 sigma |
| Uniformity, a constrained slot | Chi-square 1279 on 1,332 (ratio 0.960), 121 positions, 1 past 3 sigma |

An earlier run failed the checker on the constrained slot, at ratio 0.84. A ratio that low means counts too even for chance. The test used one seed list for every position, so positions with one pool got the same counts and their chi-squares were not independent. With seeds per position it is 0.960. No deal code changed for this.

Random play was needed: the heuristic almost never Sets a low-Level monster, so recorded play has 0 such views. A face-down monster equipped through Armory Arm, or a card of the other player's in a face-down zone, is not dealt: the deal refuses it. 166 slot views in random play were of that kind.

**Refusals.** Over 12,096 deals at 2,016 real roots:

| Outcome | Deals | |
| --- | ---: | --- |
| Dealt | 11,844 | 97.9% |
| Refused: the pending operation holds a hidden card | 252 | 2.1% |
| Refused: a hidden card carries rule state | 0 | 0.0% |
| Positions where every deal was refused | 42 of 2,016 | 2.1% |

`fae7262` refused 234 deals on a different corpus: 150 core reference and 84 rule state. **Rule-state refusals fell to 0 once stale chain entries and Tributes left the check.** The core writes an entry when it merely offers a private trigger (`card::create_relation`, `processor.cpp`) and does not take it off when the player declines, and it looks an entry up only by effect and chain id, and chain ids are never reused. So an entry whose chain is not live means nothing, and the deal clears it. One whose chain is live refuses. Tributes, which `carries_object_state` also refused on, now move with the slot.

The core-reference count rose in one way, deliberately. A viewer's own Deck card held by a pending operation, such as the top card an effect is about to Summon, carried its true identity into the sampled world. R4W found it: two worlds went different ways at the next prompt. Now only the candidates of the viewer's live prompt may move, and any other pointer into the viewer's Deck refuses.

**What a refusal can still reveal.** `kCoreReference` reads no hidden code, but which cards a pending operation holds can depend on hidden cards. In the hand-trap pairs below, it separated two worlds at 1 of 3,996 compared decisions, where a pending-chain list held an opposing hand card's optional trigger while the viewer was prompted first. The hidden fact it reveals is that the opponent holds a card with a trigger waiting at this moment. `kHiddenObjectState` refused nothing here and would reveal that a hidden card carries rule state: a counter, relation, equip or added effect. A search must take no signal from either, and should not re-roll the seed on them, since they do not depend on it.

### R1-R5 on all 21 lists

2,016 positions (every game, three stages), 6 deals each, 0 crashed positions:

| Test | Result |
| --- | --- |
| R1. Reorder both Decks: playouts completed | 12,096 of 12,096. Same seed reproduces all (0 differ) |
| R2. Full re-deal: dealt | 11,844 of 12,096 |
| R2. Playouts completed | 11,844 of 11,844. Same seed reproduces all (0 differ) |
| R2. **Viewer's own observation changed by the deal** | **0 of 11,844**, including 1074 own Deck options re-bound |
| R1, R2. Invariant failures, diagnostics, exceptions | 0, 0, 0 |
| R2. **A slot's attack state changed by the deal** | **0 of 11,844**. Control, state left on the object: 750 of 750 |
| R2. Cost per deal | 284 µs on this loaded machine |
| R3. Re-deal vs natural duel, same assignment: board | 1,538 of 1,538 |
| R3. Forward chain, canonical order | **1,538 of 1,538** |
| R3. Winner | 1,538 of 1,538 |
| R3. Exact option order against the natural duel | 1,470 of 1,538. Not a leak measure: see below |
| R3. Alternate-history acceptance | 1,747 of 5,922: early 1107/1986, mid 328/1971, late 312/1965 |
| R5. Cards outside the true list: playouts completed | 12,048 of 12,048, 276 µs. 70.7% of those cards moved in play |

**R3 exact order** fell, and that is the cost of the fix below. A sampled world's effect order no longer follows the true duel's arrival order, so it no longer copies the natural duel's arbitrary order among equal effects. Canonical chains, the rule outcome, are unchanged.

**The two old undiagnosed differences from `fae7262`'s corpus** (R3 game 57 index 11, R4 game 28 index 150) are on seeds I may not replay, so neither was re-run. The same class recurred on an earlier corpus and its cause is found: the environment's own random stream, which decides the End Phase random discard. The rig's reset does not draw from it as `reset()` does, so natural and swapped worlds held different streams, and the old deal reseeded only the core's. Given one stream, the R3 chain matches in all 1,538 above. That this explains those two old cases is likely and not verified.

### The leak tests

**R4** (two true worlds with one private timeline, one public history, one deal seed) and **R4W** (finding 2: the wider family).

R4W replays the recorded game against a rigged Deck. Worlds must agree on public zones, life, cards shown, the viewer's prompts and every byte of the viewer's observation at each of the viewer's decisions. At an opposing prompt only the rigged world has, it declines. At an opposing prompt only the true world had, if that was a decline, it skips it. Nothing about the first world filters the sample, as R4's eligibility check did.

| | R4 | R4W | R4W, pairs with a private difference |
| --- | ---: | ---: | ---: |
| Samples / accepted | 5,922 / 1,747 | 6,048 / 2,770 | 1,017 worlds |
| Extra / skipped private prompts | | 5,378 / 3,546 | |
| Pairs | 3,494 | 5,540 | 2,034 |
| Refused in both worlds | 0 | 100 | |
| **Refused in one world only** | 0 | **0** | 0 |
| **Hidden slots equal** | 3,494 | **5,440** | 1,990 |
| **Forward chain, canonical order** | 3,493 | **5,439** | 1,990 |
| Winner | 3,494 | 5,440 | |
| Viewer's observation at the root | 3,494 | 5,440 | |
| Next 8 viewer decisions, all bytes | 3,494 | 5,440 | |
| Exact order, viewer's bytes to the end of the game | 3,494 | 5,436 | |
| **Every seat's bytes to the end of the game** | 3,494 | **5,436** | 1,988 (exact chain) |
| Hard-rule violations | | 0 | |
| Control, no re-deal: chains equal / viewer bytes equal | 20 / 18 of 1,747 | | |

**R4W's rejections:** 3,022 where the true world took an opposing action other than a decline that the rigged world could not take, 255 on a public difference, 1 on a viewer's byte. **What it still misses:** worlds where the opponent took a different private action, as a different Set or a different choice at a private prompt. Those need a model of the opponent's choices, and rejection here is why R4W accepts 45.8%. It also shares R4's blind spots: one list, one opposing Extra Deck.

**Hand traps and private prompts.** Each opposing card with a trigger or Quick effect from the hand, in each list that holds it: one duel with that card in the opening hand, one with it at the bottom of the Deck, one scripted public line, and the opponent declining everything. Four seeds per list pair. Wherever the viewer's views have matched so far, the two worlds must deal alike.

| | Result | Old rule (`refuse-relate` control) |
| --- | ---: | ---: |
| Fixtures | 224 | 224 |
| Fixtures with extra private prompts | 135 | |
| Extra private prompts: Gorz / D.D. Crow / Tragoedia / Battle Fader | 267 / 229 / 97 / 47 | |
| Viewer decisions compared | 3,996 | 3,996 |
| **One world refused, the other dealt** | **1** | **1,465** |
| Of those, Gorz | 0 of 1,842 | 915 |
| Slots differ / canonical chain differs / exact differs | 0 / 0 / 0 | |

Honest, Kalut and Herald of Orange Light reached no private prompt on this line, as it has no battle with an opposing monster. So those three are not exercised here. Sol's exact Gorz pair sits on a seed outside this round's block, so I reproduced it on Sol's build of `fae7262` (20 of 20 refused against 0 of 20) and the regression test rebuilds the same situation on seeds 85650100-85650267.

**The planted leaks.** 170 pairs each in R4:

| Planted | Slots equal | Canonical chain | All bytes | Root bytes |
| --- | ---: | ---: | ---: | ---: |
| Deal keeps the true opposing hand | 14 of 164 | 15 | 14 | 164 |
| Deal's seed mixed with the true opposing hand | 14 of 164 | 14 | 14 | 164 |
| Deal's pool keeps the true slot order | 0 of 164 | 0 | 0 | 164 |

Root bytes stay equal because the viewer's own view holds no hidden card, so that row alone proves nothing.

### What R4 found on the way

Each would have been a real leak through prompts. They are most of finding 4. Game numbers here are from older corpora.

- **Effect registration order.** The core lists same-code effects in registration order, so a dealt card kept the place of where it truly was. All hidden slots now re-register in slot order.
- **Effect ids.** Offered effects sort by id. An id says when a card was created, in Deck order, or when it last arrived somewhere. A dealt card kept both, so option order read the true opening order and history. `order_effects_by_arrival` now orders every effect in the duel by the slot's cardid or arrival, then hands the same id values back in that order.
- **The availability flag.** A conditional effect takes a fresh id the first time its condition holds, and the flag saying so stays on the effect (`effect::is_available`). A Swap Frog whose Summon was open in the true duel therefore sorted differently. A dealt card starts with it clear.
- **A pointer into the viewer's Deck.** See Refusals above.
- **Prompt candidates after a deal.** The core sorts a prompt's candidates by Deck position, so option k means the k-th copy. Left in the old order after a deal, one option took a copy from a different slot depending on the true Deck, and chains parted some turns after an own Deck search. `restore_live_order` sorts them again if they were sorted before.
- **What a slot's card did this turn (not in the review).** The core's card constructor never sets `announce_count` and its neighbours. They are cleared only when a card reaches the field or a turn starts. A dealt card arrives without either, so it kept what its object held. At one R4W position on the older corpus, game 243 index 191, that was 16 from heap residue, so the face-down monster could not change position in one world only. These bytes and the turn's indestructible counts now move with the slot, and R2 checks them, the map's contents included.
- **The limit on effect ids at creation (not in the review).** The limit that says "this id was given at creation" was read at the first decision, and how far a duel gets by then depends on the opening hands. With no prompt before the first Main Phase, a conditional effect had already taken a fresh id below it, so it sorted with cards at creation in that world only. Two Judgment Dragons then listed in the other order. The limit is now the first card drawn, which depends only on the lists. This was 4 of the 8 pairs below.

Found by reading, with no failing test first:

- **Public memory.** A card stays tracked only while no hidden event could have moved it, judged from public counts alone. A card entering a Deck stops being tracked.
- **Reveals between decisions.** A message tap reads `MSG_CONFIRM_CARDS` and `MSG_CONFIRM_DECKTOP`.
- **The other seat's history.** Rows not public when written lose their card id: 43,782 ids over 11,844 deals.
- **The Deck binder.** Own Deck options re-bind by production's own rule, which the viewer's bytes confirm above.

### What holds across true worlds

For one public history, one assumed list and one deal seed:

| Claim | Holds? | Evidence |
| --- | --- | --- |
| Which code goes in which hidden slot | **Yes** | 3,494 of 3,494 in R4, 5,440 of 5,440 in R4W |
| Same true world, same seed: same world, to the last byte | **Yes** | R1, R2 repeats: 0 differ |
| Same load order: same future, every byte | **Yes, in every pair measured** | R4 with one load order: 3,076 of 3,076 canonical, 3,076 every byte. R4 only. R4W does not split on this |
| Different load orders: same future | **No** | Same-seed differences below |
| Over deal seeds, a sampled world's distribution given the public history does not depend on load order | **Not refuted, tested on the lines below** | Homogeneity tests below |

So a search must treat seeds as samples. It must not compare or cache sampled results seed for seed between true positions, and a finite set of seeds gives slightly different evaluations in different true worlds. Those differences should average out. A proof would have to cover every group a script can read, and the audit below is that far and no further. `fm_world.h` says this on `deal`.

**Why the seed stops short.** The core reads `cardid` in one place, `card_sort`, which orders every `card_set`. `cardid` is a card's place in the loaded list, and a hidden slot's number and a public card's number both reflect the true list. Group.RandomSelect draws an index into a group, so one draw takes another card when the numbers fall in another order.

**The test.** Natural duels only: each variant is a real duel from a rigged initial Deck, with the same forced opening cards, one searched or milled card at a place spread across the Deck, and the rest in the variant's own order. A variant counts only if every viewer observation and public hash before the root match the first variant's. Each variant has its own deal seeds, so counts are independent, and the table is a chi-square homogeneity across variants. Thin columns are pooled. Refusals and worlds that never reach the pick are columns too. Seeds 85660000-85660002.

| Line | Variants with one public history | Deal seeds per variant | Reached the pick | Refused | **Same seed, other result** | Homogeneity by place picked | By card and Life Points |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Thestalos discards from the viewer's hand, with a searched card in it (Sol's line) | 16 of 16 | 4,000 | 18,202 of 64,000 | 0 | 119 of 2,265 | 74 on 90, p = 0.892 | 346 on 360, p = 0.693 |
| The viewer's Thestalos discards from the opposing hand, with a card they searched in public | 16 of 16 | 1,500 | 24,000 of 24,000 | 0 | 944 of 7,680 | 80 on 75, p = 0.324 | 411 on 390, p = 0.221 |
| Power Tool Dragon picks from three Equip Spells in the viewer's Deck | 16 of 16 | 1,000 | 15,222 of 16,000 | 0 | 0 of 7,320 | 36 on 45, p = 0.830 | 22 on 30, p = 0.860 |

**The plants**, in a Lua override in those runs only. `first` takes the group's first card, which reads load order. `min2` draws twice and keeps whichever comes first in the group, a weaker lean. The last column is the estimate in [Renumbering](#renumbering-an-estimate).

| Line | `first`: homogeneity | `first`: same seed, other result | `min2`: homogeneity | Numbered by place: same seed, other result |
| --- | ---: | ---: | ---: | ---: |
| Thestalos, viewer's hand | 4,079 on 30, p < 1e-100 | 447 of 2,265 | 107 on 90, p = 0.102 | 0 of 2,265 |
| Thestalos, opposing hand | 13,357 on 15, p < 1e-100 | 2,880 of 7,680 | 134 on 75, p = 3.3e-05 | 0 of 7,680 |
| Power Tool Dragon | 49 on 45, p = 0.300 | 0 of 7,305 | 31 on 45, p = 0.944 | 0 of 7,275 |

Power Tool's three cards never moved, so their numbers keep Deck order whatever the load order was. One seed takes one card there, which is why the plant changes nothing on that line. The line shows the pick's odds are flat. It cannot show more.

**Across the corpus.** In the wider leak test, 169 accepted pairs, 100 deal seeds a world, own seeds each, winner by game length: chi-square 371 on 409, p = 0.913, 3 pairs past 3 sigma. Control, a deal that keeps the true opposing hand: chi-square 776 on 340, p = 3.7e-36. This covers true worlds that differ in private prompts as well as load order. It has little power per pair, and it is random play.

**What this does not show.** A small lean can be missed. `min2` is the weakest plant tried. The test saw it on the opposing hand, at p = 3.3e-05, but not on Sol's line, at p = 0.10, with fewer worlds reaching the pick and numbers that change rank less often. So on that line, the test rules out a lean like `first` and not one like `min2`. Nothing here covers other lists, other policies, or picks on lines not run.

### The pairs that differ

**On this corpus.** Every pair that fails any comparison is listed, with a causal test: give world B world A's numbers, place by place, and change nothing else. World A plays the very walk that showed the difference, so if world B now follows it to the end, in chains and every byte, numbering was the whole of the difference. The checker fails on any pair that does not pass. A planted leak does not: 0 of 150 `leak-hand` differences pass this test.

| Test | Game / index / deal | Duel seed | Extra / skipped private prompts | Canonical chain equal | Winner equal | Viewer's bytes equal | First difference |
| --- | --- | ---: | ---: | --- | --- | --- | --- |
| R4W | 454 / 75 / 0 | 85628006 | 0 / 0 | yes | yes | no | `at=174 seat=0/0 msg=19/19 turn=28/28 options=2/2 first_diff=actions_[1]=0/1 options_same_set=1` |
| R4W | 51 / 19 / 0 | 85603003 | 9 / 0 | yes | yes | no | `at=251 seat=0/0 msg=11/11 turn=37/37 options=10/10 first_diff=actions_[24]=22/21 option[2]=g3|3|0|0|0|-1|0|0|0` |
| R4 | 38 / 14 / 0 | 85602006 | 0 / 0 | **no** | yes | yes | `canonical walk: at=189 seat=1/1 msg=10/16 turn=28/28 options=2/2 first_diff=cards_[902]=1/0 first_diff=global_` |
| R4W | 441 / 9 / 0 | 85627009 | 0 / 0 | yes | yes | no | `at=90 seat=0/0 msg=19/19 turn=10/10 options=2/2 first_diff=actions_[1]=0/1 options_same_set=1` |
| R4W | 441 / 9 / 1 | 85627009 | 0 / 0 | **no** | yes | yes | `canonical walk: at=219 seat=0/0 msg=12/12 turn=30/30 options=2/2 first_diff=cards_[698]=167/46 first_diff=acti` |
| R4W | 443 / 68 / 0 | 85627011 | 2 / 0 | yes | yes | no | `at=94 seat=0/0 msg=16/16 turn=17/17 options=4/4 first_diff=actions_[0]=38/36 option[0]=g17|8|0|0|0|10010|0|0|0` |

R4: 1 of 3,494. R4W: 5 of 5,440.

**The eight R4W byte-only pairs from `9380186`, with its one R4 byte-only pair and its one canonical pair.** On the 85400000 corpus, replayed on the final build for this diagnosis only, as those positions exist nowhere else: `replay-85400000.txt` in the results. Sol was right that not all were reorderings.

| Pair | Test | First difference | Cause |
| --- | --- | --- | --- |
| Game 53, index 42, deal 1 | R4 | Canonical chain only, decision 74: `msg` 12 against 10. Viewer's hand parted at decision 33, with no prompt | Load order, verified. An opposing random discard: ids sort 16, 18, 37 and 14, 16, 37, and place 1 is another card. Graveyard and 200 Life Points then differ |
| Game 47, index 87, deal 0 | R4W | `cards_[1108]`, a viewer's hand row, decision 46. Same options | Load order, verified. The same kind of discard: 44330098 in one, 5556499 in the other |
| Game 431, index 138, deal 1 | R4 | `actions_[0]`, 37 / 39, decision 11. `g11` before `g13` in one | Load order, verified. Two Graveyard effects in other id order |
| Game 431, index 138, deal 0 | R4W | `actions_[1]`, 0 / 1, position prompt, decision 18. Raw chain equal | Load order, verified. The prompt's row names another card, so its card id byte differs |
| Game 468, index 43, deal 0 | R4W | `actions_[0]`, 42 / 43, decision 60. `g12` / `g13` | Load order, verified |
| Game 32, index 66, deal 0 | R4W | `actions_[12]`, 28 / 29, decision 231. `g9` / `g10` | Load order, verified |
| Game 309, index 82, deals 0 and 1, two samples | R4W | `actions_[0]` 45 / 26 and 30 / 29 and 20 / 21, `actions_[24]` 17 / 18. `x3` / `h2`, `h2` / `h3` | **Not load order: one number in both worlds, and the test said so. A real bug:** the effect-id limit above. Fixed, and all four then match in every byte |

### Group order in the pool's scripts

From Sol's audit and a wider scan of 220 scripts for 223 codes, read by hand. Whether order changes a rule outcome:

| What the script does | Scripts | Reads order? | Checked |
| --- | --- | --- | --- |
| Random pick from a group | Thestalos, Power Tool Dragon, X-Saber Urbellum | Index into the group, uniform | Thestalos and Power Tool: tests above. **Urbellum: not tested.** On `g_frog_monarch` the only Tuner is Fishborg Blaster, whose `EFFECT_TUNER_MATERIAL_LIMIT` stops the Synchro on my scripted line (0 of 960 worlds reached it in a trial), so it needs a Tuner taken from the other player |
| One card out of a mandatory one-card selection or target | About 50 sites | No | By reading |
| `GetFirstMatchingCard` | Gadgets, Destiny HERO - Malicious, White Stone, Starlight Road | By zone and sequence, not number | By reading the core. Filters are one code |
| Sum or apply to all | Junk Warrior, Sirocco, Limiter Removal, Tempest Magician, Armor Master, Super Rejuvenation, `aux.sumreg`, Future Fusion | Yes, with the same result in any order | By reading |
| Summon a selected group one by one | Rescue Cat, Gyzarus, Secutor, Return from the Different Dimension | **Yes: which card is placed and prompted first** | Cat: Sol, 1,500 paired, 0 layouts differ. **Gyzarus, Secutor, Return: not run.** With a random or symmetric policy the order should not change the odds, but a policy that treats first and second prompts differently could read it |
| Ties, then the player chooses | Fissure, Smashing Ground | Option order only | By reading |
| `eg:GetFirst()` on a one-card event | Bottomless Trap Hole's Normal Summon path, Black Whirlwind | No | By reading. Its Special Summon path takes the whole group |
| **`eg:GetFirst()` on a group of up to two** | **Armory Arm**, when both monsters are destroyed by battle | **Yes: this decides whether the damage happens** | **Not run.** By reading, the two usually have different owners, whose number ranges do not depend on load order. If one player controls both, it does. It is the one site I found where order can decide a rule outcome outside a random pick |

Not classified: `procedure.lua`'s sites (Xyz and Shaddoll procedures, which I saw only in a scan), and order the core reads inside its own operations, such as simultaneous moves. R4 and R4W cover those only as far as play reached them.

### Renumbering: an estimate

Not built for search. A test-only switch numbers every card by its place at deal time, so the numbers are a function of public facts.

| Measured with the switch on | Result |
| --- | --- |
| R4, 1,638 dealt pairs: canonical / every byte | 1,638 / 1,638 |
| R4W, 2,796 dealt pairs: canonical / every byte | 2,796 / 2,796 |
| Pairs that fail any comparison | 0, against 6 without it on about twice as many positions |
| Thestalos, same seed, other result | 0 of 2,265 and 0 of 7,680 |
| R3, canonical chain against the natural duel with the same hidden cards | 708 of 720 |
| R2 playouts finishing, failures | 5,874 of 5,874, invariant 0, diagnostics 0, exceptions 0, crashed positions 0 |

**What it would touch.** The core reads `cardid` only in `card_sort`, and production's environment does not read it. In the forward model, `rebuild_card_sets` already rebuilds every `card_set` the core holds (22 in its core data, 4 on each card, all groups), and `order_effects_by_arrival` already keys on it. `PublicMemory` and `WorldBaseline` are indexed by it, so they would need the true number kept aside, as would the hard-rule check, whose counter means nothing with this switch on. I put it at about 40 lines in `fm_deal.h`, and the checker's exception list could then go, leaving strict equality.

**What it has not been shown to be:** safe. Three things stand open:

1. **A loop in progress.** A deal can land with a prompt open inside a Lua loop, between `GetFirst` and `GetNext`, as in a group Special Summon. Renumbering public cards reorders that group under the loop, which could skip a card or repeat one. Deals cannot do this today, since a group holding a hidden card refuses and groups of public cards never reorder. No test above looks for it, so zero failures do not rule it out. It would need the order kept in any group with a live iterator, or a refusal, which would be public.
2. **Fidelity.** 12 of 720 R3 chains part from the natural duel with the switch on, though no winner does. I did not look at why. Numbers by place are not a load order a natural duel could have: natural numbers keep each owner's cards in one range, and those ranges decide `eg:GetFirst()` on Armory Arm.
3. **Chance.** A first-card read would become a fixed function of public facts, where a natural duel varies. Numbering by a seeded shuffle of places, keeping owner ranges, would keep that chance. That is what I would build, and it is not what this switch measures.

**Recommendation: do it, but as its own reviewed commit after this merges, not now.** It is small, and it closes the one thing the contract cannot promise: Thestalos's same-seed differences go to 0, and all 4,434 pairs match in every byte. A search that treats seeds as samples does not need it to be sound, so it blocks nothing. Open item 1 is the reason not to slip it in here.

### Failure inside a sampled world

Each script plant fires only while Heavy Storm is in hand during a Summon, and the chosen game never does that on its recorded line (`g_diva_frog/g_hero_beat seed=85632000 env=0`, decision 61). So the failure cannot occur in the true duel, only in sampled worlds that deal it to the opposing hand. Three runs in fresh processes, 512 seeds each: all seeds in order, then only the clean seeds, then only the voided seeds.

| Plant | Voided as | Voids | Finished | Clean-run digest differences | Reproduced alone | True game finishes after |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `error()` in Lua | `kLuaError` | 17 | 495 | 0 of 495 | 17 of 17 | yes |
| A prompt production refuses (opposing Deck) | `kEnvironmentThrow` | 17 | 495 | 0 of 495 | 17 of 17 | yes |
| Endless loop in Lua | `kHang` | 17 | 495 | 0 of 495 | 17 of 17 | yes |
| Runaway recursion in Lua | `kLuaError` | 17 | 495 | 0 of 495 | 17 of 17 | yes |
| Invalid write inside the guard | `kFatalSignal` | 32 | 480 | 0 of 480 | 32 of 32 | yes |
| `abort()` inside the guard | `kFatalSignal` | 32 | 480 | 0 of 480 | 32 of 32 | yes |
| Production's `Spec not found` line | `kNativeDiagnostic` | 32 | 480 | 0 of 480 | 32 of 32 | yes |
| That line, then a flush (finding 6) | `kNativeDiagnostic` | 32 | 480 | 0 of 480 | 32 of 32 | yes |
| That line, then a throw (finding 6) | `kEnvironmentThrow` | 32 | 480 | 0 of 480 | 32 of 32 | yes |
| Endless loop with the arena off (finding 5) | `kHang` | 32 | 480 | 0 of 480 | 32 of 32 | yes |
| **Control:** history rollback missed, Lua plant | `kLuaError` | 17 | 495 | **495 of 495** | | |

Only `kTerminal` counts as finished now (finding 7), and 0 worlds ended any other way. The checker fails if any marker from a failed world reaches stdout. Segfault, abort and the markers are planted through a test hook, as no real crash is at hand. The hang limit is CPU time, 2 s by default, 1 s here.

**Why this works:** every byte a duel owns is in the arena, so any way out of a core call leaves nothing a restore cannot put back. Stdout is our own stream (`pin_stdio`), which counts and drops what a failed world printed, flushed or not. The signal handler may jump from anywhere except inside glibc's allocator, whose lock it would leave held; it waits that out.

**Finding 5, the rest of it.** Installing this takes process-wide handlers for six signals, an alternate stack and a periodic `ITIMER_VIRTUAL`, and closing a world does not restore them. A runtime with its own needs for those must not share the process. A fatal signal outside a guard prints a report that is not async-signal-safe, then dies, which is its job. I did not test a real inference runtime in the same process.

### Search API

`fm_world.h` includes no core or environment type. `fm_search_example.cpp` includes only it. Its 20 contract checks pass, and 8 attempts to name a core type, an environment type, the internals or the test hooks fail to compile, against a control that compiles.

**No signature changed in these rounds, but `deal`'s contract did:** it no longer promises one world per seed across true worlds. The usage sketch now checks `truncated()`. For the merge with `m2-search`: `apply_assignment` gained a parameter, and `fm_deal.h`, `fm_hidden.h`, `fm_env.h`, `arena.h` and `fm_world.cpp` changed inside.

| Need | Call |
| --- | --- |
| Build from a seeded duel | `Referee(deck0, deck1, seed_plus_env)`, `play`, `observe` (acting seat only) |
| Hand a position to search | `Referee::open_search(viewer)`: null unless the viewer is acting |
| Snapshot, restore | `SearchWorld::snapshot`, `restore`, `release`, `kRoot` |
| Deal | `deal(seed, DealAssumption)`, returning `DealResult` with refusal and counts |
| Raw observation for the acting seat | `observe(&Observation)` |
| Step an option | `step(option)` |
| Terminal, winner | `terminal`, `winner`, `win_reason`, `truncated`. A truncated world has no winner and is not a result |
| Void | `voided`, `void_reason`, `last_diagnostic`, `stats` |

A `SearchWorld` has no way back to its `Referee`, and the referee refuses to play while a world is open. A world starts undealt, and then refuses `step`, reports no result, and shows only the viewer's own decision. There is no call to read a card, a code or a hidden zone. This guards against accident, and it is C++: a determined caller could still `reinterpret_cast`.

One worker per process. A world is not thread safe. Sampled opposing observations come from the sampled world with scrubbed history, so they are not what production would show that seat in a true duel.

### The suite's verdict

`run_all.sh` returns the exit code of `check_results.py`. It fails on any nonzero mismatch, diagnostic or invariant counter, on a planted control not caught, on a planted failure that did not void as planned, on unequal digests across heaps and processes, on a missing result, and on a marker reaching stdout. `test_checker.py` breaks real results one fault at a time and requires a failure: 65 of 65 caught, Sol's six mutations among them, and the unbroken results pass.

**Since the second review it also checks:** every continuation observation counter, winners, and how counters relate. Samples equal accepted plus rejected. Pairs equal twice accepted. A later comparison cannot exceed an earlier one. Coverage has floors, so a test cannot pass by testing less.

**Allowed exceptions, and how each is verified:**

| Exception | Verified how | Limit |
| --- | --- | --- |
| A pair differing in chain, bytes or winner | **By a program, pair by pair**: listed, slots equal, one numbering makes it the same. Each counter must equal dealt minus the listed pairs that fail it, so one more failure anywhere fails, however small | 0.2% of dealt pairs |
| A one-sided refusal in the hand-trap pairs | **Kind only**: it must be `pending chain list`. That a hidden optional trigger caused it is not checked by a program | 1 of 3,996, as measured. Limit 2 |
| R3 exact order against a natural duel | **Not an exception, not gated** beyond not exceeding its base: a sampled world deliberately does not copy a natural duel's arbitrary order | None |

One-sided refusals in R4 and R4W are now required to be 0, as they are. Hand-trap exact and canonical differences are required to be 0, as they are.

### Throughput

Through the search API, on a shuffled mix of real roots, 15 s per cell, Ryzen 7 9800X3D under WSL2. **Another agent's search pilot was running, so the machine was loaded. These are lower than an idle machine would give. Treat absolutes as ±30% at best.** One-minute load average at each cell's start is in the table.

| Workers | Nodes/s per worker | Nodes/s total | Node, µs | Restore | Deal | Step | Observe | Load |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1,991 | 1,991 | 502 | 77 | 162 | 70 | 192 | 9.05 |
| 2 | 1,971 | 3,942 | 507 | 76 | 165 | 64 | 201 | 9.04 |
| 4 | 1,858 | 7,432 | 538 | 81 | 172 | 66 | 218 | 9.31 |

A node is restore + deal + one step + encode. The deal's cost rose in this round: it orders every effect in the duel and counts legal assignments. Full random playouts with a deal and the encoder: 30 / 27 / 26 per worker on 1 / 2 / 4 workers, about 4,200 decisions/s each on 4. The older playout bench, no deal, on these lists: 130 / 126 / 115 playouts/s per worker without the encoder, 29 / 28 / 22 with it.

### Still open

- **The known-list assumption.** A real opponent's list is not known. R5 shows cards outside the list work mechanically, and the API does not offer it. A wrong list is refused, so a caller guessing a list learns one bit per call.
- **One seed, one future.** Not across load orders. See [What holds](#what-holds-across-true-worlds) and [Renumbering](#renumbering-an-estimate).
- **The refusal bit.** See Refusals. Measured at 1 of 3,996 in the hand-trap pairs and 0 of 5,540 in R4W. Refusal parity in R4W now includes worlds the first world refuses.
- **2.1% of positions refuse every deal,** mostly a prompt or chain that holds a hidden card, such as a target among set cards. Following the slot instead of the object may recover many of these. I did not try it.
- **Behaviour.** Deals respect public facts only. They ignore what an opponent's passes imply, and that a card just shuffled in is in the Deck.
- **Scripts holding hidden cards.** A label object on another card's effect refuses the deal. Label groups are ignored, as Summon procedures leave them stale. References in Lua tables and the stale `operated_set` are not detected. None showed in R3, R4 or R4W.
- **Reveals.** The tap reads a batch's first message, which by source reading is where those two messages always land. Not proven. A Deck slot is pinned only while its reveal key lives, which production clears on any move.
- **Hard rules are for these lists.** Other lists may need more, and `check_set_rules.py` will say so.

### Not verified

- **Refusal paths never reached on these lists:** face-down banished (0 such cards at 2,016 roots), foreign owner, reversed Deck, prompt binding, no valid assignment. `kListMismatch` is exercised only by the API check. `kOptionOverflow` has no test.
- **Pairs:** 42 of 420 ordered list pairings. Each list is in 4 cells.
- **Playouts** are uniform random in sampled worlds. The heuristic and the network were not run inside one.
- **Sampled-world rule legality past the root.** The rules are checked at the deal. Later moves are the core's own.
- **glibc reliance.** Recovery from `abort()` relies on glibc's recursive abort lock.
- **A fatal signal inside a glibc call** other than the allocator would jump with that call's state as it stood. The planted faults do not cover arbitrary interrupted libc calls.
- **More than 4 workers, native Linux, and a real inference runtime in the same process.**
- **Not rerun on this build:** `run_costs.sh` and `run_allocator_compare.sh` (fork, replay and memory tables). Candidate D was rerun: 379 of 400 reached idle, board 379, exact action set 29 (7.7%), 156 (41.2%) ignoring phase actions, 812 wrongly offered and 2 lost. It still fails.

### Reproduce

Link the git-ignored assets, then run everything. About 1 hour 45 minutes on 4 threads on a loaded machine, and the build does not use xmake. The exit code is the verdict:

~~~bash
W=/path/to/worktree
ln -s /home/mada/ygopro-scripts "$W/scripts/script"
cp -p /home/mada/ygo-agent/ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so "$W/ygoenv/ygoenv/ygopro/"
mkdir -p "$W/assets/locale/en"
ln -s /home/mada/ygo-agent/assets/locale/en/cards.cdb "$W/assets/locale/en/cards.cdb"
bash "$W/tools/edison/forward_model/run_all.sh"
~~~

Output goes to `build/forward_model/`. Logs name each refusal, difference and byte. Results of this run are in [results-2026-11-05](../tools/edison/forward_model/results-2026-11-05/).

### Files

Under `tools/edison/forward_model/`:

| File | Purpose |
| --- | --- |
| `fm_world.h`, `fm_world.cpp` | The search API, and the guard around every core call |
| `fm_search_example.cpp`, `run_api.sh` | API contract checks and compile refusals |
| `fm_deal.h` | Determinizer, public memory, hard rules, re-binding, the planted faults |
| `fm_hidden.h` | R1-R5, R4W, hand-trap pairs, the rules scan, uniformity, load order, candidate D |
| `fm_env.h` | Production subclass, stamp, hashes, sha256, message tap, captured stdout |
| `fm_bench.cpp` | Commands: `verify`, `correct`, `redeal`, `uniform`, `rules`, `setfixture`, `handtrap`, `loadorder`, `void`, `node`, `statics`, `deepdiff`, `construct`, and the older `replay`, `fork`, `forkmem`, `arena`, `throughput`, `fork-conc` |
| `arena.h` | Allocator and snapshots |
| `collect_corpus.py` | Production games and digests |
| `check_results.py`, `test_checker.py` | The suite's verdict, and proof it can fail |
| `check_statics.py`, `check_set_rules.py` | Static data, and the facts the Set rules rest on |
| `build.sh`, `test_build_refusal.sh` | The pinned build, and proof it refuses others |
| `run_all.sh`, `run_verify.sh`, `run_correct.sh`, `run_controls.sh`, `run_loadorder.sh`, `run_void.sh`, `run_throughput.sh` | Reproduction |

---

# Spike on the old core

**Everything below this line was measured on November 4, 2026 at commit 4e3452c, on the pre-hardening core `bedb42dd...` and 3 fixture decks.** It is kept for its decision and its record. Its numbers do not describe the current build unless the section above repeats them. Its reproduce steps need that commit, as `collect_games.py` and `check_observations.py` are gone.

This decides how M2 search copies and returns to positions. It does not build search.

## Decision

**Use an in-process arena snapshot (candidate E, added by this spike), one worker
process per thread.** All duel memory, core and Lua alike, is allocated from one
fixed-address mapping, so a snapshot is a memcpy of about 1.9 MB. It needs no core or
Lua patch and no knowledge of the core's structures.

| | Approach | Node cost, p50 | Playouts/s per worker | Correct? | Verdict |
| --- | --- | ---: | ---: | --- | --- |
| E | Arena snapshot | 102–128 µs | 319–348 | 0 mismatches | **Recommended** |
| B | `fork()` | 631–696 µs | 228–245 | 0 mismatches | Fallback, and only from a small process |
| A | Replay from seed | 6.0–7.8 ms | 121–131 | 0 mismatches | Use to build roots, not nodes |
| C | Structural clone | not built | | | Ruled out: harder, and E gives the same result |
| D | Construct from description | 4–6 ms (estimated; not timed) | | **Fails** | Ruled out as a forward model |

Three findings matter beyond that table:

1. **The observation encoder, not the engine, is the bottleneck.** `WriteState` takes
   about 150 µs per decision against 10–17 µs for the engine step. It cuts arena
   throughput by a factor of 5.7 before any network runs.
2. **Hidden state can be re-dealt in place.** A deal takes 39 µs, ran 1078 random
   playouts to completion with no failures, and matched natural duels on all the
   checks below except one unexplained divergence in 119. Small leak channels remain.
3. **Nothing here loads a position from a real external duel.** Replay needs the true
   seed and the opponent's private responses, which only exist in our own simulated
   games. Construction fails. M6 needs its own solution.

**Supervisor rerun.** The code was copied to the main tree and rebuilt with `collect_games.py`,
`build.sh`, `run_verify.sh` and `run_correct.sh`. All correctness counts reproduced: 330 branch
points, 1038 options and 171,524 reference decisions with 0 mismatches, plus the same two logged
differences (`game=57 index=11` and `game=28 index=150`). In that output R3's canonical match is
117 of 119, and 118 of 119 when compared by `cardid`. The table below reports the latter.
Throughput was not rerun.

## Setup

- **Games:** 192 production heuristic-vs-heuristic duels, 32 for each of 6 cross-deck
  pairings of the legal fixture decks. Seeds 70000000 + cell × 1000 + env. Mean 163
  decisions per game, range 46–468. Mean 3.14 offered options per decision; 55.5% of
  decisions offer at most 2 and 1.6% offer at least 10.
- **Positions:** one per game at each stage, 10% (early), 50% (mid) and 90% (late) of
  game length, ±5%. Mean decision index 16, 81 and 146.
- **Test bed:** a native binary that subclasses the unmodified `YGOProEnvImpl` and links
  the production core package (`edison_early_trigger = true`). It reproduces production
  (see Correctness).
- **Machine:** Ryzen 7 9800X3D, **8 physical cores, 16 threads**, under WSL2. Other
  agents were running (load average 4–8), and the same replay measured 60–98k
  decisions/s across repeats. **Treat absolute numbers as ±30%.** Runs used at most 4
  workers.
- Playouts use a uniform random policy unless stated otherwise.

## Results

Latencies are p50 / p90 / p99 in µs, all from one worker.

| Operation | Early | Mid | Late |
| --- | ---: | ---: | ---: |
| **A. Replay** | | | |
| Create duel, load scripts, first prompt | 5707 / 7639 / 12823 | same | same |
| Replay the action prefix | 199 / 450 / 1293 | 1057 / 2236 / 3645 | 1871 / 3829 / 6570 |
| Node total (create + prefix + 1 step + destroy) | 6017 / 7992 / 13311 | 6921 / 9416 / 14913 | 7826 / 11016 / 15936 |
| **B. Fork** (parent RSS 10 MB) | | | |
| Bare fork + exit + wait | 346 / 455 / 718 | 347 / 456 / 771 | 368 / 510 / 781 |
| Node (fork + step + pipe + wait) | 631 / 840 / 1097 | 661 / 908 / 1162 | 696 / 923 / 1142 |
| **E. Arena** | | | |
| Take full snapshot | 62 / 91 / 857 | 66 / 99 / 972 | 67 / 101 / 1025 |
| Restore full snapshot | 67 / 126 / 184 | 76 / 153 / 201 | 80 / 160 / 250 |
| Node (restore + step) | 102 / 177 / 262 | 114 / 240 / 301 | 128 / 248 / 485 |
| Node, write-tracked restore | 343 / 589 / 729 | 481 / 910 / 1319 | 571 / 998 / 1451 |

| Mean cost per playout, one position | Early | Mid | Late |
| --- | ---: | ---: | ---: |
| Random playout length, decisions | 237 | 167 | 74 |
| Arena, including restore | 5.2 ms | 4.2 ms | 2.2 ms |
| Fork, including fork, pipe and wait | 8.3 ms | 7.0 ms | 4.1 ms |

Throughput over a shuffled mix of all stages, 16 playouts per root, including building
each root by replay (5–6 ms; 13 ms with the encoder). Each cell ran for 15 seconds, and
every method averaged about 160 decisions per playout. Load average was 1.6–3.5.

| Method | Workers | Playouts/s, total | Playouts/s per worker | Decisions/s, total |
| --- | ---: | ---: | ---: | ---: |
| Arena | 1 / 2 / 4 | 348 / 652 / 1275 | 348 / 326 / 319 | 55.9k / 105.6k / 206.7k |
| Arena with production encoder | 1 / 2 / 4 | 60 / 112 / 215 | 60 / 56 / 54 | 9.8k / 18.5k / 35.3k |
| Fork, separate parents | 1 / 2 / 4 | 245 / 477 / 912 | 245 / 239 / 228 | 39.1k / 76.2k / 145.7k |
| Fork, 1 parent, N children | 1 / 2 / 4 | 235 / 427 / 674 | 235 / 214 / 169 | 37.6k / 68.0k / 108.4k |
| Replay every playout | 1 / 2 / 4 | 131 / 248 / 485 | 131 / 124 / 121 | 21.0k / 40.1k / 78.0k |

So one arena worker simulates about 52–56k decisions/s and 319–348 full random playouts/s.
With the encoder that falls to about 9–10k decisions/s and 54–60 playouts/s. Fork loses
more with one parent than with separate parents, so each fork worker should own its parent.

An earlier throughput run is **discarded**. It walked positions in stage order, so short
runs stayed inside one stage (99–238 decisions per playout), and it ran under heavier load.
It gave arena 303 / 291 / 223 per worker. Its output is still in `costs.jsonl` if
that file has not been regenerated.

| Memory per snapshot | Early | Mid | Late |
| --- | ---: | ---: | ---: |
| Arena, full copy | 1.8 MB | 1.9 MB | 1.9 MB (max 2.2 MB) |
| Arena, pages dirtied by one step, mean | 40 (160 kB) | 43 (171 kB) | 51 (204 kB) |
| Arena, pages dirtied by a playout, mean | 400 of about 450 | 427 of about 480 | 425 of about 480 |
| Fork, private dirty after one step, mean | 235 kB | 272 kB | 315 kB |
| Fork, private dirty after a playout, mean | 1973 kB | 2010 kB | 1955 kB |
| Fork, page tables per live child | 68 kB | 68 kB | 68 kB |

For 200 stopped children, private dirty was 91, 145 and 314 kB per child by stage.
`MemAvailable` deltas were dominated by other agents and are not usable.

### Findings per candidate

**A. Replay is correct, and duel creation dominates its cost.** About 6 ms of the node
is creating the duel (Lua state, scripts, cards), and the prefix adds about 17 µs per
decision. It stays the simplest way to build a root, and restoring a snapshot taken
after creation would remove that 6 ms (derived, not measured).

**B. Fork is correct, but its cost grows with the parent's memory.** A bare fork took
0.35 ms at 10 MB RSS, 5.7 ms at 215 MB and 35–42 ms at 1 GB. It cannot fork from a
Python or JAX process and would need a small dedicated server. Fork, copy-on-write faults
and the pipe add about 2–3 ms per playout over the arena. Its advantages are that it is trivially
correct and it isolates crashes.

**E. Arena restore is cheap because the state is small.** A full copy is 67–80 µs. It
works because every pointer in the snapshot stays valid at the fixed address, and the
allocator's own state rolls back too, so later allocations land at identical addresses.
Constraints:

- One active duel per process, since the address is fixed. Hence one process per thread.
- Snapshot only between `process()` calls, when no Lua call is on the C stack. Every
  decision point qualifies.
- Malloc interposition suits a dedicated executable. It cannot go into the production
  Python module without `LD_PRELOAD` or allocator hooks in the core and Lua.

**Write-tracked restore failed and is rejected.** Tracking dirty pages with `mprotect`
and `SIGSEGV` was 3–4× *slower* than copying everything, because one step dirties 40–51
pages and each fault costs several µs here. It was correct, just slow, and a playout
dirties nearly every page anyway. Delta snapshots could still save memory for shallow
tree nodes (about 160–200 kB against 1.9 MB). That is untested.

**C. Structural clone: ruled out by source reading, not prototyped.** It would need:

- a Lua state clone, which Lua 5.5 does not offer (`lua_dump` covers only functions,
  without upvalues), including suspended coroutines, registry function references and
  userdata;
- deep copies with pointer fixups for cards, effects and groups, including `multimap`
  iterators stored in `effect_indexer`;
- type knowledge for the untyped `void*` fields `processor_unit::ptr1`–`ptr4`, with 61
  uses across `processor.cpp` and `operations.cpp` and 114 `add_process` sites in
  `processor.cpp`.

A missed field would diverge silently. My judgment is weeks of work plus permanent
maintenance, for the same state E copies in 70 µs. Its only advantage is several live
duels in one address space, which sequential per-worker search does not need.

**D. Construction fails as a forward model.** I rebuilt 400 sampled real idle prompts
through `new_card` and `set_player_info`, as the fixtures do. Optional chain windows
were declined on the way to the prompt, and seats were swapped so the acting player has
the first turn.

| Of 374 positions that reached an idle prompt | Count |
| --- | ---: |
| Board identical | 374 (100%) |
| All monster ATK identical | 365 (98%) |
| Offered actions exactly equal | 30 (8%) |
| Equal, ignoring phase actions | 171 (46%) |
| Same, in the easy subset: no summon used, no equip/target/counter relations | 157 of 186 (84%) |

The other 26 of the 400 sampled never reached an idle prompt.

The missing state is what the API cannot express. The rebuilt positions wrongly offered
712 actions: 228 repositions, 225 summons, 225 monster sets and 34 activations. They
lost 6 real activations. Beyond that:

- **Can inject:** cards, location, sequence, position, LP.
- **Cannot inject through the API:** turn number and phase (Turn 1 has no Battle Phase),
  normal-summon and reposition use, set-turn status, once-per-turn counters, equip and
  target relations, ongoing effects with targets, a chain in progress, and any non-idle
  prompt.

Writing these fields directly is possible but amounts to rebuilding C, and this does not
test it.

## Correctness

The check runs **every option** at each branch point. Each is continued by the same
seeded random policy to the end of the game, then compared with a straight run in a
fresh process with no snapshots. The comparison uses a hash chain over each decision
plus a privileged deep hash: all cards with full query data, deck order, future RNG
output and processor counters.

| Check | Result |
| --- | --- |
| Branch points / options / reference decisions | 330 / 1038 / 171,524 |
| Playouts truncated by the 3000-decision cap | 0 |
| Fork vs straight run | **0 mismatches** |
| Arena, full restore vs straight run | **0 mismatches** |
| Arena, options in reverse order | **0 mismatches** |
| Arena, write-tracked restore | **0 mismatches** |
| Arena, different garbage above allocator top (uninitialised-read probe) | **0 mismatches** |
| Return to position, finish real game as recorded, same result | **0 mismatches** |
| Replay repeated | **0 mismatches** |
| Replay in a deliberately scrambled heap | **0 mismatches** |
| Core/Lua diagnostics | 0 |

**The test bed is the production environment:**

- All 192 games reproduce the production trace's acting player, option count, winner and
  win reason at every decision (31,275 decisions).
- Native observation bytes match the production trace's sha256 on 5520 of 5520 decisions.
  This covers only the 32 games of cell 0.
- Whole-game digests are identical across separate processes, a scrambled heap, the
  arena allocator, and encoder on or off. Deep hashing does not change decisions.

**A caveat for replay only.** The core has pointer-keyed ordered and unordered containers
(`quick_f_chain`, `delayed_quick`, `reseted_effects`, `pheff`), and six script files use
`pairs()`, whose order can depend on addresses or Lua's per-state seed. None of this
caused a divergence on these decks, but other cards might. Snapshots preserve addresses
and are immune.

## Hidden state

Search must never read true hidden state, so a sampled world must depend only on public
inputs and a seed. The prototype determinizer (`fm_hidden.h`) re-deals the viewer's deck
order and the opponent's deck, hidden hand and set cards. It then reseeds the core RNG,
because the true stream encodes future shuffles and coin flips.

The core mechanism is a **slot permutation**. A slot keeps its rule state (how it got
there, set-turn status, field ids, position). Only the card object occupying it changes,
with effects cancelled and re-applied around the move. Monster and spell/trap set zones
keep their public type.

| Test | Result |
| --- | --- |
| R1. Reorder both decks only: playouts completed | 1080 of 1080 |
| R2. Full re-deal: deals that succeeded | 1078 of 1080; 2 could not satisfy set-zone types in 500 tries |
| R2. Playouts completed | 1078 of 1078 |
| R1 and R2. Invariant failures, diagnostics, exceptions, crashes | 0 |
| R1 and R2. Same seed reproduces the same playout | all |
| R2. Cost per deal | 39 µs |
| R3. Re-deal vs natural duel, same assignment: board | 119 of 119 |
| R3. Forward decision chain, canonical prompt order | 118 of 119 |
| R3. Winner | 119 of 119 |
| R3. Exact prompt order | 101 of 119 |
| R4. Two true worlds, same public history, same deal seed: hidden slots equal | 168 of 168 |
| R4. Forward chain equal, canonical order | 167 of 168 |
| R4. Forward chain equal, exact prompt order | 157 of 168 |
| R4. Control, no re-deal: forward chains equal | 2 of 168 |
| R5. Cards outside the true decklist: playouts completed | 1080 of 1080, 0 failures, 85 µs per deal |
| R5. Materialised cards that moved during play | 2011 of 3240 (62%) |

**R3 is the oracle.** It builds the same assignment as a natural duel by rigging the
initial deck and replaying history. The re-deal matches it, including on rule state the
slot swap had to reconstruct. One sample diverges at decision 292 of 315 with the same
length and winner (game 57, index 11). **That divergence is unexplained.**

**R4 tests for leaks directly.** It found one: my first determinizer pinned cards listed
in `core.select_cards`, which stays populated after a prompt ends, so the deal depended on
the opponent's earlier private candidates. Slot equality was 100/108 before that fix and
is now 168/168. Three residuals remain:

- **Prompt order:** in 11 of 168 pairs the exact order differs. List order follows
  `cardid` and similar internal ordering, so a policy reading list position could pick up
  true-world information. Sort prompts canonically at the search boundary.
- **One canonical difference** (game 28, index 150), in a pair whose physical layouts had
  already diverged. Not diagnosed.
- **Pinning reads hidden state:** a hidden object carrying rule state (relations,
  counters, extra effects) is pinned, and that check reads it. It happened 6 times in
  68,948 moves. Production code should fail closed here rather than pin.

**Alternate-history replay is not a usable sampler.** Even with rigged decks, acceptance
was 168 of 540 (31%): early 47%, mid 25%, late 21%. Replaying raw indices survived 103 of
540 (19%). Each accepted sample costs about 8 ms. It also has two deeper problems:

- It uses the opponent's private responses, which a real engine does not have, so these
  rates are upper bounds.
- 338 of 372 rejections came from opponent-side or sequence prompt differences, which are
  not public contradictions. These rates therefore measure the limits of this replay, not
  how many deals are impossible.

A legal version must be driven by public events. That was not built.

### What re-dealing cannot do yet

- **Decklist knowledge.** R2 deals the true multiset, which is fair only for these known
  fixtures. An unknown opponent list needs materialisation. R5 shows that works
  mechanically, but script `global_check` trackers would start mid-duel without history,
  and the replaced object stays allocated at location 0.
- **Consistency with the opponent's behaviour.** Deals respect public facts only. They do
  not reason that an opponent who passed probably lacks a response.
- **Reveal memory.** Public tracking scans at decisions and uses production's known-imperfect
  reveal memory. A card public only between decisions could be missed.
- **Lua references.** Scripts holding hidden cards in `LabelObject` or Lua tables are not
  detected. Moved cards in a `card_set` could also break its ordering when `cardid` is
  transferred. Neither surfaced on these decks.
- **Not re-dealt:** the opponent's Extra Deck, and face-down banished cards.

## How many playouts per decision

**The 16-thread figures are extrapolated.** Only 1, 2 and 4 workers were measured, and the
machine has 8 physical cores. The range runs from 8 × to 16 × the 4-worker rate, assuming
SMT adds between nothing and a full core. Network inference is excluded, as requested.

| Budget | Random playouts, no encoder | Playouts with production encoder |
| ---: | ---: | ---: |
| Rate | 2.6k–5.1k per second | 430–860 per second |
| 1 s | 2.6k–5.1k | 430–860 |
| 10 s | 26k–51k | 4.3k–8.6k |
| 60 s | 150k–310k | 26k–52k |

Spread over the mean 3.14 options, the sampling error of a win rate near 50% (0.5/√n) is:

| Budget | No encoder | With encoder |
| ---: | ---: | ---: |
| 1 s | 1.2–1.8 pp | 3–4 pp |
| 10 s | 0.4–0.6 pp | 1.0–1.4 pp |
| 60 s | about 0.2 pp | 0.4–0.6 pp |

**Is that enough?** Statistically yes for root ranking: about 10 seconds resolves
differences of a few points even with the encoder, and pairing worlds across options
should help further. It is not enough for a deep tree at 1 second. **Whether the estimates
mean anything is untested:** uniform random playouts may be a poor evaluator of Edison
positions, and this spike measured cost, not playing strength.

Truncated rollouts look like the better use of the budget. Restore + step + encode is
about 280 µs, or roughly 28k–57k encoded nodes per second across the machine (derived).
Speeding up the encoder is the highest-value engineering after the transport.

## Search recommendation

**Start with PIMC, with a fresh deal per iteration, then add a learned belief. Defer full
ISMCTS.**

- **PIMC first.** A deal plus restore is about 120 µs against 2–5 ms per random playout, so
  per-iteration sampling is affordable and needs no world reuse. It is the simplest thing
  that can be tested against the M2 exit criterion.
- **Keep the path to ISMCTS open.** Single-observer ISMCTS uses this same transport and
  deal step, so nothing here locks it out. Its extra machinery is information-set keying,
  which is hard with 3.14 mean options but about 160 decisions per game and many multi-step
  prompts. I would add it only if PIMC's known weaknesses appear in measured losses, namely
  strategy fusion and undervaluing information-gathering or bluffing. This is reasoning from
  the literature, not a measurement here.
- **Learned belief next.** Uniform deals ignore what the opponent's actions imply, and the
  alternate-history result shows that filtering by replay is too costly and illegal as
  built. A belief model over the public stream, used to weight or generate deals, is the
  practical route. It needs the M1 network, so it comes later.

## Not verified

- Scaling beyond 4 workers; contention on the machine could be better or worse than the
  range above.
- Any deck other than the 3 fixtures (67 card identities). The correctness and hidden-state
  results apply to these only.
- Playouts driven by the heuristic or a network. The heuristic is Python and was not run in
  playouts; the encoder was, the network was not.
- Multi-level tree search with nested snapshots, and moving a snapshot between processes.
  That needs identical code addresses, so workers must fork from one parent.
- Whether the sampled deal distribution is uniform. Rejection on set-zone types may bias it.
- Full certification was not rerun, because no runtime source changed.
- Native Linux. Fork and page-fault costs here are WSL2's and may differ.

## Reproduce

Set up the git-ignored files in a checkout of 4e3452c, reusing the main checkout's assets:

~~~bash
W=/path/to/worktree
ln -s /home/mada/ygopro-scripts "$W/scripts/script"
cp -p /home/mada/ygo-agent/ygoenv/ygoenv/ygopro/ygopro_ygoenv*.so "$W/ygoenv/ygoenv/ygopro/"
mkdir -p "$W/assets/locale/en"
ln -s /home/mada/ygo-agent/assets/locale/en/cards.cdb "$W/assets/locale/en/cards.cdb"
~~~

Then run, in order. Output goes to `build/forward_model/`, and the build does not use xmake:

~~~bash
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 /home/mada/ygo-agent/.venv/bin/python \
  "$W/tools/edison/forward_model/collect_games.py"     # 192 games, about 10 s
bash "$W/tools/edison/forward_model/build.sh"          # native test bed, g++ directly
bash "$W/tools/edison/forward_model/run_verify.sh"     # trace and digest checks
bash "$W/tools/edison/forward_model/run_costs.sh"      # latency and memory; about 2 min
bash "$W/tools/edison/forward_model/run_throughput.sh" # throughput; about 4 min
bash "$W/tools/edison/forward_model/run_correct.sh"    # correctness, R1-R5, construction
bash "$W/tools/edison/forward_model/run_allocator_compare.sh"  # replay noise check
~~~

Results: `costs.jsonl`, `costs-encode.jsonl`, `throughput.jsonl`, `correct.jsonl`,
`results.jsonl`.
`redeal.stderr` lists any divergence, and it holds the two lines cited above.

## Files

Under `tools/edison/forward_model/`:

| File | Purpose |
| --- | --- |
| `arena.h` | Fixed-address allocator, malloc interposer, snapshots, write tracking |
| `fm_env.h` | Production environment subclass, hashes, game loading |
| `fm_bench.cpp` | Commands: `verify`, `obsdump`, `replay`, `fork`, `forkmem`, `arena`, `throughput`, `fork-conc`, `correct` |
| `fm_hidden.h` | Determinizer, tests R1–R5 (`redeal`), candidate D (`construct`) |
| `collect_games.py` | Runs production duels, exports traces |
| `check_observations.py` | Compares native observations with production sha256 |
| `build.sh`, `run_*.sh` | Build and reproduction scripts |

No game rule, production source, core package or lock changed. The test bed reaches
production's private helpers with `#define private protected`, confined to `fm_env.h`, and
the malloc interposer exists only in the `fm_bench` executable. Production's `step()` reads
`msg_` values that `WriteState` stamps onto legal actions, so code that steps without
encoding must stamp them first, as `FmEnv::act` does.
