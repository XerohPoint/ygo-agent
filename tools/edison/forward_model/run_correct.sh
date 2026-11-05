#!/usr/bin/env bash
# Snapshot correctness across all 21 lists, the static-data audit, and hidden-state tests R1-R5.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
B="$ROOT/build/forward_model"
R="$B/correct.jsonl"
PY=/home/mada/ygo-agent/.venv/bin/python
: > "$R"
cd "$ROOT/scripts"
"$B/fm_bench" correct --root .. --per-cell "${CORRECT_PER_CELL:-4}" --workers 4 --cap 3000 --log "$B/correct.log" --out "$R"
"$B/fm_bench" statics --root .. --game 100 --index 40 > "$B/statics.txt"
"$PY" "$ROOT/tools/edison/forward_model/check_statics.py" "$B/fm_bench" "$B/statics.txt" | tee -a "$R"
"$B/fm_bench" redeal --root .. --per-cell "${REDEAL_PER_CELL:-16}" --deals 6 --cap 3000 --workers 4 --log "$B/redeal.log" --out "$R" 2> "$B/redeal.stderr"
"$B/fm_bench" uniform --root .. --per-cell 2 --deals 4000 --out "$R"
"$B/fm_bench" rules --root .. --out "$R" 2> "$B/rules-recorded.stderr"
"$B/fm_bench" rules --root .. --policy random --out "$R" 2> "$B/rules-random.stderr"
"$B/fm_bench" setfixture --root .. --out "$R"
"$B/fm_bench" handtrap --root .. --log "$B/handtrap.log" --out "$R"
"$PY" "$ROOT/tools/edison/forward_model/check_set_rules.py" | tee -a "$R"
"$B/fm_bench" construct --root .. --positions 400 --out "$R" 2> "$B/construct.stderr"
wc -l "$B/redeal.stderr" "$B/construct.stderr"
