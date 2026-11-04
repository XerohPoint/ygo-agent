#!/usr/bin/env bash
# Latency, memory and throughput for each transport. At most 4 workers.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
B="$ROOT/build/forward_model"
R="$B/costs.jsonl"
: > "$R"
cd "$ROOT/scripts"
uptime | tee -a "$B/costs-load.txt"
"$B/fm_bench" replay --root .. --reps 3 --out "$R"
"$B/fm_bench" replay --root .. --reps 3 --encode 1 --out "$B/costs-encode.jsonl"
"$B/fm_bench" arena --root .. --positions 40 --out "$R"
"$B/fm_bench" fork --root .. --positions 40 --out "$R"
"$B/fm_bench" fork --root .. --positions 15 --ballast-mb 200 --out "$R"
"$B/fm_bench" fork --root .. --positions 15 --ballast-mb 1000 --out "$R"
"$B/fm_bench" forkmem --root .. --children 200 --out "$R"
uptime | tee -a "$B/costs-load.txt"
