#!/bin/bash
# Latency of edison_net.h on the real mix, single thread, pinned to one CPU, with the load
# recorded before and after each pass. Run after run_differential.py has written the data.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="$ROOT/build/edison-native-net"
CPU="${1:-5}"
SEED=1702
META=/home/mada/ygo-agent/build/edison-retrain-2026-11-04/runs/gauntlet-$SEED/checkpoint-2560.json
PY=/home/mada/ygo-agent/.venv/bin/python
PARAMS=$("$PY" -c "import json,sys;print(json.load(open(sys.argv[1]))['params_sha256'])" "$META")
CODES=$("$PY" -c "import json,sys;print(json.load(open(sys.argv[1]))['codes_sha256'])" "$META")
for run in 1 2 3; do
  echo "load before run $run: $(cat /proc/loadavg)"
  taskset -c "$CPU" "$OUT/bin/net_bench" "$OUT/gauntlet-$SEED.ednet" "$PARAMS" "$CODES" "$OUT/data/decisions.bin" "$OUT/bench-$run.json" 20000 3
  echo "load after run $run:  $(cat /proc/loadavg)"
done
