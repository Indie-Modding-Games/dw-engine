#include "vm_state.hpp"

#include <algorithm>
#include <chrono>
#include <functional>

#include "lua_errors.hpp"

namespace dwe::script::detail {
VmState::VmState(const ScriptLimits& limits)
    : memory{.used = 0,
             .limit = limits.memoryBytes,
             .scriptLimit = limits.memoryBytes - (limits.memoryBytes / EngineReserveDivisor),
             .packLimit = limits.packMemoryBytes,
             .largestBlock = limits.largestBlockBytes,
             .owner = EngineMemory,
             .usedByOwner = {0},
             .budget = &budget},
      budget{.state = nullptr,
             .perCall = limits.instructionsPerCall,
             .timePerCall = limits.callDeadline,
             .left = 0,
             .deadline = {},
             .sinceClockCheck = 0,
             .running = nullptr,
             .stepped = nullptr,
             .waited = false,
             .chargesAllocations = false,
             .linesPrinted = 0,
             .stopped = false,
             .leftAtStop = 0,
             .lastStop = BudgetStop::None},
      lua{sol::default_at_panic, allocateLuaMemory, &memory},
      hookBudget{.instructions = static_cast<std::int64_t>(limits.instructionsPerHook),
                 .time = limits.hookDeadline},
      hookFrameBudget{.instructions = static_cast<std::int64_t>(limits.hookInstructionsPerFrame),
                      .time = limits.hookTimePerFrame},
      resumeBudget{.instructions = static_cast<std::int64_t>(limits.instructionsPerResume),
                   .time = limits.resumeDeadline} {
  budget.state = lua.lua_state();
  budget.attach();
  budget.reset();
}

ModState* VmState::findMod(std::string_view pack) {
  const auto found = std::ranges::find(mods, pack, &ModState::pack);
  return found == mods.end() ? nullptr : &*found;
}

MemoryOwner enterCall(VmState& vm, MemoryOwner owner, const std::optional<CallBudget>& budget) {
  if (vm.callDepth++ == 0) {
    if (budget) {
      vm.budget.reset(budget->instructions, budget->time);
    } else {
      vm.budget.reset();
    }
    vm.budget.chargesAllocations = true;
  }
  const MemoryOwner previous = vm.memory.owner;
  vm.memory.owner = owner;
  return previous;
}

void leaveCall(VmState& vm, MemoryOwner previousOwner) {
  vm.memory.owner = previousOwner;
  if (--vm.callDepth == 0) {
    vm.budget.chargesAllocations = false;
  }
}

CallScope::CallScope(VmState& vm, MemoryOwner owner, const std::optional<CallBudget>& budget)
    : vm_{&vm},
      outer_{budget && vm.callDepth > 0 ? std::optional{OuterBudget{.left = vm.budget.left,
                                                                    .deadline = vm.budget.deadline,
                                                                    .running = vm.budget.running}}
                                        : std::nullopt},
      previousOwner_{enterCall(vm, owner, budget)} {
  Budget& current = vm.budget;
  if (outer_) {
    // Never more than the outer call has, never less than nothing.
    current.left = std::max<std::int64_t>(0, std::min(outer_->left, budget->instructions));
    current.deadline = std::min(outer_->deadline, std::chrono::steady_clock::now() + budget->time);
    // The handlers run on the main thread, even when a coroutine fired the hook: its allocations
    // arm that one.
    current.running = current.state;
    granted_ = current.left;
  } else if (vm.callDepth == 1) {
    granted_ = budget ? budget->instructions : static_cast<std::int64_t>(current.perCall);
  } else {
    granted_ = current.left;
  }
}

CallScope::~CallScope() {
  if (outer_) {
    // The outer call goes on: it pays what this one spent, and re-arming its thread's hook restarts
    // the count. The main thread's hook may have been left firing on every instruction, if this
    // call ran out: re-arm it too when the outer call runs in a coroutine, paying for its restarted
    // count as well.
    Budget& current = vm_->budget;
    current.running = outer_->running;
    const bool inCoroutine = current.running != current.state;
    current.left = outer_->left - spent() - current.checkInterval(current.running) -
                   (inCoroutine ? current.checkInterval(current.state) : 0);
    current.deadline = outer_->deadline;
    // Only this call was stopped: the outer one goes on.
    current.stopped = false;
    current.arm(current.running);
    if (inCoroutine) {
      current.arm(current.state);
    }
  }
  leaveCall(*vm_, previousOwner_);
}

std::int64_t CallScope::spent() const {
  return std::max<std::int64_t>(0, granted_ - std::max<std::int64_t>(vm_->budget.unspent(), 0));
}

core::Result<void> runProtected(lua_State* state, lua_CFunction body, void* request) {
  lua_pushcfunction(state, body);
  lua_pushlightuserdata(state, request);
  if (lua_pcall(state, 1, 0, 0) != LUA_OK) {
    std::string message{errorMessage(state)};
    lua_pop(state, 1);
    return core::fail(std::move(message));
  }
  return {};
}

std::size_t hashText(std::string_view text) {
  return std::hash<std::string_view>{}(text);
}

void rememberAsset(std::vector<WatchedAsset>& assets, const assets::VirtualFileSystem& files,
                   const assets::AssetId& id, std::string_view text) {
  const WatchedAsset watched{.files = &files, .id = id, .textHash = hashText(text)};
  const auto known = std::ranges::find(assets, id, &WatchedAsset::id);
  if (known == assets.end()) {
    assets.push_back(watched);
  } else {
    *known = watched;
  }
}

} // namespace dwe::script::detail
