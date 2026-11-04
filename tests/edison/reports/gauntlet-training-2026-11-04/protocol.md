# Edison long gauntlet training

Written and hashed before training. Simulator: certified module 67423a99970519fb (`edison_rules` 6df1bd58680d6816), which includes
the owner's trigger-order rulings.

**Question.** With network settings unchanged from the pilot, does 8 times more gauntlet training pass the heuristic on fixtures and the gauntlet?

## Training

Arm `gauntlet`: 21 lists, random pairs fixed per round, random seat, rotating thirds of frozen heuristic, past self and current self. Optimizers
1702, 1703, 1704. 1,280 rounds of 96 duels, 122,880 duels each. Seed base 77000000. 64 channels, 2 layers, 4 heads, id dim 32, critic 128, Adam 1e-3, otherwise
the pilot's frozen settings, on code list v1 (53fcea4d). The 2 lists with XX-Saber Gottoms still play without it. Learner on the GPU in `.venv-gpu` with
`--xla_gpu_exclude_nondeterministic_ops=true`; actors on CPU with the certified module. No hyperparameter changes during the run.

**Fixed checkpoints, no selection:** rounds 160, 320, 640, 1280.

## Evaluation

| Block | Seeds | Matrix | Use |
| --- | ---: | --- | --- |
| Fixtures | 78000000-78000063 | 3 fixtures, 6 ordered pairs, both seats, 768 games per model and opponent | Main readout |
| Gauntlet | 78010000-78010007 | 21 lists, next 4 in a cycle, both seats, 1,344 games per model | Main readout for the decks trained on |

All play is greedy. Fixtures: every checkpoint against the heuristic; round 1280 also against random and the same-index NumPy baseline; baseline and heuristic controls
on the same starts. Gauntlet: every checkpoint, the baseline and the heuristic mirror against the heuristic, with the public repetition guard on networks only.
1 cell in 24 is replayed at a different thread count, and any mismatch stops the run. These blocks are consumed by this study and are not a final test.

## Rules for what will be concluded

Win rates are per optimizer, then averaged. Draws are not wins.

- **G1, more games.** `MORE_GAMES_HELP` on a block if round 1280 minus round 160 against the heuristic averages at least +5 pp with every optimizer above 0. Otherwise `NOT_SHOWN`. Reported for both blocks.
- **G2, plateau.** `STILL_RISING` on a block if round 1280 minus round 640 averages at least +2 pp. Otherwise `FLAT_OR_NOISE`.
- **G3, candidate.** Round 1280 is a candidate for a predeclared fresh final test only with mean at least 55% and every optimizer at least 50% against the heuristic on **both** blocks. This is not an M1 pass.
- **G4, baseline.** `NET_AHEAD` if round 1280's paired gain over the baseline against the heuristic is above 0 on both blocks and fixture head to head is at least 50%. Budgets differ, so this cannot rank the methods.
- **G5, failure.** Void cells are reported by reason and their games do not count. A group above 1% void is `INVALID`. If training itself aborts on the 1% void limit, that is reported as the result.

The guard fired about once per game on the earlier gauntlet block, so gauntlet numbers include its unmeasured effect. Three optimizers sharing environment seeds are not independent samples, and these
opponents are not a measure of tournament strength. Any later change of settings is a new study with new seeds.
