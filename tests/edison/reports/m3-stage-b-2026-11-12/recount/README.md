# M3 Stage B: independent recount

November 16, 2026. A Claude Sonnet subagent at high effort recounted the Stage B run with its own code. It was told
not to read `stage_b.py` or `stage_a.py`, to work from [the protocol](../../../../../docs/edison-m3b-protocol.md),
`stage-b-lists.json` and the 115 raw chunk files, and to open `stage-b-result.json` only after its own numbers were
on disk. It reran no game.

**Outcome: the recount agrees.** It reaches `STAGE_B_MET` and the same four readings from its own numbers.

- **Records.** 115 chunks, 333,144 cells, 1,875,888 games, every cell `PASS`, none missing or duplicated, every seed
  as the protocol's formula gives it, and all 2,624 replays equal to their first run on winner and trace.
- **Point estimates.** Every gated number and every reading matches the result file to 1.3e-16.
- **Intervals.** Its bootstrap draws differ from the runner's. Interval ends differ by at most 0.035 points, and none
  is near a bar.
- **Who is counted.** It derived from the rows that pilot, base and mask count the network on build X, face counts
  the network on training list O, and mirror counts X with the heuristic on both sides. It checked this on the
  known-block mirror cells where X is O: the two seats' games are identical and the counted side wins exactly half.
- **Usage.** The offered and chosen counts for activate, Normal Summon and attack in the held block's pilot, base,
  face and mirror arms equal the result file's. It took the counters' layout from `actor.py`.

**Not recounted:** guard fires, decisions needing the wider tables, the breakdowns by archetype and by card, the
per-build table, the per-list dose curve, the heavy set's usage table, and the Set and Special Summon usage rows.

**One question it raised.** 10,294 games carry `win_reason` −1. That is the env's code for a failed draw
(`ygopro.h`: core reason 2 is reported as −1, core reason 1 as 1), and Stage A had the same kind of ending. The games
are ordinary wins and losses. Counting them as losses instead would leave every rule holding (pilot 75.4% on all
builds and 72.9% on H).

## Files

| File | Holds |
| --- | --- |
| `load.py` | Reads the chunks into compact rows and checks each replay against its row |
| `analyze_recount.py` | Integrity, conventions, rules, readings and its bootstrap |
| `recount_log.txt`, `recount_results.json` | Its output |
| `usage_recount.py`, `usage_log.txt`, `usage_recount.json` | The usage recount |
| `compare.py`, `comparison.txt`, `comparison.json` | Its numbers beside the result file's |

The scripts read the build directory by absolute path. `raw-sha256.txt`, one level up, holds the hashes of the chunk
files they read.
