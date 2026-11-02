#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "ygopro-core/duel.h"
#include "ygopro-core/field.h"
#include "ygopro-core/effect.h"
#include "ygopro-core/card.h"

namespace ygopro {
// This is the pinned core's waiting execution frame, not an inferred effect
// category. Lua target functions also perform non-targeting activation setup.
enum class DecisionStage { Unknown=0, Cost=1, Activation=2, Operation=3 };
struct ExecutionContext {
  DecisionStage stage = DecisionStage::Unknown;
  card* source = nullptr;
};
inline ExecutionContext decision_execution_context(intptr_t handle) {
  if (!handle) return {};
  const auto& units = reinterpret_cast<duel*>(handle)->game_field->core.units;
  for (const auto& unit : units) {
    DecisionStage stage;
    switch (unit.type) {
      case PROCESSOR_EXECUTE_COST: stage=DecisionStage::Cost; break;
      case PROCESSOR_EXECUTE_TARGET: stage=DecisionStage::Activation; break;
      case PROCESSOR_EXECUTE_OPERATION: stage=DecisionStage::Operation; break;
      default: continue;
    }
    // Step zero is scheduled but has not started executing its Lua callback.
    if (unit.step == 0) continue;
    return {stage, unit.peffect ? unit.peffect->get_handler() : nullptr};
  }
  return {};
}

struct DecisionContext {
  // Only standard UI selection hints are exposed. Larger hint payloads can be
  // private card passcodes and must never enter a public observation.
  std::array<int,2> pending_hints{};
  int hint=0, minimum=0, maximum=0, sum_target=0;
  bool active=false;
  std::vector<std::string> selected, mandatory, candidates;
  void clear_selection() {
    hint=minimum=maximum=sum_target=0;
    active=false;
    selected.clear(); mandatory.clear(); candidates.clear();
  }
  void reset() { clear_selection(); pending_hints.fill(0); }
  void hint_for(int player, uint32_t value) {
    if (player >= 0 && player < 2)
      pending_hints[player] = value >= 500 && value <= 575 ? int(value) : 0;
  }
  void begin(int player, int message, int min, int max) {
    clear_selection();
    active=true; minimum=min; maximum=max;
    if (player >= 0 && player < 2) hint=pending_hints[player];
    // Consume at the next prompt, including a prompt for the other player.
    pending_hints.fill(0);
    if (!hint && message==MSG_SELECT_TRIBUTE) hint=531;
  }
  bool knows_candidate(const std::string& spec) const {
    if (!active) return false;
    return std::find(candidates.begin(),candidates.end(),spec)!=candidates.end() ||
           std::find(selected.begin(),selected.end(),spec)!=selected.end() ||
           std::find(mandatory.begin(),mandatory.end(),spec)!=mandatory.end();
  }
};
}
