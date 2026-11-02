# Edison production baseline

This document records checkpoint d657dde. The subsequent audited heuristic,
expanded stress tests and training pilot are in [edison-agents.md](edison-agents.md).

The full certification suite passes **519 cases, 0 failures** on the final build:
507 native cases, 10 production deck-ingestion cases, and 2 production interaction
cases. Both complete output traces are identical.

The fixed-deck baseline completed **64 distinct seeded matchup duels** and **162
executions** including fresh-process replays and scheduling controls. Completion
was **100%**, with no crashes, native diagnostics, timeouts, truncations, or
decision-limit endings in these runs.

## Reproduce

From the repository root, with the pinned scripts/database installed:

    source .venv/bin/activate
    xmake b edison_rules
    xmake b ygopro_ygoenv
    python tests/edison/run.py
    python tests/edison/baseline.py

Production regression alone:

    python tests/edison/run.py --test 077 --fail-fast

Custom seeded matchup:

    python tests/edison/smoke.py --deck1 blackwing --deck2 frog_monarch --seed 20100424 --num-envs 16

The production target copies the module into ygoenv/ygoenv/ygopro. Smoke assets
are checked against smoke_assets.lock.json; each replay uses a fresh process.
No model checkpoint, GPU, training package, or optimizer is needed.

## Decks and policy

assets/edison/decks contains constructed integration fixtures: Blackwing
(40 Main, 15 Extra) and Frog Monarch (40 Main, no Extra), both with empty Side
Decks. Production checks pool membership, deck sizes, and March 2010 limits.
These are test fixtures, not tournament recommendations.

Both players choose uniformly among offered legal options. Each environment has
its own Python Random instance, seeded with (20100424 + environment ID) XOR
0xED150. The production pool seed is 20100424; its environment seeds add the ID.
The policy uses no opponent-private information.

The action capacity is 256, and reaching it fails the run to avoid silently
truncated choices. The largest observed choice set was 26. The 10,000-decision
guard is a failure, not a counted win. Native step timeout is 10 seconds, with
an additional worker-process timeout. Completed duels took 80–444 decisions.

## Results

Each row has 16 distinct seeds and two identical fresh runs. Wins are by seat.
Rates average the two runs and include reset, Python decisions, observation
hashing, and flushed trace logging; module/pool creation is excluded. These
are instrumented smoke rates, not a training throughput benchmark.

| P0 deck | P1 deck | P0–P1 wins | Mean decisions/duel | Decisions/second |
| --- | --- | ---: | ---: | ---: |
| blackwing | blackwing | 10–6 | 241.2 | 12,334 |
| blackwing | frog_monarch | 13–3 | 227.2 | 11,118 |
| frog_monarch | blackwing | 4–12 | 225.4 | 12,380 |
| frog_monarch | frog_monarch | 9–7 | 293.6 | 13,568 |

These small random-agent samples establish an operational baseline, not
deck-strength estimates. Both deck orientations and both mirrors are included.
Environment 0 matches standalone serial play. All sixteen cross-match duels
match with one versus sixteen worker threads, including decision fingerprints
and terminal results. Longest observed parallel batch step was under 3 ms.
Performance varies by host and load.

## Failure and regression

The original Blackwing/Frog seed crashed after selecting Dupe Frog's Normal
Summon: the core requested Des Frog metadata (84451804), absent from the minimal
preload list. Dupe Frog changes its on-field name to Des Frog. The fix adds that
pinned resource alongside Gorz's token; neither is added to the decks.
This was a fixture resource-closure defect, not a core-rules regression.

Test 077A plays the original seed beyond that summon to a natural duel end.
077B compares four complete seeded duels with one versus four worker threads.
The regression requires the Dupe Normal Summon actually to be selected and the
old failing checkpoint to be passed. Both cases are part of the full suite.

The preload inventory is 55 identities: deck cards and two resource-only
identities. The smoke lock pins the database, decks, metadata, code list,
required scripts, and shared helpers.

## Review and checkpoint

Review covered Rule-1-only script/core routing, chronological trigger groups,
phase retries, Soul Exchange, in-resolution summons, YDK parsing, combined-zone
legality, shared script loading, evidence fingerprints, production initialization,
natural termination accounting, RNG isolation, and scheduling independence.

The initial full suite passed 517 cases; after production coverage was added,
the final full suite passed 519. No engine change was needed in this checkpoint
pass. Dependency versions, upstream pins, and existing tags remain unchanged.
Training and optimization have not started.

## Committed evidence

- [Snapshot metadata](../tests/edison/reports/baseline-2026-11-02/snapshot.json):
  runtime versions and artifact hashes.
- [Certification report](../tests/edison/reports/baseline-2026-11-02/certification.json):
  source/core/assets/binary fingerprints and both trace hashes.
- [Compressed full trace](../tests/edison/reports/baseline-2026-11-02/certification-trace.txt.gz):
  identical output from either full run.
- [Playing baseline report](../tests/edison/reports/baseline-2026-11-02/playing-baseline.json):
  every seeded result, decision hash, interaction counter, and timing.

Reports preserve original absolute execution paths as provenance; commands above
use repository-relative paths. Binaries are not committed, but their hashes
identify the tested artifacts. The commit containing this document is the
checkpoint; snapshot.parent_commit identifies its pre-existing parent.

Next work can expand deck/seed coverage and compare a simple heuristic policy
against this baseline before training. These results do not establish exhaustive
correctness for every Edison interaction.
