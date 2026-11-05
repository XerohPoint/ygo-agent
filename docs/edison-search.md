# Edison search

Determinized search for the play engine: for each decision, deal the hidden cards many ways, try the
network's best few options on the same deals, and leave the network's choice only if an option does
measurably better. Code is in `tools/edison/search/`.

**Status:** pilot results only, and they choose settings. The M2 test has not run. See
[What this does not show](#what-this-does-not-show).

## How one decision works

`pimc::search` in `pimc.h` includes only the search API (`fm_world.h`) and the network (`edison_net.h`), so
it sees no core or environment type.

1. Take the options the network may play, best logits first, up to `max_candidates`.
2. For each deal: restore the root, deal from a seed, play one option, then roll out.
3. A rollout plays the network greedily for both seats for `depth` network decisions, then asks the value
   head. One-option steps are free. A finished duel scores +1, -1 or 0. A truncated or voided world gives
   no value and is skipped.
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
pilots and the different-network study, and `test_search.py` runs the checks below.

## Assumptions

- **The opposing Main Deck list is known.** A wrong list is refused or searched as declared, never corrected
  from hidden state. The Extra Deck is not re-dealt, under the same assumption.
- **Later play is the network's.** A win-rate estimate is for games where both seats play on as the network
  does, so it isn't a win rate against perfect play, or against a person.
- **Hidden cards are uniform over what the public record allows.** There is no belief about what an opponent
  would have played given their hand.

## Checks

`test_search.py`, on the deals after Sol's forward model review. Seeds 86000000-86009999.

| Check | Result |
| --- | --- |
| Without search, native play matches the JAX production chain and winner | 84 games, 12,380 decisions, 0 differ |
| A search that runs but is ignored changes nothing | 84 games, 12,099 decisions, 6,196 searches, 0 differ |
| Same game and seed across worker counts | 64 games, 5,337 searches, 0 differ |
| A wrong opposing list is never searched | 32 positions, 0 searched |
| Planted Lua failure and refused prompt in sampled worlds | 16 games each, 1,505 worlds voided, 0 worker deaths. The 6 games where the true duel also hit it are void |
| Planted hang | 2 games, 61 worlds voided, 0 worker deaths, 1 true duel void |

**Privacy.** Two natural duels with one public history and one viewer view must get byte-identical search
answers, because the search may depend only on what the viewer can see. Pairs come from the forward model's
R4 and R4W rigs. The last row is the case a same-timeline test can't reach, such as a privately declined Gorz.

| Pairs | Both searched | One side refused | Answers identical |
| --- | --- | --- | --- |
| R4, one private timeline | 222 | 0 | 222 |
| R4W, one private timeline | 218 | 0 | 218 |
| R4W, private timelines differ | 121 | 0 | 121 |

The planted leaks are caught at every level. In the three families respectively, answers matched on 18 of 66,
22 of 78 and 5 of 42 pairs with the hand leak; 15, 20 and 5 with the seed leak; and 8, 11 and 0 with the order leak.

The pair test passes a fixed decision key, so it doesn't cover how the play loop chooses that key. That part
is checked by reading the code only.

## Pilot

Each row is 336 games on 168 seeds, across the gauntlet's neighbouring pairs in both seats and 3 networks,
at 64 rollouts per decision. These runs chose settings, so they are not the test.

@@PILOT@@

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
- Sampled worlds can still fail: about 100 of 1.5M rollouts at depth 48 on the old deals, mostly Lua errors. They
  are discarded and counted, and the true duel isn't affected.
- When deals are refused, about 4-6% of searched decisions, the network plays without search. A refusal can
  reflect a hidden card (`docs/edison-forward-model.md`), so a seat's search being on or off is not perfectly
  independent of hidden information, though no estimate is steered by it.
- No search of the hidden information's game tree is done: each deal is searched as if the viewer knew it. That
  means bluffing and information-gathering plays are undervalued.
