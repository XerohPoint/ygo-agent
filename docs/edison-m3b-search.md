# M3 Stage B search check: what is fixed

Written November 16, 2026, after [the Stage B verdict](edison-heldout.md#stage-b-lists-with-cards-no-training-list-plays)
and **before any search game on the check's seeds**. [The Stage B protocol](edison-m3b-protocol.md#search-check) set
the check's outline and said its code is frozen in its own manifest first. This file and
[`stage_b_search.py`](../tools/edison/heldout/stage_b_search.py) are that code and design, hashed into
[`m3b-search-manifest.json`](../tools/edison/heldout/m3b-search-manifest.json).

The check is **reported only**. It has no bar, and no result changes the Stage B verdict.

## The question

Does M2's search still gain over the bare network when it pilots lists with cards no training list plays?

## Design

As [Stage A's check](edison-heldout.md#search-check-reported-only), with Stage B's builds, networks and seeds.

- **Builds.** 60 of the 747 held-out builds, drawn once from seed 90310001. 14 of them are in the heavy set H.
- **Opponents.** Each build meets 4 training lists drawn from the same seed: 240 pairs.
- **Games.** Each pair plays one seed, 90300000 + its index, in both seats: 480 comparisons. The search pilots the
  held-out build. The opponent is the same Stage B network without search, piloting the training list.
- **Comparison.** Each search game is compared with the plain game on the same seed, decks and network, where the
  network plays both seats without search. A win scores 1, a draw 1/2. The plain game scores the held-out build in
  plain network play, so it need not be 50%.
- **Networks.** The three sealed Stage B networks, `checkpoint-2560` of optimizers 1702, 1703 and 1704, exported to
  native files. The network rotates by pair.
- **Setting.** M2's frozen one: 64 rollouts, depth 48, 4 candidates, margin 1.0.
- **Intervals** resample the 60 builds, 10,000 draws.
- **Reported:** search score, plain score and gain on all comparisons, by seat, on H and on the rest; missing and void
  games with reasons; how often the search departed from the network's choice and how often it fell back to it;
  sampled worlds voided; CPU per game.
- **Runs once.** The manifest is committed alone on the commit it names, and `run` refuses anywhere else. Results sit
  in a directory named after the manifest. A game already played is not played again.

## What changed in the code

The search and its forward model read ids from code list v1. Three opt-in changes let them read v2:

- `ForwardModelConfig::codes` names the code list the env loads. Empty is v1.
- `edison_search` takes `--code-list v2 --id-row-sha S`. It then expects a v2 net file (4,042 ids, an unknown row, the
  wider action tables) and names the identity map's hash, which the loader requires. Without the options it behaves
  as before.
- `search_common.use_code_list_v2` points the Python driver at v2 networks. It refuses unless the process reads
  code list v2 itself and the file's hash is the one the networks are checked against.

The binary is rebuilt on the certified r5 core. M2 and Stage A used a binary built on r2, which stays their record.

## Checks before freezing

On development seeds 89700000 to 89799999. No game used a seed from 90300000 onward. The parity, values and smoke
checks were run again on the committed code, at commit f1555e1, with the same results; those runs are the ones `freeze` accepts.

| Check | Result |
| --- | --- |
| `selftest`, the job design | 15 of 15 pass |
| `parity`: the network through the search binary against the certified module under JAX, on 12 held-out builds (8 with the most unknown Main Deck copies, 4 that play Mind Crush) and 9 training lists | 60 games, 9,359 decisions: every observation byte, winner and length equal. 8 games met a prompt only the wider tables hold (18 decisions) |
| `parity`'s control: the same comparison with the JAX side reading each id's own row instead of the identity map | 46 of 48 held-out games differ, so the comparison sees a skipped map |
| Code list v1 with the rebuilt binary: `test_search.py`'s production and determinism stages | 42 games, 6,444 decisions equal; 16 search games equal on 1 and 12 workers |
| `values`: each sealed network's native logits and values against JAX on 18,371 recorded v2 decisions of lists with untrained cards, under the identity differential's gates | No gate fails and no greedy choice differs; the largest value error is 1.0e-5, against a gate of 2e-5. A build with the identity lookup of card rows removed fails nine gates on every network |
| `smoke`: 24 search games and 24 plain games at the frozen setting on those 12 builds, on 2 and on 15 workers | The same games on both. No void game, voided world, network error or option mismatch. The search fell back on 56 of 1,521 searched decisions |

The smoke test's scores are on 12 builds chosen for their unknown cards and are not a result.

**Review.** A Claude Opus reviewer at high effort read the change and the runner before the freeze. It found no way
for the run to play on a code list, identity map, network file or deck other than the sealed ones without being
refused, and no change to the default v1 path. Its findings:

| Finding | What was done |
| --- | --- |
| `freeze` would crash: this file did not exist yet | Written |
| `freeze` passed a file that was never added to git, since it asked git only about tracked files | `freeze` and `run` require every dependency to be in HEAD as it stands on disk |
| `smoke` could pass while every rollout failed, because a search with no sample left plays the network's move in a game that still ends normally | `smoke` now also needs no network error, no option mismatch and a fall-back share of at most 25% |
| Nothing compared the decks, database, scripts and code list with the Stage B test's | `freeze` and `run` refuse unless they equal `m3b-manifest.json`'s |
| The development checks' passes were not tied to what gets frozen | Each records the binary, network files and sources it ran on, and `freeze` requires those to be current |
| Plain games never compare the value head, which the search reads at every rollout's leaf | New `values` check, above |
| Nothing showed the binary was built from the committed sources | The build is reproducible, so `freeze` builds again and refuses if the binary changes |
| `export` overwrote the network files before comparing a second export | It writes beside them and replaces only when equal |
| The binary trusts its caller about which code list file the env loads; only the Python driver hashes it | Not changed. Every game here goes through the driver. A direct call to the binary is not checked |
| The forward model reads prompts that name a card conservatively in a sampled world | Not changed. It bears on search quality, not on correctness |

## Limits

M2's, unchanged:

- The search is told the opposing list, and deals the opponent's hidden cards uniformly from it rather than from a
  belief. Here the opposing list is a training list.
- Every opponent is one of the same three networks, so a gain is over the bare network, not over a person.
- The repetition guard is part of both players.

New here:

- Rollouts are played by the same network, which reads every unknown card through one shared row. The search can
  still see what an unknown card does, because rollouts run the real rules.
- 60 builds give wide ranges, and H has 14 of them.

## Commands

```
EDISON_CODE_LIST=v2 is set by the runner itself.
tools/edison/forward_model/build.sh edison_search
python tools/edison/heldout/stage_b_search.py export
python tools/edison/heldout/stage_b_search.py selftest
python tools/edison/heldout/stage_b_search.py parity --workers 12
python tools/edison/heldout/stage_b_search.py values
python tools/edison/heldout/stage_b_search.py smoke --workers 15
python tools/edison/heldout/stage_b_search.py freeze     # on a clean commit; commit the manifest alone
python tools/edison/heldout/stage_b_search.py run --workers 15
```
