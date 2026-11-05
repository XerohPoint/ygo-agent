#!/usr/bin/env bash
# build.sh must refuse every core but the certified one. Each case points it at a scratch package tree,
# and no compiler runs: a pass here is a REFUSED line and a nonzero exit.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="$ROOT/tools/edison/forward_model"
REAL="$HOME/.xmake/packages/y/ygopro-core/0.0.2"
SCRATCH="$(mktemp -d)"
trap 'rm -rf "$SCRATCH"' EXIT
failures=0

try() {
  local name="$1"
  local output
  output="$(HOME="$SCRATCH/$name" bash "$HERE/build.sh" fm_bench 2>&1)"
  local code=$?
  if [ "$code" -ne 0 ] && grep -q "REFUSED" <<<"$output"; then
    echo "pass: $name: $(grep REFUSED <<<"$output" | cut -c1-110)"
  else
    echo "FAIL: $name built or failed for another reason: $(tail -1 <<<"$output" | cut -c1-110)"
    failures=1
  fi
}

package() {
  local name="$1" source="$2"
  mkdir -p "$SCRATCH/$name/.xmake/packages/y/ygopro-core/0.0.2"
  ln -s "$REAL/$source" "$SCRATCH/$name/.xmake/packages/y/ygopro-core/0.0.2/$source"
}

R1=aa797b55e1fc45fdb959732181b1e231
OLD=bedb42dd1c644c7482fd8520cd0c023e
NO_TRIGGER=3d3542c926ac4002a490d7c8de06787e
R2=5fe29f6ad6dd4ebb83a1f1b5123c1837

package only_old_core "$OLD"
package only_hardening_r1 "$R1"
package only_no_early_trigger "$NO_TRIGGER"
mkdir -p "$SCRATCH/no_packages/.xmake/packages/y/ygopro-core/0.0.2"

# The right manifest on the wrong library: what changing a patch in place would look like.
mkdir -p "$SCRATCH/wrong_library/.xmake/packages/y/ygopro-core/0.0.2/$R2/lib"
cp "$REAL/$R2/manifest.txt" "$SCRATCH/wrong_library/.xmake/packages/y/ygopro-core/0.0.2/$R2/manifest.txt"
cp "$REAL/$R1/lib/libygopro-core.a" "$SCRATCH/wrong_library/.xmake/packages/y/ygopro-core/0.0.2/$R2/lib/libygopro-core.a"

# Two packages claiming the certified configuration.
package two_matches "$R2"
ln -s "$REAL/$R2" "$SCRATCH/two_matches/.xmake/packages/y/ygopro-core/0.0.2/copy"

for name in only_old_core only_hardening_r1 only_no_early_trigger no_packages wrong_library two_matches; do try "$name"; done
exit "$failures"
