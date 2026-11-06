#pragma once

#include <cstdint>

namespace dwe::script {

// Why the budget stopped a script (ScriptVm::lastBudgetStop).
enum class BudgetStop : std::uint8_t {
  // Nothing was stopped.
  None,
  // It ran all its instructions. The count is the same on every machine and under any load: this is
  // how the budget
  // stops a runaway script.
  Instructions,
  // It reached the time ceiling first. Only work that the instruction count does not see should get
  // there, such as
  // comparing big texts; the ceiling is a loose safety net and depends on the machine, not on its
  // load: it counts the processor time the script ran, without the engine's published functions.
  TimeCeiling,
};

} // namespace dwe::script
