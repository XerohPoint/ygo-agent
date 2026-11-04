#!/usr/bin/env bash
# Builds build/forward_model/fm_bench from the pinned xmake packages, without
# touching xmake.lua, the production module or the certification binary.
# The core package is the production configuration (edison_early_trigger = true).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
PKG="$HOME/.xmake/packages"
CORE="$PKG/y/ygopro-core/0.0.2/bedb42dd1c644c7482fd8520cd0c023e"
grep -q 'edison_early_trigger = true' "$CORE/manifest.txt"
LUA="$PKG/l/lua/v5.5.1/3754d30faef64f908abcf74341f1054e"
FMT="$PKG/f/fmt/10.2.1/661aacf15d164223b9da55feb5616065"
GLOG="$PKG/g/glog/v0.6.0/98bb9424878c424382eb29e8237853f9"
GFLAGS="$PKG/g/gflags/v2.3.1/8338b247002044cbaa6bcca58a29a392"
SQL="$PKG/s/sqlite3/3.43.0+200/95fcc1aa9fa6461d859b22bc16ee0811"
SQLCPP="$PKG/s/sqlitecpp/3.2.1/933cfe1510514674b6adfaba68903085"
UD="$PKG/u/unordered_dense/v4.4.0/658322d51189425c89b903e244f839f8"
CQ="$PKG/c/concurrentqueue/v1.0.4/40b39cfb949741cfbb04b0eff8ec6596"
OUT="$ROOT/build/forward_model"
mkdir -p "$OUT"
g++ -std=c++17 -O3 -march=native -g -w -DGLOG_NO_EXPORT= -DGLOG_EXPORT= \
  -I"$ROOT/ygoenv" -I"$ROOT/tools/edison/forward_model" \
  -I"$CORE/include" -I"$LUA/include" -I"$LUA/include/lua" -I"$FMT/include" -I"$GLOG/include" \
  -I"$GFLAGS/include" -I"$SQL/include" -I"$SQLCPP/include" -I"$UD/include" -I"$CQ/include" \
  -I"$CQ/include/concurrentqueue" \
  "$ROOT/tools/edison/forward_model/fm_bench.cpp" -o "$OUT/fm_bench" \
  "$CORE/lib/libygopro-core.a" "$LUA/lib/liblua.a" "$SQLCPP/lib/libSQLiteCpp.a" \
  "$SQL/lib/libsqlite3.a" "$GLOG/lib/libglog.a" "$FMT/lib/libfmt.a" \
  -L"$GFLAGS/lib" -lgflags -lpthread -ldl -lm
echo "built $OUT/fm_bench"
