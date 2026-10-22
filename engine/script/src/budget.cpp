#include "budget.hpp"

#include <string_view>

#include "lua_errors.hpp"
#include "messages.hpp"

namespace dwe::script::detail {
namespace {

void checkBudget(lua_State* state, lua_Debug* /*debug*/);

// The instructions come first: a call that ran out of them and is also late stops the same way on
// every machine.
BudgetStop stopCause(const Budget& budget, bool outOfTime) {
  if (budget.left <= 0) {
    return BudgetStop::Instructions;
  }
  return outOfTime ? BudgetStop::TimeCeiling : BudgetStop::None;
}

std::string_view stopReason(BudgetStop cause) {
  return cause == BudgetStop::TimeCeiling ? messages::ReachedTimeCeiling
                                          : messages::RanOutOfInstructions;
}

// From now on every instruction fails, so a script that catches the error with pcall cannot keep
// running: the first instruction it runs outside the pcall fails too, for the reason of the first
// stop.
int stopScript(lua_State* state, BudgetStop cause) {
  Budget& budget = budgetOf(state);
  if (!budget.stopped) {
    budget.stopped = true;
    budget.leftAtStop = budget.left;
    budget.lastStop = cause;
  }
  budget.left = 0;
  lua_sethook(state, checkBudget, LUA_MASKCOUNT, 1);
  return raiseError(state, {messages::RanTooLong, stopReason(budget.lastStop)});
}

// Charges the instructions the thread ran since its hook was armed or last fired: the count it was
// armed with, which is its own (a coroutine created from C++ starts with its creator's) and
// survives its yields and resumes.
void checkBudget(lua_State* state, lua_Debug* /*debug*/) {
  Budget& budget = budgetOf(state);
  budget.left -= lua_gethookcount(state);
  const BudgetStop cause = stopCause(budget, budget.pastDeadline());
  if (cause != BudgetStop::None) {
    stopScript(state, cause);
    return;
  }
  if (lua_gethookcount(state) != budget.checkInterval(state)) {
    budget.arm(state);
  }
}

} // namespace

Budget& budgetOf(lua_State* state) {
  return **static_cast<Budget**>(lua_getextraspace(state));
}

void Budget::attach() {
  *static_cast<Budget**>(lua_getextraspace(state)) = this;
}

void Budget::reset() {
  reset(static_cast<std::int64_t>(perCall), timePerCall);
}

void Budget::reset(std::int64_t instructions, std::chrono::steady_clock::duration time) {
  // Arming restarts the hook's count, so the instructions before its first check are paid up front.
  left = instructions - checkInterval(state);
  deadline = std::chrono::steady_clock::now() + time;
  sinceClockCheck = 0;
  stopped = false;
  leftAtStop = 0;
  lastStop = BudgetStop::None;
  running = state;
  linesPrinted = 0;
  arm(state);
}

void Budget::chargeAllocation(std::size_t bytes) {
  if (!chargesAllocations) {
    return;
  }
  left -= stepsForBytes(bytes);
  if (left <= 0) {
    arm(running);
  }
}

void Budget::arm(lua_State* thread) const {
  lua_sethook(thread, checkBudget, LUA_MASKCOUNT,
              left <= 0 ? 1 : static_cast<int>(checkInterval(thread)));
}

std::int64_t Budget::unspent() const {
  return stopped ? leftAtStop : left;
}

bool Budget::pastDeadline() const {
  return std::chrono::steady_clock::now() > deadline;
}

std::int64_t Budget::checkInterval(const lua_State* thread) const {
  return thread == state ? InstructionCheckInterval : CoroutineCheckInterval;
}

void chargeBudget(lua_State* state, std::int64_t steps) {
  Budget& budget = budgetOf(state);
  budget.left -= steps;
  budget.sinceClockCheck += steps;
  bool outOfTime = false;
  if (budget.sinceClockCheck >= InstructionCheckInterval) {
    budget.sinceClockCheck = 0;
    outOfTime = budget.pastDeadline();
  }
  const BudgetStop cause = stopCause(budget, outOfTime);
  if (cause != BudgetStop::None) {
    stopScript(state, cause);
  }
}

} // namespace dwe::script::detail
