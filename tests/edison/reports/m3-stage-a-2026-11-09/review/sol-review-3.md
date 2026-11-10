# M3 final confirmation

Reviewed only `50a784d..c077422`, at detached HEAD `c077422`.

**Findings: none.** I found no REACHABLE problem or incorrect fix in this diff.

Confirmed:

- `repair_journal()` removes an incomplete tail or terminates a complete final record before appending. `read_journal()` rejects malformed newline-terminated records. The cut-start, cut-result and missing-final-newline resume/reread selftests pass, preserve stored results and retain the appropriate unresolved start.
- Rows now combine first-run and replay interruptions. Validation checks the individual counts, their sum and replay agreement; suspect wins use the combined count. Analysis reports both interruption totals. The replay-only interruption selftest confirms that its cell's wins enter the worst case.
- Undefined sampled build gaps receive -2 rather than being omitted; undefined pooled rates receive 0. An independent artificial-count check confirmed those exact fallback values and the undefined-value count (150 expected and observed). The sparse-mirror regression also passes.
- `check_chunk()` verifies the journal hash and exact reconstruction of the chunk from its journal, and both resume and result loading call it. The added tests reject edited results and a missing journal. Empty F is handled without the previous reshape failure.
- The corpus document now distinguishes the Side-in counting predicates, and the privacy wording distinguishes numeric JSON values from digit strings.

Validation: the full permitted `stage_a.py selftest` exited 0. Its intentional fake-worker interruption prints the expected thread traceback. All additional calculations used synthetic counts. No actual duel, corpus win-rate calculation, network/search play, or prohibited Stage A command was run. Python used the required WSL interpreter and thread limits; temporary files stayed inside this worktree. No source or read-only dependency was edited, no commit was made, and tracked files remain unchanged.

**Freeze status: nothing in this diff blocks a freeze.**
