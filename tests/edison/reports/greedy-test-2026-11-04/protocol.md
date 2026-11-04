# Greedy-play test protocol

Written and committed before any game on these seeds was played. It tests the
recommendation in [the loss review](../../../../docs/edison-loss-review.md).

**Question:** do the frozen PPO weights score higher when they play their most
probable action than when they sample from their distribution?

**Seeds:** 72000000-72000015. Not used by any training, validation, test, forward-model
(70M) or pool-audit (71M) work. No model, hyperparameter or feature changes.

**Models:** `rl-budget-2026-11-02/seed-{1702,1703,1704}/final-policy.json`, unchanged.

**Matrix:** 3 optimizers, 6 ordered pairings of Blackwing, Frog Monarch and Machina Gadget,
learner in both seats, 16 seeds: 576 games per arm and opponent.

| Arm | Choice |
| --- | --- |
| sampled | Unmodified `agent_match` worker and `RLAgent` |
| greedy | Same worker, `RLAgent` subclass taking the highest-probability action, first index on ties |

**Opponents:** frozen heuristic and uniform random.

**Controls:** frozen heuristic against itself on the same seeds and pairings (96 games).
The greedy-versus-heuristic arm is replayed in a fresh process with 4 threads and must match.

**Primary measure:** greedy minus sampled win rate against the heuristic, per optimizer,
paired by game start.

**The recommendation passes only if all hold:**

1. Mean paired gain across optimizers against the heuristic is at least +5 percentage points.
2. Every optimizer's gain against the heuristic is above zero.
3. Against random, mean greedy minus sampled is no lower than -3 percentage points.
4. No incomplete duels, in any arm.
5. The replay matches.

**Reporting:** every arm and optimizer is reported whatever the result, including a failure.
A bootstrap over the 96 game starts is descriptive only. Starts are shared across optimizers
and arms, so these are not independent tournament samples, and results apply only to these
fixed decks and non-adapting opponents. A pass does not say greedy play is best against
adapting opponents, where predictable play could be exploited.
