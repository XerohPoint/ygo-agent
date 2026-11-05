#!/usr/bin/env bash
# Does a sampled world's distribution depend on true load order? Natural duels from rigged Decks, one public history per
# line, each load-order variant with its own deal seeds. check_results.py runs the homogeneity tests.
#   none       the real pick
#   first      a Lua plant in this run only: take the group's first card, which reads load order. The test must see it
#   min2       a weak plant: draw twice, keep whichever comes first in the group. How small a lean the test sees
#   canonical  an estimate of the fix not made: number every card by its place at deal time. Not in any search build
# Then the same estimate over the corpus, on the R3, R4 and R4W comparisons. Its hard-rule counters are not meaningful,
# since public memory is indexed by the number the estimate replaces.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
B="$ROOT/build/forward_model"
R="$B/loadorder.jsonl"
: > "$R"
cd "$ROOT/scripts"
run() { "$B/fm_bench" loadorder --root .. --variants 16 --workers 4 "$@" 2>> "$B/loadorder.stderr" | tee -a "$R"; }
: > "$B/loadorder.stderr"
: > "$B/renumber.jsonl"
for plant in none first min2; do
  run --line thestalos_viewer_hand --seeds "${LOAD_SEEDS_A:-4000}" --plant "$plant"
  run --line thestalos_opposing_hand --seeds "${LOAD_SEEDS_B:-1500}" --plant "$plant"
  run --line power_tool_viewer_deck --seeds "${LOAD_SEEDS_C:-1000}" --plant "$plant"
done
run --line thestalos_viewer_hand --seeds 400 --canonical-numbering 1
run --line thestalos_opposing_hand --seeds 400 --canonical-numbering 1
run --line power_tool_viewer_deck --seeds 400 --canonical-numbering 1
"$B/fm_bench" redeal --root .. --per-cell "${RENUMBER_PER_CELL:-8}" --deals 6 --cap 3000 --workers 4 --canonical-numbering 1 --log "$B/renumber.log" --out "$B/renumber.jsonl" 2> "$B/renumber.stderr"
grep -h '^EXCEPTION ' "$B"/renumber.log.* | sed 's/^EXCEPTION //' > "$B/renumber-exceptions.jsonl"
