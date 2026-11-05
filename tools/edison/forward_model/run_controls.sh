#!/usr/bin/env bash
# Negative controls. A check that passes on a correct build and on a broken one proves nothing, so each
# planted fault must be caught. Results go to controls.jsonl next to the clean runs they compare with.
#   R4 must catch: a deal that reads the true opposing hand, a deal whose seed is mixed with that hand,
#                  and a deal whose pool keeps the true slot order.
#   The snapshot check must catch: missed rollback of the history rings, their cursors, reveal memory,
#                  the Deck binder's ranks, and the core's random stream.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
B="$ROOT/build/forward_model"
R="$B/controls.jsonl"
: > "$R"
cd "$ROOT/scripts"
for leak in leak-hand leak-seed leak-order; do
  "$B/fm_bench_mutant" redeal --root .. --mutant "$leak" --per-cell "${CONTROL_PER_CELL:-1}" --deals 6 --cap 3000 --workers 4 \
    --log "$B/control-$leak.log" 2> "$B/control-$leak.stderr" | tee -a "$R"
done
for miss in rollback-history rollback-cursor rollback-reveal rollback-bind rollback-rng; do
  "$B/fm_bench_mutant" correct --root .. --mutant "$miss" --per-cell "${CONTROL_PER_CELL:-1}" --workers 4 --cap 3000 \
    --log "$B/control-$miss.log" 2> "$B/control-$miss.stderr" | tee -a "$R"
done
