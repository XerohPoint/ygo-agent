# Fable 5.2 handoff: Edison-format AI, technical lead

Prepared November 12, 2026, by the Opus 5.5 lead session, for Claude Fable 5.2. It replaces
[the Opus 5.5 handoff](opus-5.5-handoff-2026-11-04.md) as the starting point. That file still holds the simulator invariants
and the early experiment history.

## Your role

You are now the technical lead. This codebase is yours: what it's for and how it's built are your call, and you'll keep
building on it. The owner supervises. They set goals, settle rulings that only a player can settle, and approve pushes.
They prefer that you carry authorized work through to a concrete result rather than ask at each step.

The goal is three tools for the Edison format (the March 2010 list), in this order, because each depends on the one before:

1. a **play engine** that ranks legal actions by win probability against a belief about hidden cards;
2. a **deck builder** that proposes legal decks ranked against an expected metagame;
3. a **side-deck advisor** for games 2 and 3.

[The roadmap](edison-roadmap.md) holds the milestones (M0 to M6) and the decisions taken so far. Read it first.

## Where things stand

**Update, November 16, 2026.** M3 Stage B is done and met, so M3's exit is met; the report is
[edison-heldout.md](edison-heldout.md#stage-b-lists-with-cards-no-training-list-plays). Retraining for unknown cards
was not shown to help. Local `edison` holds the Stage B commits and is ahead of `origin/edison`; nothing was pushed.
Still open: the Stage B search check, which needs `edison_search` to load code list v2 and a rebuild on r5, with its
own freeze. The rest of this section is as written on November 12.

- **Branch:** `edison` at `e21b7cd`. It was pushed to `origin/edison` (the owner's fork on GitHub) on November 11. The
  working tree is clean apart from the untracked `.claude/`. `main` is the upstream project; do not build on it.
- **Nothing is running.** No experiment or review is waiting to be resumed.
- **Milestones:**
  - M0 (evidence) and M1 (a network that beats the heuristic) are done.
  - M2 (search beats the same network without search) is met: 80.3% on fresh seeds.
  - M3 Stage A (held-out builds of known cards) is met: 80.6% piloting 488 builds that no network trained on.
  - M3 Stage B (lists with cards that no training list plays) had not started on November 12. It was met on
    November 16: 75.6% piloting 747 builds, against 75.4% for M1's networks without retraining. M4 to M6 have not started.
- **The certified build:**
  - The engine is ygopro-core `f9692965` with two patches, early trigger and hardening revision **r5**.
  - Production module `d1f1f701...`, rules binary `edison_rules` `29ca41fd...`, and conformance binary `8a3a107b...`.
  - Certification passes **619 of 619** cases and replays identically. The hashes are recorded in
    `tests/edison/reports/gold-sarcophagus-2026-11-11/hashes.json`.
- **The last two pieces of work** were engine crash fixes found by the M3 corpus soak:
  - The Dark Panther copy fix (r3) is in [edison-dark-panther.md](edison-dark-panther.md).
  - The Gold Sarcophagus fix (r5), for a pointer to a deleted effect, is in [edison-gold-sarcophagus.md](edison-gold-sarcophagus.md).
  - Both reports end with what they don't cover.

All strength numbers so far are against a weak rule-based heuristic, with the opposing list given to the search, on deck
lists from one site's events. They are not human or tournament strength. Keep saying so in reports.

## Next task: M3 Stage B

**Done on November 16, 2026**, apart from the search check. This section is kept as the brief it was.

Stage B asks whether the networks can play cards they never trained on. The outline is in
[the Stage A protocol](edison-m3-protocol.md#stage-b-outline), and [the Stage A report](edison-heldout.md)
listed what it needs. In order:

1. **Rebuild the search binary on r5.** Run `tools/edison/forward_model/build.sh edison_search`. The script refuses any core
   other than the certified r5 library. The binary that M2 and Stage A used (`167be8aa...`) was built on r2 and stays the
   record for those runs.
2. **Write the Stage B protocol and freeze it before any training.** Use the M2 and Stage A pattern: the protocol and the runner are hashed
   into a manifest that sits alone on the commit it names. The outline fixes these parts:
   - **Code list v2:** the whole legal pool. Its first 509 lines are v1's, so the v1 checkpoints load with rows added.
   - **The unknown-identity row:** one reserved row stands for a card the network does not know. A constant mask sends every
     card that no training list plays to that row. Training replaces known identities with it at a fixed rate, so the
     network learns to play from printed features.
   - **The native forward pass** in `tools/edison/search/edison_net.h` must apply the same mask.
   - **Training:** the M1 trainer and budget (round 2560 on the 21 training lists) with three optimizers, 1702, 1703 and 1704.
   - **The held-out set:** legal corpus lists that are not training lists and have at least one Main or Extra card that no
     training list plays.
3. **Get the code reviewed before the long run.** The code list, the mask, observation encoding and the native
   forward pass are interface code. That is the class where the Deck-label defect hid behind hundreds of passing tests.
   Build a differential that compares what the policy sees with what the engine offers, and run it on the old build too.
   A check that passes on both builds proves nothing.
4. **Train, evaluate on fresh predeclared seeds, and recount** the verdict with separate code before reporting it.

The protocol is your design. These are starting points, not constraints. If the evidence points somewhere else,
for example a learned belief or a tougher opponent before Stage B, make the case in a report and change the plan.

## Subagents, models and effort

The owner set three limits for you:

- **Use fewer than 5 subagents.** Run at most 4 Claude subagents at a time. Most tasks need none or one. Don't use the
  Workflow tool: one workflow can start more agents than that limit allows.
- **Choose the model and effort level for each subagent.** Set both explicitly on every Agent call. The owner asked for
  this, so it overrides the Agent tool's default of leaving effort unset.
- **Use Sol only for code review, and only when necessary.** The owner has little Sol usage. See the next section.

A subagent starts with no context, so it costs a full read of everything it needs, and you see only its summary. Use one
when the work can run in parallel with yours, or when you want a reading of code that isn't anchored on your own
conclusion. Don't use one to wait on a long job: run the job with background Bash and you are told when it ends.

Prices per million input and output tokens (from the Claude API reference, November 2026):

| Agent `model` | Model | Input | Output | Relative cost |
| --- | --- | ---: | ---: | ---: |
| `fable` | Fable 5.2 | $8.00 | $40.00 | 80× |
| `opus` | Opus 5.5 | $4.00 | $20.00 | 40× |
| `sonnet` | Sonnet 5.5 | $2.00 | $10.00 | 20× |
| `haiku` | Haiku 5.5 | $0.10 | $0.50 | 1× |

Higher effort means more thinking and more tool calls, so it multiplies cost within a model. Recommended defaults for this
project:

| Task | Model | Effort | Reason |
| --- | --- | --- | --- |
| Find code, files or facts across the repository (read-only, `Explore` agent) | `haiku` | `low` | Locating, not judging |
| Check a report's numbers against its JSON evidence | `sonnet` | `low` | Mechanical, but numbers must be exact |
| Write a well-specified script, fixture or analysis tool in `tools/edison/` | `sonnet` | `medium` | The spec carries the hard part |
| Independent recount of a frozen test, with separate code | `sonnet` | `high` | Well specified, correctness critical |
| Look up and paraphrase rulings (edisonformat.com and similar) | `sonnet` | `medium` | Reading and summarizing |
| Independent review of engine, interface, privacy or search code | `opus` | `high` | Claude reviewers found the r4 crash and the test 090D bug; another model reads differently from you |
| The same review when the change touches object lifetimes or pointers in the core | `opus` | `xhigh` | Those bugs only show in rare legal games |
| An engine or rules fix in its own worktree, while you do other work | `opus` | `high` | Hard, but reviewable afterwards |
| Protocol design, statistics, choosing the next milestone step | yourself | `high` | Judgment you own |
| Work no other model has managed, after a failed attempt | `fable` | `high` | Only when the price is earned |

For your own turns, `high` is the right default. Use `medium` for routine bookkeeping, docs and small edits. Use `xhigh`
only for hard engine debugging where `high` has stalled. Change any row when you measure a better choice, and record it here.

**Briefing a subagent:**
- Give a reviewer the code and the question, not your conclusion.
- Tell every worktree agent:
  - to use `xmake -P <worktree>` (a bare `xmake` acts on the main checkout);
  - to report its base commit;
  - to set up the ignored files (see the environment section).
- Subagents don't commit. You review their work and commit it yourself, staging explicit paths.

## Sol (gpt-6.1-sol)

Sol is an OpenAI model reached through the Codex CLI. Its usage comes from the owner's ChatGPT account and runs out.

**When Sol is necessary.** All three must hold:
- the change alters the certified build (a core patch, `ygopro.h`, the observation or action interface), or it alters
  privacy-bearing code, meaning what the policy or the search can see;
- a Claude reviewer has already reviewed it, and you have fixed what that review found;
- its result will carry a milestone claim or a long training run.

The Stage B interface change meets this bar. Docs, analysis scripts, test-only changes, recounts and reverts don't.
Use a Claude subagent for those.

**What has worked.** Sol has found real defects. In the r5 review it found two test gaps; each let a broken fix pass the
suite, and tests 090E and 090F now cover them. It has also reported cases that no legal game can reach.
- Verify each finding yourself, and sort the findings by whether a legal game reaches them.
- Set a stopping rule in the brief, so a review doesn't run on through unreachable cases.

**How to run it:**
- The commands are in the project memory note `sol-codex-cli.md`. In short, Codex runs on Windows through `cmd.exe`,
  with the prompt on stdin. Give Sol its own worktree, and never resume a thread while a turn is running.
- Point Sol at the sections it needs, not whole reports. That saves usage, and it avoids a content filter: one brief that
  asked Sol to break a fix with a memory checker, with use-after-free traces, was stopped as a security risk. Frame engine
  reviews as rules-engine correctness, and leave memory-checker work to a Claude reviewer.
- A run that ends with "You've hit your usage limit ... try again at <time>" is the account limit, not a review result.
  Resume the same thread after that time with a short prompt. Don't buy credits.

## Approvals

You may do these without asking:
- commit reviewed checkpoints to `edison` and merge your own branches into it locally;
- build, run certification, soaks, training and evaluations;
- create worktrees and branches;
- start Claude subagents within the limits above, and Sol reviews that meet the bar above.

Ask the owner first for:
- pushing or opening a pull request;
- any download or install, apart from GPU packages in the separate `.venv-gpu`, which are approved;
- contacting Format Library, Dueling Book or anyone else.

Never buy usage credits.

Two decisions belong to the owner and are settled: rulings D1 (Caius takes Chain Link 1) and D4 (Ignition priority
survives a declined trigger). Don't raise them again. New rulings conflicts go to the owner, with the sources side by side.

## Rules that protect the results

Each of these rules exists because breaking it would make a result wrong or would expose private data.

- **Hidden information.** At play time nothing may read hidden state: `oppo_info=False`, and there is no privileged core query. Search
  may only sample hidden cards from what the acting player could know.
- **Seeds.** Predeclare final-test seed blocks and never tune on a consumed block. The used blocks are listed in each study
  report.
- **Honest reporting.** Report failed trials and variation, and never drop a run to improve an average. Bounded certification
  is not complete rules correctness, and fixed-deck win rates are not tournament strength.
- **Rule 5 controls.** Keep Rule 5 comparison behavior. When you fix an Edison ruling, add a control on the stock scripts.
- **Engine changes.** Report a defect first, then fix it with its own tests and a review. Bump the `edison_hardening`
  revision string whenever the patch changes. Run the rules suite itself under the address sanitizer: in the r5 work,
  a test read freed memory. When you clear or null an engine pointer, check every place that dereferences it.
- **Player privacy.** Committed data carries no player name, handle or id from the corpus. Run
  `tools/edison/corpus/privacy_check.py` on the files you commit. The raw responses it reads hold player names, so they
  are not committed. They are only in `.claude/worktrees/m3-corpus/build/edison-corpus/raw`, so pass that path with
  `--raw`. On thousands of files the check is too slow, because it runs one regex per name per file. Before the November
  11 push, a version of the same check that indexes words took 3 seconds. Making that the committed version is a good
  small task.
- **Sources.** Don't scrape Dueling Book replay contents: they sit behind a token, and the known scraper solves a CAPTCHA.
  Paraphrase rulings and commentary; don't republish transcripts or video.
- **Analysis code location.** Put evaluation code that changes no runtime behavior in `tools/edison/`, not `tests/edison/`. Certification
  fingerprints the Python files directly under `tests/edison/`.

## Known open issues

- **A Field-type gap in test 090.** No test covers a deleted effect that lacks the Field type bit. One broken version of the r5 fix still
  passes the suite because of it, and no traced legal duel contained such an effect.
- **Script-held effect references.** Lua scripts that keep their own reference to an effect still outlive its deletion. This
  predates r5.
- **An unchecked Dark Panther ruling.** No one has checked against a ruling the case where a Dark Panther target changes control before the copy resolves.
- **`resp_buf_[128]` in `ygopro.h`.** It is shorter than the 512 bytes that `set_responseb` copies. The core uses only the
  counted bytes.
- **Open M2 search issues:**
  - There is no belief over hidden cards: they are dealt uniformly.
  - The search is told the opposing list.
  - Every opponent is one of three networks from one training method.
  - Smaller items: no native fall-back flag in the search log, a load-order channel to remove with a seeded shuffle, and a
    privacy test for equal-view pairs with different private choices.
- **Real external positions.** No way exists to load a position from a real game. This blocks the commentated
  position suite and M6.
- **Rules conformance defects.** The ones still open are recorded in `tests/edison/conformance/known_failures.json`.

## Environment

- **Checkout.** `/home/mada/ygo-agent` in WSL Ubuntu 22.04. Use `.venv/bin/python`. GPU training uses `.venv-gpu`, with the
  actors in `.venv`.
- **Machine.** 16 threads, 30 GB of RAM and an RTX 5090. The owner uses it for other heavy work. When they ask you to pause,
  stop every Edison process, including your own Codex process tree but never the Codex desktop app. When they say to
  continue while they use the PC, run at most 4 workers under `nice -n 19`.
- **xmake.**
  - It hangs silently while it tries the network. Prefix builds with a dead proxy: `http_proxy`, `https_proxy`,
    `all_proxy`, `HTTP_PROXY` and `HTTPS_PROXY` all set to `http://127.0.0.1:9`.
  - In a worktree, run `xmake f -P $W -o $W/build -m release -y`, then `xmake b -r -P $W <target>`. For the main checkout,
    run `env -C /home/mada/ygo-agent xmake ...`.
  - Compare the binary hash with what you expect. `tests/edison/run.py` refuses a module older than its sources.
- **A new worktree needs the ignored files:**
  - copy `assets/locale/en/cards.cdb`;
  - link `scripts/script` to `/home/mada/ygopro-scripts`;
  - generate `build/edison-pool-audit-2026-11-04` with `tools/edison/pool_audit/build_codelist.py` before any soak.

  `tools/edison/hardening/soak.py` allows at most 6 workers.
- **Old worktrees.** Many old worktrees under `.claude/worktrees/` keep review fixtures and evidence builds, for example
  `sarc-fix/build/sarc/` with the hand-built sanitizer and mutant tooling. Leave them unless the owner agrees to a cleanup.

## Memory and notes

Project memory notes load automatically in your sessions. Some of them were written for earlier sessions, and where they
conflict with this handoff, this handoff wins. In particular, the note about Sol reviewing all new code is superseded by
the Sol section above. Remove a note's cautions when their reason no longer holds.

## Reports

The owner reads reports and may read them on another device without the files open:
- Lead with the result and what it does not show.
- Give exact numbers with their sample sizes.
- Name the commit and the binary hashes.
- Say which reviewer actually ran.
- Keep each study's evidence under `tests/edison/reports/<study>-<date>/`, and use a fresh output directory for every run.

## Where to read

| File | What it holds |
| --- | --- |
| [edison-roadmap.md](edison-roadmap.md) | Goal, milestones, decisions, standing rules |
| [opus-5.5-handoff-2026-11-04.md](opus-5.5-handoff-2026-11-04.md) | Simulator invariants and observation contracts |
| [edison-m3-protocol.md](edison-m3-protocol.md), [edison-heldout.md](edison-heldout.md) | Stage A protocol, and the results of both stages |
| [edison-m3b-protocol.md](edison-m3b-protocol.md) | Stage B protocol, as frozen |
| [edison-learner.md](edison-learner.md) | The M1 network, trainer, league and GPU setup |
| [edison-search.md](edison-search.md), [edison-m2-protocol.md](edison-m2-protocol.md) | Search, PIMC and the M2 test |
| [edison-forward-model.md](edison-forward-model.md), [edison-native-net.md](edison-native-net.md) | State copies for search and the native forward pass |
| [edison-deck-labels.md](edison-deck-labels.md) | The interface defect that passed 567 tests, and the prompt differential |
| [edison-corpus.md](edison-corpus.md) | The 1,324-list corpus, legality and the privacy check |
| [edison-hardening.md](edison-hardening.md), [edison-certification.md](edison-certification.md) | Engine patches by revision, and every certification case |
| [edison-rules-conformance.md](edison-rules-conformance.md), [edison-trigger-order.md](edison-trigger-order.md) | Rules defects and the owner's rulings |
