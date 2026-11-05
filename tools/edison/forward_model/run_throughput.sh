#!/usr/bin/env bash
# The search loop's cost through the search API: restore + deal + step + observe, per worker, on 1, 2 and
# 4 worker processes. Then full random playouts by the older bench, with and without the encoder, for
# comparison with the old core's table. At most 4 workers. Each result carries the load average.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
B="$ROOT/build/forward_model"
R="$B/throughput.jsonl"
: > "$R"
cd "$ROOT/scripts"
for w in 1 2 4; do
  "$B/fm_bench" node --root .. --workers "$w" --seconds "${SECONDS_PER_CELL:-15}" --out "$R"
done
for w in 1 2 4; do
  echo "load $(cut -d' ' -f1-3 /proc/loadavg)" >> "$B/throughput-load.txt"
  "$B/fm_bench" throughput --root .. --method arena --workers "$w" --seconds "${SECONDS_PER_CELL:-15}" --out "$R"
  "$B/fm_bench" throughput --root .. --method arena --workers "$w" --seconds "${SECONDS_PER_CELL:-15}" --encode 1 --out "$R"
done
