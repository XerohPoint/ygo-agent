#!/bin/bash
# Native test bed for edison_net.h, g++ directly. Output goes to build/edison-native-net/bin.
# `build.sh` builds the real targets; `build.sh faults` also builds one net_diff per injected fault.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="$ROOT/build/edison-native-net/bin"
mkdir -p "$OUT"
FLAGS=(-std=c++17 -O3 -march=native -Wall -Wextra -Wno-psabi "-I$HERE")
g++ "${FLAGS[@]}" "$HERE/net_diff.cpp" -o "$OUT/net_diff"
g++ "${FLAGS[@]}" "$HERE/net_bench.cpp" -o "$OUT/net_bench"
g++ "${FLAGS[@]}" "-I$HERE/../forward_model" "$HERE/net_arena_test.cpp" -o "$OUT/net_arena_test"
g++ "${FLAGS[@]}" "$HERE/net_load_test.cpp" -o "$OUT/net_load_test"
if [ "${1:-}" = "faults" ]; then
  for fault in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    g++ "${FLAGS[@]}" "-DEDNET_FAULT=$fault" "$HERE/net_diff.cpp" -o "$OUT/net_diff_fault$fault" &
    if [ $((fault % 4)) = 0 ]; then wait; fi
  done
  wait
fi
