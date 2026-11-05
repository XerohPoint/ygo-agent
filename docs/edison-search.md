# Edison search

Determinized search for the play engine: for each decision, deal the hidden cards many ways, try the
network's best few options on the same deals, and leave the network's choice only if an option does
measurably better. Code is in `tools/edison/search/`.

**Status:** pilot results only, and they choose settings. The M2 test's plan is in
[`edison-m2-protocol.md`](edison-m2-protocol.md). See [What this does not show](#what-this-does-not-show).

## How one decision works

`pimc::search` in `pimc.h` includes only the search API (`fm_world.h`) and the network (`edison_net.h`), so
it sees no core or environment type.

1. Take the options the network may play, best logits first, up to `max_candidates`.
2. For each deal: restore the root, deal from a seed, play one option, then roll out.
3. A rollout plays the network greedily for both seats for `depth` network decisions, then asks the value
   head. One-option steps are free, including at the horizon. A rollout has its own repeat guard, so it can't
   spin on an option that changes nothing. A finished duel scores +1, -1 or 0. A truncated or voided world
   gives no value and is skipped.
4. Every option compared sees the same deals, so comparisons are paired. Sequential halving cuts the
   candidates, and the network's own choice always survives.
5. Play the best finalist only if its paired gain over the network's choice exceeds `margin_z` paired
   errors. Otherwise play the network's choice.

If the first 8 deals are refused, or fewer than 2 pairs finish, the network's choice stands. The repeat guard
matches `actor.py`, so the search and the plain network forbid the same options.

A deal's seed comes from the job's search seed, how many decisions that seat has made, and the attempt
number. The duel's whole decision count is not used, since it counts the opponent's private prompts.

## Commands

```bash
python3 tools/edison/search/rank_position.py --deck0 frog_monarch --deck1 blackwing --seed 86000000 --moves "" --budget 1024 --depth 48
python3 tools/edison/search/rank_position.py --game build/edison-search/pilot2-b64d48k4z1/g12.json --decision 40
```

`rank_position.py` lists every option with an estimated win rate and an error. Run it from `scripts/`.

`edison_search play` plays games from a job file, one forked process per game. `search_study.py` runs the
pilots and the different-network study, `test_search.py` runs the checks below, and `m2_final.py` is the M2 test.

Run games through the Python scripts. They refuse to resume a directory if the jobs, binary, nets, Lua scripts,
database, lists, code list or arguments changed. Run directly, the binary checks only the job and its own build.

## Assumptions

- **The opposing Main Deck list is known.** A wrong list is refused or searched as declared, never corrected
  from hidden state. The Extra Deck is not re-dealt, under the same assumption.
- **Later play is the network's.** A win-rate estimate is for games where both seats play on as the network
  does, so it isn't a win rate against perfect play, or against a person.
- **Hidden cards are uniform over what the public record allows.** There is no belief about what an opponent
  would have played given their hand.

## Checks

`test_search.py`, seeds 86000000-86009999. Each stage returns a failing exit code if a rule breaks, and the rows
that name a planted fault are checked on a broken build too.

| Check | Result |
| --- | --- |
| Without search, native play matches the JAX production chain and winner | 84 games, 12,380 decisions, 0 differ |
| A search that runs but is ignored changes nothing | 84 games, 12,099 decisions, 6,196 searches, 0 differ |
| Same game and seed across worker counts | 64 games, 4,940 searches, 0 differ |
| A wrong opposing list is never searched | 32 positions, 0 searched |
| Planted Lua failure and refused prompt in sampled worlds | 16 games each, 1,504 worlds voided each, 0 worker deaths. The 3 games each where the true duel also hit it are void |
| Planted hang | 2 games, 71 worlds voided, 0 worker deaths, 1 true duel void |
| The deal key is the seat's own decision count, from the game's record of who acted | 641 rows, 0 wrong. Planted total count wrong on 607, half the opponent's count on 588 |
| Estimates separate wins from losses, on games the network plays | Won 27% of decisions estimated low and 67% estimated high; near-certain 0% and 99.5%. Flipping the leaf or terminal sign reverses it, and controls with no estimates fail |
| A directory refuses other jobs, builds, Lua and arguments | 6 of 6 refused |
| The ranker loads the acting seat's net at that decision | 47 of 47 network choices match the move played, against 35 with the other net |

**The ranker's numbers**, on 64 games with budget 64 and depth 48:

| Ranker | Shown numbers wrong | Options alone that differ | Winner's last choice estimated at 80%+ | Later wins minus losses | Branches agreeing |
| --- | ---: | ---: | ---: | ---: | ---: |
| Correct | 0 | 0 of 645 | 58 of 63 | 37 points | 30 of 34 |
| All means and errors zero | 645 | 0 of 127 | no estimates pass | none | none |
| All numbers zero, samples too | 0 | 0 of 127 | 0 of 63 | 0 points | none |
| Numbers rotated across all options | 0 | 127 of 127 | 46 of 63 | 34 points | 7 of 34 |
| Rotated across all but the network's | 0 | 60 of 127 | 58 of 63 | 37 points | 21 of 34 |
| Swapped between the best and worst only | 0 | 15 of 127 | 58 of 63 | 37 points | 30 of 34 |
| Rollouts step the next option | 0 | 0 of 127 | 46 of 63 | 34 points | 7 of 34 |

"Alone" rolls an option out with no others on the deals it had, which must give the same results to the last bit. A
branch plays the shown best and worst in the true duel and ranks from there. Opening labels name cards from the acting
seat's list: 511 of 511. These are floors, not measures of accuracy.

**Privacy.** Two natural duels with one public history and one viewer view must get byte-identical search
answers, because the search may depend only on what the viewer can see. Pairs come from the forward model's
R4 and R4W rigs. The last row is the case a same-timeline test can't reach, such as a privately declined Gorz.

| Pairs | Both searched | One side refused | Answers identical |
| --- | --- | --- | --- |
| R4, one private timeline | 215 | 0 | 215 |
| R4W, one private timeline | 207 | 0 | 207 |
| R4W, private timelines differ | 146 | 0 | 146 |

The planted leaks are caught in every family. In the three families respectively, answers matched on 20 of 82,
20 of 84 and 13 of 45 pairs with the hand leak; 16, 17 and 10 with the seed leak; and 3, 3 and 1 with the order leak.

The pair test passes a fixed decision key, so it doesn't cover how the play loop chooses that key. That part
is checked by reading the code only.

The test doesn't record whether a pair's load orders differ. Sol found natural worlds where load order changes
which card a random discard takes under the same deal seed, so identical answers aren't guaranteed in general. Over
seeds the random picks tested showed no dependence, but a group summon such as Return from the Different Dimension
follows true load order on every seed. So a search can learn which public card the engine will prompt first, which a
player couldn't. It doesn't learn what's in a hidden slot. `docs/edison-forward-model.md` gives the limits.

## Pilot

Each row is 336 games on 168 seeds, across the gauntlet's neighbouring pairs in both seats and 3 networks,
at 64 rollouts per decision. These runs chose settings, so they are not the test.

Seeds 86300000-86300167, with the plain network scoring 46.1% from the same seats and seeds. A draw would count
half, and none occurred. These ran before the last forward model merge (commit 8554f63).

| Setting | Score | 95% range | Leaves the network's choice | CPU per game |
| --- | --- | --- | --- | --- |
| Depth 48, rollouts without the guard | 76.2% | 71.4-80.4 | 16.4% | 48 s |
| Depth 48 | 79.8% | 75.1-83.7 | 16.0% | 52 s |
| Depth 96 | 77.4% | 72.6-81.5 | 13.8% | 112 s |

The guard's 3.6 points are within noise on these 336 pairs. Depth 96 cost twice as much and did not score higher,
so the test uses depth 48.

Against a different network on seeds 86310000-86310167, depth 48 scored 75.9% (71.0-80.2), against 50.9% for the
plain network on those seats and seeds.

### On the deals before Sol's fixes

Seeds 86100000-86100167, against the same network. The plain network won 47.9% from the same seats and seeds.

| Setting | Win rate | 95% range | Leaves the network's choice | CPU per game |
| --- | --- | --- | --- | --- |
| Budget 64, depth 4 | 59.8% | 54.5-64.9 | 36.4% | 7 s |
| Budget 64, depth 16 | 71.4% | 66.4-76.0 | 25.1% | 20 s |
| Budget 64, depth 48 | 78.0% | 73.2-82.1 | 17.0% | 51 s |
| Budget 64, depth 16, margin 0 | 69.1% | 63.9-73.8 | 38.3% | 22 s |
| Budget 64, depth 16, margin 2 | 69.1% | 63.9-73.8 | 14.7% | 23 s |
| Budget 64, depth 16, 8 candidates | 72.3% | 67.3-76.8 | 26.2% | 20 s |
| Budget 256, depth 16 | 72.9% | 67.9-77.4 | 28.4% | 88 s |

Depth moves the result most. Four times the rollouts added about 1.5 points, within noise.

**Different opposing network.** Same-network rollouts predict the opponent's every choice given the deal, which
could flatter search. On seeds 86200000-86200167 the search rolled out with one network and faced another
without search: 71.1% (66.1-75.7) at depth 16 and 80.1% (75.5-84.0) at depth 48, against 48.8% for the plain
network on the same seeds and seats. So it did not flatter it here. Any two of the networks choose the same move on
about 57% of 474 sampled positions, but they share architecture and training method, so this doesn't speak for
other kinds of opponent.

**The opening "End turn".** At depth 16, on frog_monarch against blackwing, "End turn" led the first decision at
71.0% ± 1.3 (2,046 rollouts). At depth 48 "Set Treeborn Frog" led at 81.7% ± 2.5, with "End turn" at 78.3% ± 2.9.
I read this as a horizon effect in the shallow search, not proven. It is one position.

**Calibration.** Estimates run low. At depth 48, decisions estimated around 54% came from games that were won 84.5% of
the time. The likely reason is that rollouts play on as the network while the real game keeps searching, and
decisions within a game aren't independent, so this is a pattern and not a measurement.

## What this does not show

- It has not been tried against a person, or against anything but the three networks of one training method.
- It knows the opposing list, which a real player would have to infer.
- Sampled worlds can still fail. They are discarded and counted, and the true duel isn't affected. At depth 48 on the
  fixed deals, 241 of 1.36M rollouts failed, all in one game.
- When deals are refused, about 4-6% of searched decisions, the network plays without search. A refusal can
  reflect a hidden card (`docs/edison-forward-model.md`), so a seat's search being on or off is not perfectly
  independent of hidden information, though no estimate is steered by it.
- No search of the hidden information's game tree is done: each deal is searched as if the viewer knew it. That
  means bluffing and information-gathering plays are undervalued.
