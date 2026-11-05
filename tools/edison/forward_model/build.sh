#!/usr/bin/env bash
# Builds the forward-model test bed from the certified xmake packages, without touching
# xmake.lua, the production module or the certification binary. No xmake is run.
#
# The core package is found by what its manifest says, never by a hash written here, and the
# build refuses every configuration except the certified one: early trigger on, hardening r2,
# release, static, no debug. The library's sha256 is then checked against the certified build,
# so changing the patch in place cannot go unnoticed.
#
# Usage: build.sh [target ...]
#   fm_bench           the test bed (default)
#   fm_bench_mutant    negative controls: planted leaks and missed rollbacks, chosen by --mutant
#   fm_search_example  a search-facing translation unit that includes only fm_world.h
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="$ROOT/tools/edison/forward_model"
PKG="$HOME/.xmake/packages"
EXPECT_HARDENING=r2
EXPECT_CORE_SHA256=e3adf73d2dbd6f6d144c0679bc00219debfacf3d610c6880fb6c01279bdb3d27

die() { echo "build.sh: REFUSED: $*" >&2; exit 1; }

grep -q "edison_early_trigger = true, edison_hardening = \"$EXPECT_HARDENING\"" "$ROOT/xmake.lua" \
  || die "xmake.lua no longer requires edison_hardening = \"$EXPECT_HARDENING\""

matches=()
for dir in "$PKG"/y/ygopro-core/0.0.2/*/; do
  m="$dir/manifest.txt"
  [ -f "$m" ] || continue
  grep -q 'edison_early_trigger = true' "$m" || continue
  grep -q "edison_hardening = \"$EXPECT_HARDENING\"" "$m" || continue
  grep -q 'debug = false' "$m" || continue
  grep -q 'shared = false' "$m" || continue
  grep -q 'mode = "release"' "$m" || continue
  matches+=("${dir%/}")
done
[ "${#matches[@]}" -eq 1 ] || die "need exactly one core package with early trigger and hardening $EXPECT_HARDENING, found ${#matches[@]}: ${matches[*]:-none}"
CORE="${matches[0]}"
got="$(sha256sum "$CORE/lib/libygopro-core.a" | cut -d' ' -f1)"
[ "$got" = "$EXPECT_CORE_SHA256" ] || die "core library $got is not the certified $EXPECT_CORE_SHA256"

# Lua must be the build the core was linked against, per the core's own manifest.
LUA_HASH="$(sed -n '/lua = {/,/}/s/.*buildhash = "\([0-9a-f]*\)".*/\1/p' "$CORE/manifest.txt")"
LUA_VER="$(sed -n '/lua = {/,/}/s/.*version = "\([^"]*\)".*/\1/p' "$CORE/manifest.txt")"
LUA="$PKG/l/lua/$LUA_VER/$LUA_HASH"
[ -d "$LUA" ] || die "the core's Lua package $LUA is missing"

FMT="$PKG/f/fmt/10.2.1/661aacf15d164223b9da55feb5616065"
GLOG="$PKG/g/glog/v0.6.0/98bb9424878c424382eb29e8237853f9"
GFLAGS="$PKG/g/gflags/v2.3.1/8338b247002044cbaa6bcca58a29a392"
SQL="$PKG/s/sqlite3/3.43.0+200/95fcc1aa9fa6461d859b22bc16ee0811"
SQLCPP="$PKG/s/sqlitecpp/3.2.1/933cfe1510514674b6adfaba68903085"
UD="$PKG/u/unordered_dense/v4.4.0/658322d51189425c89b903e244f839f8"
CQ="$PKG/c/concurrentqueue/v1.0.4/40b39cfb949741cfbb04b0eff8ec6596"
OUT="$ROOT/build/forward_model"
mkdir -p "$OUT"

INCLUDES=(-I"$ROOT/ygoenv" -I"$HERE" -I"$CORE/include" -I"$LUA/include" -I"$LUA/include/lua"
          -I"$FMT/include" -I"$GLOG/include" -I"$GFLAGS/include" -I"$SQL/include" -I"$SQLCPP/include"
          -I"$UD/include" -I"$CQ/include" -I"$CQ/include/concurrentqueue")
LIBS=("$CORE/lib/libygopro-core.a" "$LUA/lib/liblua.a" "$SQLCPP/lib/libSQLiteCpp.a"
      "$SQL/lib/libsqlite3.a" "$GLOG/lib/libglog.a" "$FMT/lib/libfmt.a"
      -L"$GFLAGS/lib" -lgflags -lpthread -ldl -lm)
FLAGS=(-std=c++17 -O3 -march=native -g -w -DGLOG_NO_EXPORT= -DGLOG_EXPORT=
       -DFM_CORE_PIN="\"$EXPECT_HARDENING:$EXPECT_CORE_SHA256\"")

targets=("$@")
[ "${#targets[@]}" -gt 0 ] || targets=(fm_bench)
for target in "${targets[@]}"; do
  case "$target" in
    fm_bench)
      g++ "${FLAGS[@]}" "${INCLUDES[@]}" "$HERE/fm_bench.cpp" -o "$OUT/fm_bench" "${LIBS[@]}" ;;
    fm_bench_mutant)
      g++ "${FLAGS[@]}" -DFM_MUTANTS=1 "${INCLUDES[@]}" "$HERE/fm_bench.cpp" -o "$OUT/fm_bench_mutant" "${LIBS[@]}" ;;
    fm_search_example)
      # Only fm_world.h on the search side: this file sees no core or environment type at all.
      g++ -std=c++17 -O2 -w -Wall -Wextra -I"$HERE" -c "$HERE/fm_search_example.cpp" -o "$OUT/fm_search_example.o"
      g++ "${FLAGS[@]}" "${INCLUDES[@]}" -c "$HERE/fm_world.cpp" -o "$OUT/fm_world.o"
      g++ "$OUT/fm_search_example.o" "$OUT/fm_world.o" -o "$OUT/fm_search_example" "${LIBS[@]}" ;;
    *) die "unknown target $target" ;;
  esac
  echo "built $OUT/$target (core $(basename "$CORE"), hardening $EXPECT_HARDENING)"
done
echo "$(basename "$CORE") $EXPECT_CORE_SHA256" > "$OUT/core-pin.txt"
