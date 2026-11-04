#!/usr/bin/env bash
# Same replay, alternating allocators, so machine load affects both about equally.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
B="$ROOT/build/forward_model"
cd "$ROOT/scripts"
uptime
for round in 1 2 3; do
  "$B/fm_bench" replay --root .. --reps 2 --heap glibc | grep -E 'engine_decisions|early/create_duel'
  "$B/fm_bench" replay --root .. --reps 2 --heap arena | grep -E 'engine_decisions|early/create_duel'
done
uptime
