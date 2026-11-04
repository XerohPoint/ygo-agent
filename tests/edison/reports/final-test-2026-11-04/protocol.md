# Edison M1 final test

Written and hashed before any game on these seeds, and before the per-deck tables of the long run were read. Simulator: certified module 67423a99970519fb.

**Question.** Do the round 1280 networks from the [long gauntlet run](../gauntlet-training-2026-11-04/protocol.md) clearly beat the heuristic on fresh seeds, across all fixture decks and both seats?

**Models, with no selection:** `checkpoint-1280` of optimizers 1702, 1703, 1704. All three count, including the weakest.

## Matrix

| Block | Seeds | Matrix | Games per model |
| --- | ---: | --- | ---: |
| Fixtures | 79000000-79000255 | 3 fixtures, 6 ordered pairs, both seats | 3,072 |
| Gauntlet | 79010000-79010031 | 21 lists, next 4 in a cycle, both seats | 5,376 |

All play is greedy against the greedy heuristic. As in the long run, fixtures run without the repetition guard and gauntlet networks run with it. 1 cell in 24 is replayed at a different thread count, and any mismatch stops the run. The heuristic mirror runs on both blocks as a control.

## Rules

Win rates are per optimizer, then averaged. Draws are not wins. "Lower bound" is the 95% Wilson bound over that optimizer's games.

- **F1, fixtures.** Passes if the mean is at least 55%, every optimizer's lower bound is above 50%, every fixture deck pooled over optimizers is at least 50%, and both seats pooled are at least 50%.
- **F2, gauntlet.** Passes if the mean is at least 55%, every optimizer's lower bound is above 50%, and both seats pooled are at least 50%. Per-list rates are reported with no pass rule.
- **F3, failure.** Void cells are reported by reason and their games do not count. A group above 1% void is `INVALID` and fails its block.
- **F4, guard diagnostic, no pass rule.** The same networks play the gauntlet block without the guard. Report the paired difference on games valid in both arms and the void reasons. This measures how much of the gauntlet result depends on the guard.

**Verdict:** `M1_EXIT_MET` if F1 and F2 both pass, otherwise `NOT_MET`. These seeds are consumed either way. A failure is not retried here, and any new attempt needs new settings or models and a new block.

This measures the networks against one heuristic that they also trained against. Three optimizers sharing environment seeds are replications of training, not independent samples of games. It is not a measure of tournament strength or of strength against human players.
