#!/usr/bin/env bash
# Failure inside sampled worlds. Each case plants a real failure in a real card script, in a private
# script directory, so that it fires only while Heavy Storm is in hand during a Summon. The chosen game
# never does that on its recorded line. So the failure cannot occur in the true duel, only in sampled
# worlds that deal Heavy Storm to the opposing hand. Results go to void.jsonl.
#   lua     a Lua error in play
#   prompt  a prompt production refuses to show (it throws, as in docs/edison-deck-labels.md)
#   hang    an endless loop in Lua, stopped by the CPU watchdog
#   stack   runaway recursion in Lua
#   segv    no script: a real invalid write inside the guarded call, through a test hook
#   abort   no script: abort() inside the guarded call, as a failed native assert would
#   marker  no script: production's "Spec not found" line, printed inside the guarded call
#   flush   no script: the same line, then a flush, which must still be seen
#   printthrow    no script: the same line, then a throw. The line must not reach stdout later
#   outsidearena  no script: an endless loop with the arena off, as during a deal's planning
# Last, a control: a missed rollback of the history rings, which the clean comparison must catch.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
B="$ROOT/build/forward_model"
R="$B/void.jsonl"
CARD=19613556
SCRIPTS=/home/mada/ygopro-scripts
: > "$R"

make_overlay() {
  local name="$1" body="$2"
  local dir="$B/overlay-$name/script"
  mkdir -p "$dir"
  cp -s -f "$SCRIPTS"/* "$dir"/
  rm -f "$dir/c$CARD.lua"
  cat "$SCRIPTS/c$CARD.lua" > "$dir/c$CARD.lua"
  cat >> "$dir/c$CARD.lua" <<EOF

-- Planted by run_void.sh. Fires only while this card is in hand during a Summon.
local planted_initial_effect=c$CARD.initial_effect
function c$CARD.initial_effect(c)
	planted_initial_effect(c)
	local e=Effect.CreateEffect(c)
	e:SetType(EFFECT_TYPE_CONTINUOUS+EFFECT_TYPE_FIELD)
	e:SetCode(EVENT_SUMMON_SUCCESS)
	e:SetRange(LOCATION_HAND)
	e:SetOperation(function(e,tp,eg,ep,ev,re,r,rp) $body end)
	c:RegisterEffect(e)
end
EOF
}

run_case() {
  local name="$1" binary="$2"
  shift 2
  ( cd "$B/overlay-$name" && "$B/$binary" void --root "$ROOT" --plant "$name" --planted-card "$CARD" --seeds "${VOID_SEEDS:-256}" \
      --step-cpu-limit-ms 1000 "$@" 2> "$B/void-$name.stderr" | tee -a "$R" )
}

make_overlay lua 'error("planted failure in a sampled world")'
make_overlay prompt 'Duel.SelectMatchingCard(tp,aux.TRUE,tp,0,LOCATION_DECK,1,1,nil)'
make_overlay hang 'while true do end'
make_overlay stack 'local function f(n) return 1+f(n+1) end f(1)'
make_overlay segv ''
make_overlay abort ''
make_overlay marker ''
make_overlay flush ''
make_overlay printthrow ''
make_overlay outsidearena ''

run_case lua fm_bench
run_case prompt fm_bench
run_case hang fm_bench
run_case stack fm_bench
run_case segv fm_bench
run_case abort fm_bench
run_case marker fm_bench
run_case flush fm_bench
run_case printthrow fm_bench
run_case outsidearena fm_bench
run_case lua fm_bench_mutant --mutant rollback-history
