#!/usr/bin/env bash
# Production observation parity, branch-point correctness, hidden-state tests.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
B="$ROOT/build/forward_model"
R="$B/correct.jsonl"
PY=/home/mada/ygo-agent/.venv/bin/python
: > "$R"
cd "$ROOT/scripts"
"$B/fm_bench" obsdump --root .. --limit-games 32 --dump "$B/obs.bin"
"$PY" "$ROOT/tools/edison/forward_model/check_observations.py" --dump "$B/obs.bin" --games-per-cell 32 | tee -a "$R"
rm -f "$B/obs.bin"
"$B/fm_bench" correct --root .. --positions "${POSITIONS:-110}" --workers 4 --cap 3000 --log "$B/correct.log" --out "$R"
"$B/fm_bench" redeal --root .. --positions "${REDEAL_POSITIONS:-60}" --deals 6 --cap 3000 --workers 4 --out "$R" 2> "$B/redeal.stderr"
"$B/fm_bench" construct --root .. --positions 400 --out "$R" 2> "$B/construct.stderr"
"$B/fm_bench" replay --root .. --reps 3 --heap arena --out "$R"
wc -l "$B/redeal.stderr" "$B/construct.stderr"
