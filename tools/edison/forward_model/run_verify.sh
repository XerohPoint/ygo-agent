#!/usr/bin/env bash
# Production equivalence, on the corpus collect_corpus.py recorded through the certified module:
# acting player, option count, winner, win reason and every observation field at every decision of
# every game. Then whole-game digests across processes, heaps and allocators.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
B="$ROOT/build/forward_model"
R="$B/results.jsonl"
: > "$R"
cd "$ROOT/scripts"
"$B/fm_bench" verify --root .. --digests "$B/corpus/digests.txt" --hash 0 --check-stamp 1 --out "$R"
"$B/fm_bench" verify --root .. --chains "$B/chains-glibc-deep.txt" --out "$R"
"$B/fm_bench" verify --root .. --chains "$B/chains-glibc-deep2.txt" --out "$R"
"$B/fm_bench" verify --root .. --heap perturb --chains "$B/chains-perturb-deep.txt" --out "$R"
"$B/fm_bench" verify --root .. --heap arena --chains "$B/chains-arena-deep.txt" --out "$R"
"$B/fm_bench" verify --root .. --encode 1 --chains "$B/chains-encode-deep.txt" --out "$R"
"$B/fm_bench" verify --root .. --deep 0 --chains "$B/chains-glibc-nodeep.txt" --out "$R"
"$B/fm_bench" verify --root .. --deep 0 --heap arena --chains "$B/chains-arena-nodeep.txt" --out "$R"
cd "$B"
for name in glibc-deep glibc-deep2 perturb-deep arena-deep encode-deep glibc-nodeep arena-nodeep; do
  cat "chains-$name.txt".* | sort -n > "chains-$name.sorted"
done
echo "== full digests: decision, deep, observation. These must agree with each other =="
md5sum chains-glibc-deep.sorted chains-glibc-deep2.sorted chains-perturb-deep.sorted chains-arena-deep.sorted chains-encode-deep.sorted
echo "== decision digest only: hashing and allocator must not change decisions =="
awk '{print $1,$2,$3,$6,$7}' chains-glibc-deep.sorted | md5sum
awk '{print $1,$2,$3,$6,$7}' chains-glibc-nodeep.sorted | md5sum
awk '{print $1,$2,$3,$6,$7}' chains-arena-nodeep.sorted | md5sum
