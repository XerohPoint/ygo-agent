#!/usr/bin/env bash
# Everything in docs/edison-forward-model.md's newest section, in order, then the verdict: this script's exit code is
# check_results.py's. About 1 hour 45 minutes on 4 threads on a machine with other work running.
# Game seeds: 85600000-85641015 for the corpus, 85650002 and 85650100 upward for fixtures, 85650000 for the API check,
# 85660000-85660002 for the load-order lines.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="$ROOT/tools/edison/forward_model"
B="$ROOT/build/forward_model"
PY=/home/mada/ygo-agent/.venv/bin/python
mkdir -p "$B/corpus"
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
bash "$HERE/build.sh" fm_bench fm_bench_mutant || exit 1
bash "$HERE/test_build_refusal.sh" > "$B/build_refusal.out" 2>&1
( cd "$ROOT/scripts" && "$PY" "$HERE/collect_corpus.py" --games-per-cell 16 --threads 4 2> /dev/null | tail -1 ) || exit 1
bash "$HERE/run_verify.sh" > "$B/verify.out" 2>&1
bash "$HERE/run_correct.sh" > "$B/correct.out" 2>&1
bash "$HERE/run_controls.sh" > "$B/controls.out" 2>&1
bash "$HERE/run_loadorder.sh" > "$B/loadorder.out" 2>&1
VOID_SEEDS=512 bash "$HERE/run_void.sh" > "$B/void.out" 2>&1
bash "$HERE/run_api.sh" > "$B/api.out" 2>&1
bash "$HERE/run_throughput.sh" > "$B/throughput.out" 2>&1
"$PY" "$HERE/check_results.py" "$B"
verdict=$?
if [ "$verdict" -eq 0 ]; then
  "$PY" "$HERE/test_checker.py"
  verdict=$?
fi
echo "verdict $verdict"
exit "$verdict"
