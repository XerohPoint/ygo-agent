# Edison AI roadmap

Started November 4, 2026. Owner: project supervisor session. Read
[the Opus 5.5 handoff](opus-5.5-handoff-2026-11-04.md) for simulator invariants
and experiment history. This file holds direction; evidence stays in study reports.

## Goal

Three tools for the Edison format (March 2010 list):

1. **Play engine** — given what a player can legally see, rank every legal action
   by estimated win probability, and explain the line behind the top choices.
2. **Deck builder** — propose legal 40-card decks with Extra and Side, ranked by
   measured strength against an explicit expected metagame.
3. **Side-deck advisor** — given both a deck and what game 1 revealed about the
   opponent, recommend swaps for games 2 and 3.

"Stockfish for Edison" sets the bar and the shape, not a literal promise. Chess has
no hidden information, so a chess engine computes one best move. Yu-Gi-Oh has hidden
hands, set cards and deck order, plus random draws, so the correct output is the
action with the best expected result **against a belief about hidden cards**. Good
moves can lose. Strength must be measured over many games, never judged from one.

## Where we are

Measured at commit 4e3452c:

| What a Stockfish-like engine needs | Current state |
| --- | --- |
| Correct, deterministic rules | 544 fixtures pass; bounded, not exhaustive |
| Copy a position to search it | No state copy or snapshot exists; search is not possible yet |
| Handle hidden information in search | Not started; no belief or determinization code |
| Strong evaluation | 32-unit NumPy MLP on hand-built features; 45.3% vs own heuristic playing greedily, 30.0% sampling ([fresh-seed test](edison-loss-review.md)) |
| Training scale | 3,072 duels on CPU; win rate was still rising at the end of the budget |
| GPU | RTX 5090 present; JAX 0.4.28 in `.venv` sees CPU only |
| Card coverage | 67 agent card identities of 3,659 legal cards; 3 fixed decks |
| Benchmark | Own heuristic and random only; no human or real-metagame reference |
| Matches and siding | Single duels; no best-of-three or Side swap flow |

Upstream `ygoai/rl/jax/agent.py` and `scripts/cleanba.py` contain a much larger
card-embedding transformer learner, and `mcts/` contains a C++ AlphaZero tree.
Neither is verified on the Edison pipeline or wired to it.

## Target ruleset and external sources

The target is **traditional community Edison**, as played on Format Library, Really
Big Edison and Dueling Book. It is not Konami's Time Wizard variant with modern
mechanics, announced around June 2025. That exemption comes from one fan-run post
([edisonformat.net](https://www.edisonformat.net)) and is not independently confirmed.

| Source | Use | Status |
| --- | --- | --- |
| [Rule differences](https://www.edisonformat.com/edison-rule-differences.html), [errata](https://www.edisonformat.com/rulings/relevant-errata), [rulings](https://www.edisonformat.com/rulings) | Rules specification for conformance tests | In use (M0) |
| [Format Library deck gallery](https://formatlibrary.com/deck-gallery/edison) and its public API | Gauntlet decks with Side decks | In use (M0) |
| Format Library replay metadata | Archetype share and human matchup results | [Collected](../tests/edison/reports/metagame-replays-2026-11-04/summary.json) |
| Dueling Book replay contents | Move-level human games | **Blocked**: needs a token; requires permission |
| Commentated replay reviews (YouTube): IMNerd, E3 Yugioh | Curated position test suite with human reasoning | [Pilot](edison-position-suite.md): 8 positions from one video; cannot run automatically until a position loader exists |

Replay metadata covers 697 of 698 listed games from 85 events. Blackwing (22.7%) and
Vayu Turbo (22.4%) lead by appearance. No fixture deck covers Vayu Turbo. Published
replays are a selected sample, so shares and matchup records are guidance, not a
true metagame measure, and the matchup sample sizes are small.

Do not scrape Dueling Book replay contents. The token requirement is an access
control, and the community scraper found (`nedhmn/duel-tools`) includes a CAPTCHA
solver. Move-level data needs permission from the site or data supplied by players.
Dueling Book is also a manual simulator without a rules engine, so its logs would
not map directly onto simulator actions and may contain rewinds or illegal plays.
If obtained, they could support imitation warm starts and a "does the engine agree
with strong players?" benchmark.

Commentated reviews serve a different purpose: a small hand-curated suite, similar
to chess test positions, that records the board, the options, the commentator's
preferred play and their reason. Commentators can be wrong, so these are human
opinions to compare against, not ground truth, and too few to train a network on.
Keep our own short notes and links rather than republishing transcripts or video.

## Order of work

Each milestone unblocks the next. Deck building and siding are evaluated by playing
games, so they are only as trustworthy as the play policy underneath them.

### Decisions from M0 evidence (November 4, 2026)

- **Evaluate greedily.** Playing the top action is the baseline; sampling stays a training
  diagnostic ([fresh-seed test](edison-loss-review.md)). Revisit with adapting opponents.
- **Search transport: arena snapshot.** One worker process per thread, built on the
  [forward-model report](edison-forward-model.md). Start with PIMC and a fresh deal per iteration,
  then add a learned belief. Defer ISMCTS unless measured losses call for it.
- **Observation encoding is now the main native speed limit.** It cuts simulation throughput
  by about 5.7×, so it is worth optimizing once the network's inputs settle.
- **Train on the sourced gauntlet.** [18 lists](edison-pool-audit.md) covering 85.93% of replay
  appearances pass legality today, and 90.66% once XX-Saber Gottoms is added. Main and Extra ran
  1,296 duels with no crashes. Treat the 3 fixtures as a regression set.
- **Treat the pool authority as the listed legal sets plus Format Library dates,** and add the 13
  omitted cards, subject to the owner's review. YGOPRODeck's filter missed a card that appears
  in 8 of 75 sampled tournament lists.
- **Hardening comes before the wider pool or siding.** Production ignores Lua errors because it never
  installs a message handler, and 114 confirmed cards crash the interface through unhandled prompts
  such as coin, dice and declarations. The gauntlet's Main and Extra avoid these, but Side decks do not.
- **Hardening landed; old win rates no longer compare.** [The hardening report](edison-hardening.md) covers Lua
  errors, rule fixes, prompts and legality, with 567 certification cases passing. Rule changes alter seeded games,
  so re-baseline before comparing with any earlier result. The forward-model test bed still links the older core.
- **Owner's rule decisions:** D1, Soul Exchange on the opponent's Sangan tributed for Caius gives Caius Chain Link 1 and Sangan
  Chain Link 2, and mandatory Broww goes before earlier optional Watapon, so SEGOC steps come first and chronology applies only
  within a step. D4, Ignition priority survives a declined Summon trigger. These outrank the sources that conflict.
- **M1 learner: smaller network, not yet past the bar.** [The report](edison-learner.md) has a 207,800-parameter card-identity network with
  league self-play. On the hardened simulator it scores 52.0% against the heuristic at 30,720 duels and 58.6% head to head against the NumPy
  baseline, below the 55% candidate bar. A fixture-trained net does not generalize to the gauntlet, so train on the gauntlet.
- **Keep the network on CPU for search, and use the GPU for training.** A leaf evaluation at 0.19 ms inline beats any plausible GPU round trip. The
  next search dependency is a native forward pass.
- **Greedy play needs a repetition guard outside the fixtures.** Free negated effects can loop to the decision limit, which matches open defect D12.
- **M1 exit met on the fixed labels.** [The report](edison-learner.md#m1-exit-on-the-fixed-deck-labels) has round 2560 networks at 67.6% on fixtures and 79.8% on the gauntlet against the heuristic on fresh seeds, with all three replications, fixture decks and seats past the predeclared bar. The earlier 55.9% and 71.3% run on the defective interface did not pass.
  The heuristic is weak and the decks are familiar, so this is not human or tournament strength. One matchup is still badly lost (Blackwing against Machina Gadget, 27.6%), and training was still rising.
- **Deck option labels are fixed; earlier strength results no longer stand.** [The defect](edison-learner.md#known-defect-deck-option-labels) showed policies the wrong cards at Deck prompts and exposed hidden Deck cards to their owner, for every result up to then.
  [The fix](edison-deck-labels.md) is merged with 593 certification cases, and the networks are retrained. [Substitoad chains](edison-learner.md#on-the-fixed-deck-labels) became about 5 times as common, though the whole line stays rare.
- **Independent review and differential checks for interface code.** A prompt differential now compares observations with the engine's own prompts. New checks must also run on the old build, because a check that passes on both proves nothing.
- **M2 exit met.** [The final test](edison-search.md#m2-final-test) has search at 64 rollouts and depth 48 scoring 80.3% (77.8-82.7)
  against the same network without search, on fresh seeds, with every net and both seats past the predeclared bar and `rank_position.py`
  passing the ranker rules. Against a different network it scored 79.5%. Sol recounted it with separate code and reran frozen games and
  every ranker position with no differences. Every opponent is one of three networks from one training method, some wins punish that
  network's own mistakes, the search is told the opposing list, and hidden cards are dealt uniformly rather than from a belief. So this
  is not human strength.
- **Open: loading a position from a real external game.** Replay needs private data and construction
  fails, so M6 and the position suite need their own solution.

### M0 — Evidence (in progress)

- **Loss review**: why the learned policy loses to the heuristic, classified as
  policy, observation, interface or rules causes.
- **Forward-model spike**: measured options for copying or reconstructing positions.
  This decides whether search can be fast enough, and how M2 is built.
- **Pool and metagame audit**: which legal cards actually work, sourced real Edison
  decks with Side decks, and what match play needs.
- **Rules conformance**: test the simulator against the published rule differences,
  errata and card rulings. Defects are reported first and fixed after review.

Exit: a decision for M1's learner and M2's search, each backed by measurement.

### M1 — A policy/value network that scales

GPU training on a card-embedding network, self-play against a league of frozen
opponents rather than only the heuristic, and every existing privacy contract kept
for anything used at play time. Exit: clearly beats the heuristic on fresh seeds
across all fixture decks and both seats, with replications. **Met by [predeclared rule](edison-learner.md#m1-exit-on-the-fixed-deck-labels), November 5, 2026.**

### M2 — Search: the actual engine

Search over sampled hidden states guided by the M1 network, built on the forward
model chosen in M0. Includes opponent-hand and deck belief. Exit: search at a fixed
decision budget beats the same network without search on fresh seeds, and a command
ranks actions with estimated win rates from a recorded position. **Met by [predeclared rule](edison-search.md#m2-final-test),
November 5, 2026.** The belief is not built yet: hidden cards are dealt uniformly over what the public record allows,
and the opposing list is given.

### M3 — Real metagame coverage

Expand from 3 fixtures to a sourced gauntlet of Edison archetypes, and fix the rule
and card defects that this exposes. Test against decks held out of training to
measure generalization. Exit: the policy performs on held-out decks, not only known ones. **Stage A, new builds of
known cards, met by [predeclared rule](edison-heldout.md), November 10, 2026:** 80.6% piloting 488 placed builds no
network trained on, with the edge over the heuristic within a point of the edge on each build's nearest training list.
Sol recounted it with separate code and reran 84 cell runs and 48 search games with no differences.
Stage B, cards no training list plays, needs code list v2, an unknown-identity row and retraining, and is not started.

### M4 — Deck builder

An outer search over legal decks, scored by playing the gauntlet: fast surrogate
filters candidates, then successive halving spends games on survivors. Exit:
recovers or beats known-good lists on fresh seeds, and generated decks are checked
by production legality validation.

### M5 — Side-deck advisor

Best-of-three support with Side swaps, inference of the opponent's deck from cards
seen, then swap recommendations scored by simulated post-side games. Exit: advised
siding beats no siding and a simple rule baseline over complete matches.

### M6 — Usable product and human validation

A way for a person to load a position, replay or deck and get recommendations.
Strength checked against experienced Edison players, the only external reference.

## Standing rules

- Keep Rule 5 comparison behavior and all privacy contracts. Search may only sample
  hidden state from what the acting player could know; it must never read it.
- Do not tune on consumed test seeds. Predeclare fresh final-test blocks.
- Report failed trials and variation; do not present fixed-deck win rates as
  tournament strength or bounded certification as complete rules correctness.
- Evaluation scripts that change no runtime behavior belong in `tools/edison/`, so
  they do not invalidate certification fingerprints of `tests/edison/*.py`.
