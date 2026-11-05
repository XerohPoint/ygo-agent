#!/usr/bin/env bash
# The search API from outside: contract checks through a program that includes only fm_world.h, and
# proof by compiler that such a program cannot name what it must not reach.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="$ROOT/tools/edison/forward_model"
B="$ROOT/build/forward_model"
PROBE="$B/api-probe"
mkdir -p "$PROBE"

# Each of these must fail to compile against fm_world.h alone.
refuse=0
try() {
  local name="$1" body="$2"
  printf '#include "fm_world.h"\nvoid probe(fm::SearchWorld &world, fm::Referee &duel) { %s }\n' "$body" > "$PROBE/$name.cpp"
  if g++ -std=c++17 -fsyntax-only -I"$HERE" "$PROBE/$name.cpp" 2> /dev/null; then
    echo "FAIL: $name compiles"
    refuse=1
  else
    echo "pass: $name does not compile"
  fi
}
printf '#include "fm_world.h"\nint probe(fm::SearchWorld &world) { return world.option_count(); }\n' > "$PROBE/control.cpp"
g++ -std=c++17 -fsyntax-only -I"$HERE" "$PROBE/control.cpp" || { echo "FAIL: the control does not compile, so the refusals below prove nothing"; refuse=1; }
try core_type        'duel *d = nullptr;'
try card_type        'card *c = nullptr;'
try environment_type 'ygopro::YGOProEnvImpl *e = nullptr;'
try world_internals  'auto &i = world.impl_;'
try referee_internals 'auto &i = duel.impl_;'
try world_to_referee 'fm::Referee &r = world.referee();'
try true_cards       'auto c = world.true_code(0);'
try test_hooks       'fm::TestAccess::env(world);'

cat > "$PROBE/lists.py" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1] + "/tools/edison/learner")
import decks
for name in sys.argv[2:]:
    codes = decks.parse_ydk(open(decks.registry()[name]).read())["main"]
    print(len(codes), *codes)
EOF
DECK0="${DECK0:-g_lightsworn}"
DECK1="${DECK1:-blackwing}"
/home/mada/ygo-agent/.venv/bin/python "$PROBE/lists.py" "$ROOT" "$DECK0" "$DECK1" > "$PROBE/lists.txt"
bash "$HERE/build.sh" fm_search_example
cd "$ROOT/scripts"
"$B/fm_search_example" "$ROOT" "$DECK0" "$DECK1" 85450000 "$PROBE/lists.txt"
exit "$refuse"
