#pragma once

#include <dwe/script/budget_stop.hpp>

#include <lua.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ratio>

namespace dwe::script::detail {

// The processor time the calling thread has run: it stands still while the thread waits for a core,
// so a loaded machine does not bring a script closer to its time ceiling. Where the system has no
// such clock it is the steady clock.
struct ThreadCpuClock {
  using rep = std::int64_t;
  using period = std::nano;
  using duration = std::chrono::nanoseconds;
  using time_point = std::chrono::time_point<ThreadCpuClock>;
  static constexpr bool is_steady = true;

  static time_point now() noexcept;
};

// The time ceiling of a call: the processor time its thread may run. The steady clock's ceiling is
// a cheap first test: a thread never runs longer than the time that passed, so the processor clock
// is read only once the steady one is past. The engine's own work inside a call (a published
// function loading a model) does not count: only postpone() moves the ceiling, for that work.
class TimeCeiling {
public:
  // The ceiling `time` from now.
  [[nodiscard]] static TimeCeiling after(std::chrono::steady_clock::duration time);

  [[nodiscard]] bool passed() const;

  // The same ceiling, `time` later.
  [[nodiscard]] TimeCeiling postponed(std::chrono::steady_clock::duration time) const;

  // Whichever of the two comes first.
  [[nodiscard]] static TimeCeiling earlier(const TimeCeiling& first, const TimeCeiling& second);

private:
  std::chrono::steady_clock::time_point steady_;
  ThreadCpuClock::time_point processor_;
};

// What the running call may still spend: instructions, counted by a Lua count hook on each thread,
// and time, a loose ceiling (TimeCeiling) read at the same checks. The state's extra space points
// to it, so the coroutines a script creates share it.
struct Budget {
  lua_State* state = nullptr;
  std::uint64_t perCall = 0;
  std::chrono::milliseconds timePerCall{};
  std::int64_t left = 0;
  TimeCeiling deadline;
  // Steps charged by C code since the clock was last read.
  std::int64_t sinceClockCheck = 0;
  // The thread running now: the main one, or the coroutine it resumed.
  lua_State* running = nullptr;
  // Kept here because API closures reach the VM only through the state's extra space. Only this
  // coroutine may yield from a waiting function: a script's own coroutine would skip the wait.
  lua_State* stepped = nullptr;
  bool waited = false;
  // True while an engine call into Lua runs: only then do allocations cost budget.
  bool chargesAllocations = false;
  // Lines the call printed, to keep a looping script from flooding the log.
  std::size_t linesPrinted = 0;
  // Whether the call was stopped, and what it had left then: stopping on time empties left, which
  // must not count as instructions spent.
  bool stopped = false;
  std::int64_t leftAtStop = 0;
  // Why the latest stop of the outermost call happened; a nested call that goes on after its stop
  // keeps it.
  BudgetStop lastStop = BudgetStop::None;

  // Points the state's extra space at this budget. Coroutines copy it when they are created.
  void attach();

  // Gives the next call the whole budget.
  void reset();

  // Gives the next call a budget of its own: a hook's, smaller than a call's. The instructions the
  // main thread runs before the hook's first check are paid up front, as a coroutine's are on every
  // resume.
  void reset(std::int64_t instructions, std::chrono::steady_clock::duration time);

  [[nodiscard]] bool pastDeadline() const;

  // The instructions the call has not spent: what it had when it was stopped, if it was.
  [[nodiscard]] std::int64_t unspent() const;

  // Charges the bytes the allocator reserves, so C functions that build big strings or tables
  // (string.rep, table.concat, `..`) pay for them. It never raises, since the allocator may run
  // anywhere: when the budget runs out, the next instruction of the running thread stops the
  // script. Only `running` is armed, and only coroutine.resume and ScriptCoroutine::resume set it
  // to a coroutine; reset() and a nested CallScope (a hook's handlers) point it back at the main
  // thread.
  void chargeAllocation(std::size_t bytes);

  // Sets the hook of a thread about to run: every instruction stops it if the budget ran out, else
  // the regular check of that thread (a coroutine's is shorter, see CoroutineCheckInterval). The
  // hook belongs to the thread and keeps its count across yields and resumes; each check charges
  // the count it was armed with.
  void arm(lua_State* thread) const;

  // The instructions between two checks of a thread's budget.
  [[nodiscard]] std::int64_t checkInterval(const lua_State* thread) const;
};

// How often, in instructions, the budget of the main thread is checked. Short, because one
// instruction may run a C function (comparing two long strings) that the hook cannot interrupt.
inline constexpr std::int64_t InstructionCheckInterval = 1'000;

// How often a coroutine's budget is checked. Setting a hook restarts its count, and resuming a
// coroutine sets it, so a coroutine that yields before the count runs out would run for free; every
// resume pays this many steps up front instead, and the count is short so that payment stays small.
inline constexpr std::int64_t CoroutineCheckInterval = 64;

// The budget of the call running in a state or any of its coroutines.
Budget& budgetOf(lua_State* state);

// The bytes that C code compares or copies for the cost of one Lua instruction: moving memory is
// much cheaper.
inline constexpr std::size_t BytesPerStep = 64;

// The steps that C code reading or writing bytes costs.
constexpr std::int64_t stepsForBytes(std::size_t bytes) {
  return static_cast<std::int64_t>((bytes + BytesPerStep - 1) / BytesPerStep);
}

// Charges work that a C function does in a loop, where the instruction hook cannot run: string
// patterns, copies of strings and moves of table elements. Raises the budget's error in the script
// when the budget or the time runs out.
void chargeBudget(lua_State* state, std::int64_t steps);

} // namespace dwe::script::detail
