# Edison M1 final test on the fixed Deck labels

Written and hashed before training, so before any game on these seeds. Simulator: certified module b8fcd2bcf7774ae4. It runs only if [the retraining study's](../protocol.md) G3 gives `CANDIDATE`.

**Question.** Do the round 2560 networks clearly beat the heuristic on fresh seeds, across all fixture decks and both seats?

**Models, with no selection:** `checkpoint-2560` of optimizers 1702, 1703, 1704. All three count, including the weakest.

## Matrix

| Block | Seeds | Matrix | Games per model |
| --- | ---: | --- | ---: |
| Fixtures | 83000000-83000255 | 3 fixtures, 6 ordered pairs, both seats | 3,072 |
| Gauntlet | 83010000-83010031 | 21 lists, next 4 in a cycle, both seats | 5,376 |

All play is greedy against the greedy heuristic. Fixtures run without the repetition guard and gauntlet networks run with it. 1 cell in 24 is replayed at a different thread count, and any mismatch stops the run. The heuristic mirror runs on both blocks as a control.

## Rules

The same rules as the earlier final test. Win rates are per optimizer, then averaged. Draws are not wins. "Lower bound" is the 95% Wilson bound over that optimizer's games.

- **F1, fixtures.** Passes if the mean is at least 55%, every optimizer's lower bound is above 50%, every fixture deck pooled over optimizers is at least 50%, and both seats pooled are at least 50%.
- **F2, gauntlet.** Passes if the mean is at least 55%, every optimizer's lower bound is above 50%, and both seats pooled are at least 50%. Per-list rates are reported with no pass rule.
- **F3, failure.** Void cells are reported by reason and their games do not count. A group above 1% void is `INVALID` and fails its block.
- **F4, guard diagnostic, no pass rule.** The same networks play the gauntlet block without the guard. Report the paired difference on games valid in both arms and the void reasons.

**Verdict:** `M1_EXIT_MET` if F1 and F2 both pass, otherwise `NOT_MET`. These seeds are consumed either way. A failure is not retried here, and any new attempt needs new settings or models and a new block.

This measures the networks against one heuristic that they also trained against. Three optimizers sharing environment seeds are replications of training, not independent samples of games. It is not a measure of tournament strength or of strength against human players.
