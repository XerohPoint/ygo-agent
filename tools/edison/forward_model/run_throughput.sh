#!/usr/bin/env bash
# Throughput over a shuffled mix of all stages. At most 4 workers.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
B="$ROOT/build/forward_model"
R="$B/throughput.jsonl"
: > "$R"
cd "$ROOT/scripts"
uptime | tee -a "$B/throughput-load.txt"
for w in 1 2 4; do
  "$B/fm_bench" throughput --root .. --method arena --workers "$w" --seconds 15 --out "$R"
  "$B/fm_bench" throughput --root .. --method fork --workers "$w" --seconds 15 --out "$R"
  "$B/fm_bench" throughput --root .. --method replay --workers "$w" --seconds 15 --out "$R"
  "$B/fm_bench" throughput --root .. --method arena --workers "$w" --seconds 15 --encode 1 --out "$R"
  "$B/fm_bench" fork-conc --root .. --concurrent "$w" --seconds 15 --out "$R"
done
uptime | tee -a "$B/throughput-load.txt"
