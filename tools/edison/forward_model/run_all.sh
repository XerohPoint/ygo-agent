#!/usr/bin/env bash
# Everything in docs/edison-forward-model.md's newest section, in order. About 25 minutes on 4 threads.
# Uses seeds 85100000-85141015 for games, and game index 85150000 in run_api.sh. See collect_corpus.py.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="$ROOT/tools/edison/forward_model"
B="$ROOT/build/forward_model"
mkdir -p "$B/corpus"
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
bash "$HERE/build.sh" fm_bench fm_bench_mutant || exit 1
sha256sum "$B/corpus/digests.txt" 2> /dev/null | tee "$B/corpus-before.sha" || true
( cd "$ROOT/scripts" && /home/mada/ygo-agent/.venv/bin/python "$HERE/collect_corpus.py" --games-per-cell 16 --threads 4 | tail -1 ) || exit 1
sha256sum "$B/corpus/digests.txt"
bash "$HERE/run_verify.sh" > "$B/verify.out" 2>&1
bash "$HERE/run_correct.sh" > "$B/correct.out" 2>&1
bash "$HERE/run_controls.sh" > "$B/controls.out" 2>&1
VOID_SEEDS=512 bash "$HERE/run_void.sh" > "$B/void.out" 2>&1
bash "$HERE/run_api.sh" > "$B/api.out" 2>&1
echo "api exit $?"
bash "$HERE/run_throughput.sh" > "$B/throughput.out" 2>&1
echo finished
