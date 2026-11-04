#!/bin/bash
# Compile script_probe against the Edison-patched core package. Does not touch production.
set -euo pipefail
ROOT=/home/mada/ygo-agent
OUT=$ROOT/build/edison-pool-audit-2026-11-04
PK=$HOME/.xmake/packages
CORE=$PK/y/ygopro-core/0.0.2/bedb42dd1c644c7482fd8520cd0c023e
LUA=$PK/l/lua/v5.5.1/3754d30faef64f908abcf74341f1054e
SQLCPP=$PK/s/sqlitecpp/3.2.1/933cfe1510514674b6adfaba68903085
SQLITE=$PK/s/sqlite3/3.43.0+200/95fcc1aa9fa6461d859b22bc16ee0811
g++ -std=c++17 -O1 \
  -I"$ROOT/ygoenv/ygoenv" -I"$CORE/include" -I"$LUA/include" -I"$LUA/include/lua" -I"$SQLCPP/include" -I"$SQLITE/include" \
  "$ROOT/tools/edison/pool_audit/script_probe.cpp" -o "$OUT/script_probe" \
  -L"$CORE/lib" -L"$LUA/lib" -L"$SQLCPP/lib" -L"$SQLITE/lib" \
  -lygopro-core -llua -lSQLiteCpp -lsqlite3 -lpthread -ldl
