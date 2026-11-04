#!/usr/bin/env bash
# Replay every recorded game under different processes, heaps and encoders.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
B="$ROOT/build/forward_model"
R="$B/results.jsonl"
cd "$ROOT/scripts"
"$B/fm_bench" verify --root .. --chains "$B/chains-glibc-deep.txt" --out "$R"
"$B/fm_bench" verify --root .. --chains "$B/chains-glibc-deep2.txt" --out "$R"
"$B/fm_bench" verify --root .. --heap perturb --chains "$B/chains-perturb-deep.txt" --out "$R"
"$B/fm_bench" verify --root .. --heap arena --chains "$B/chains-arena-deep.txt" --out "$R"
"$B/fm_bench" verify --root .. --encode 1 --chains "$B/chains-encode-deep.txt" --out "$R"
"$B/fm_bench" verify --root .. --deep 0 --chains "$B/chains-glibc-nodeep.txt" --out "$R"
"$B/fm_bench" verify --root .. --deep 0 --heap arena --chains "$B/chains-arena-nodeep.txt" --out "$R"
cd "$B"
echo "== full digests; these must agree with each other =="
md5sum chains-glibc-deep.txt chains-glibc-deep2.txt chains-perturb-deep.txt chains-arena-deep.txt chains-encode-deep.txt
echo "== decision digest only; deep hashing must not change decisions =="
awk '{print $1,$2,$3,$5,$6}' chains-glibc-deep.txt | md5sum
awk '{print $1,$2,$3,$5,$6}' chains-glibc-nodeep.txt | md5sum
awk '{print $1,$2,$3,$5,$6}' chains-arena-nodeep.txt | md5sum
