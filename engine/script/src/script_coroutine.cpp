#include <dwe/core/assert.hpp>
#include <dwe/core/message.hpp>
#include <dwe/script/script_coroutine.hpp>

#include <format>
#include <string_view>
#include <type_traits>
#include <utility>

#include "api_functions.hpp"
#include "lua_errors.hpp"
#include "messages.hpp"
#include "vm_state.hpp"

namespace dwe::script {
namespace {

int forgetReference(lua_State* state) {
  luaL_unref(state, LUA_REGISTRYINDEX, *static_cast<const int*>(lua_touserdata(state, 1)));
  return 0;
}

void releaseReference(detail::VmState& vm, int reference) {
  // Forgetting a reference only writes to the registry; failing to would only keep the value alive.
  [[maybe_unused]] const auto released =
      detail::runProtected(vm.state(), forgetReference, &reference);
}

std::string pathText(std::span<const ApiValue> path) {
  std::string text;
  for (const ApiValue& key : path) {
    if (!text.empty()) {
      text += '.';
    }
    std::visit(
        [&text](const auto& held) {
          using Held = std::decay_t<decltype(held)>;
          if constexpr (std::is_same_v<Held, std::monostate>) {
            text += "nil";
          } else if constexpr (std::is_same_v<Held, std::string>) {
            text += held;
          } else {
            text += std::format("{}", held);
          }
        },
        key);
  }
  return text;
}

struct CoroutineStart {
  int module;
  std::span<const ApiValue> path;
  int reference;
  lua_State* thread;
};

int startCoroutine(lua_State* state) {
  auto* start = static_cast<CoroutineStart*>(lua_touserdata(state, 1));
  lua_rawgeti(state, LUA_REGISTRYINDEX, start->module);
  for (const ApiValue& key : start->path) {
    if (lua_type(state, -1) != LUA_TTABLE) {
      return 0;
    }
    detail::pushApiValue(state, key);
    lua_rawget(state, -2);
    lua_remove(state, -2);
  }
  if (lua_type(state, -1) != LUA_TFUNCTION) {
    return 0;
  }
  lua_State* thread = lua_newthread(state);
  lua_insert(state, -2);
  lua_xmove(state, thread, 1);
  start->reference = luaL_ref(state, LUA_REGISTRYINDEX);
  start->thread = thread;
  return 0;
}

struct ValuePush {
  const ApiValue* value;
};

int pushValue(lua_State* state) {
  detail::pushApiValue(state, *static_cast<const ValuePush*>(lua_touserdata(state, 1))->value);
  return 1;
}

// Strings allocate, so the value is made on the main thread inside a protected call and then moved.
bool pushResumeValue(lua_State* state, lua_State* thread, const ApiValue& value) {
  if (lua_checkstack(thread, 1) == 0 || lua_checkstack(state, 2) == 0) {
    return false;
  }
  ValuePush push{.value = &value};
  lua_pushcfunction(state, pushValue);
  lua_pushlightuserdata(state, &push);
  if (lua_pcall(state, 1, 1, 0) != LUA_OK) {
    lua_pop(state, 1);
    return false;
  }
  lua_xmove(state, thread, 1);
  return true;
}

std::uint32_t luaCallDepth(lua_State* thread) {
  lua_Debug debug{};
  std::uint32_t frames = 0;
  for (int level = 0; lua_getstack(thread, level, &debug) != 0; ++level) {
    if (lua_getinfo(thread, "S", &debug) != 0 && std::string_view{debug.what} == "Lua") {
      ++frames;
    }
  }
  return frames > 0 ? frames - 1 : 0;
}

} // namespace

ScriptModule::ScriptModule(detail::VmState& vm, std::size_t mod, int reference, std::string name)
    : vm_{&vm}, mod_{mod}, reference_{reference}, name_{std::move(name)} {}

ScriptModule::ScriptModule(ScriptModule&& other) noexcept
    : vm_{other.vm_}, mod_{other.mod_}, reference_{std::exchange(other.reference_, LUA_NOREF)},
      name_{std::move(other.name_)} {}

ScriptModule& ScriptModule::operator=(ScriptModule&& other) noexcept {
  if (this != &other) {
    release();
    vm_ = other.vm_;
    mod_ = other.mod_;
    reference_ = std::exchange(other.reference_, LUA_NOREF);
    name_ = std::move(other.name_);
  }
  return *this;
}

ScriptModule::~ScriptModule() {
  release();
}

void ScriptModule::release() {
  if (reference_ != LUA_NOREF) {
    releaseReference(*vm_, std::exchange(reference_, LUA_NOREF));
  }
}

core::Result<ScriptCoroutine> ScriptModule::coroutine(std::span<const ApiValue> path) const {
  DWE_ASSERT(reference_ != LUA_NOREF);
  CoroutineStart start{
      .module = reference_, .path = path, .reference = LUA_NOREF, .thread = nullptr};
  const detail::CallScope scope{*vm_, vm_->mods[mod_].memoryOwner};
  if (auto started = detail::runProtected(vm_->state(), startCoroutine, &start); !started) {
    return std::unexpected{started.error()};
  }
  if (start.thread == nullptr) {
    return core::fail(core::formatMessage(messages::NoModuleFunction, name_, pathText(path)));
  }
  return ScriptCoroutine{*vm_, mod_, start.reference, start.thread};
}

ScriptCoroutine::ScriptCoroutine(detail::VmState& vm, std::size_t mod, int reference,
                                 lua_State* thread)
    : vm_{&vm}, mod_{mod}, reference_{reference}, thread_{thread} {}

ScriptCoroutine::ScriptCoroutine(ScriptCoroutine&& other) noexcept
    : vm_{other.vm_}, mod_{other.mod_}, reference_{std::exchange(other.reference_, LUA_NOREF)},
      thread_{std::exchange(other.thread_, nullptr)}, started_{other.started_} {}

ScriptCoroutine& ScriptCoroutine::operator=(ScriptCoroutine&& other) noexcept {
  if (this != &other) {
    if (thread_ != nullptr) {
      const detail::CallScope scope{*vm_, vm_->mods[mod_].memoryOwner, vm_->resumeBudget};
      close();
    }
    vm_ = other.vm_;
    mod_ = other.mod_;
    reference_ = std::exchange(other.reference_, LUA_NOREF);
    thread_ = std::exchange(other.thread_, nullptr);
    started_ = other.started_;
  }
  return *this;
}

ScriptCoroutine::~ScriptCoroutine() {
  if (thread_ != nullptr) {
    const detail::CallScope scope{*vm_, vm_->mods[mod_].memoryOwner, vm_->resumeBudget};
    close();
  }
}

void ScriptCoroutine::close() {
  detail::Budget& budget = vm_->budget;
  // A coroutine can only be closed while it is suspended, never from inside its own run.
  DWE_ASSERT(budget.running != thread_);
  lua_State* const resumer = budget.running;
  budget.arm(thread_);
  budget.running = thread_;
  // Errors of its to-be-closed variables are lost: the coroutine is gone either way.
  [[maybe_unused]] const int closed = lua_closethread(thread_, vm_->state());
  budget.running = resumer;
  releaseReference(*vm_, std::exchange(reference_, LUA_NOREF));
  thread_ = nullptr;
}

CoroutineStep ScriptCoroutine::resume(const ApiValue& value) {
  if (thread_ == nullptr) {
    return CoroutineFailed{.message = std::string{messages::CoroutineOver},
                           .stop = BudgetStop::None};
  }
  detail::VmState& vm = *vm_;
  const detail::CallScope scope{vm, vm.mods[mod_].memoryOwner, vm.resumeBudget};
  detail::Budget& budget = vm.budget;
  int arguments = 0;
  if (started_) {
    if (!pushResumeValue(vm.state(), thread_, value)) {
      close();
      return CoroutineFailed{.message = std::string{messages::ResumeOutOfMemory},
                             .stop = BudgetStop::None};
    }
    arguments = 1;
  }
  started_ = true;
  lua_State* const resumer = budget.running;
  lua_State* const outerStepped = budget.stepped;
  // Arming restarts the thread's count, so its first check is paid up front, as coroutine.resume
  // does.
  budget.left -= detail::CoroutineCheckInterval;
  budget.arm(thread_);
  budget.running = thread_;
  budget.stepped = thread_;
  budget.waited = false;
  int results = 0;
  const int status = lua_resume(thread_, vm.state(), arguments, &results);
  const bool waited = std::exchange(budget.waited, false);
  budget.running = resumer;
  budget.stepped = outerStepped;
  if (status == LUA_YIELD && waited) {
    ApiValue yielded = results > 0 ? detail::readApiValue(thread_, -1) : ApiValue{};
    lua_pop(thread_, results);
    return CoroutineWaiting{.value = std::move(yielded), .callDepth = luaCallDepth(thread_)};
  }
  if (status == LUA_YIELD) {
    lua_pop(thread_, results);
    close();
    return CoroutineFailed{.message = std::string{messages::YieldWithoutWait},
                           .stop = BudgetStop::None};
  }
  if (status == LUA_OK) {
    const int type = results > 0 ? lua_type(thread_, -results) : LUA_TNIL;
    CoroutineStep step =
        detail::isApiType(type)
            ? CoroutineStep{CoroutineFinished{
                  .value = results > 0 ? detail::readApiValue(thread_, -results) : ApiValue{}}}
            : CoroutineStep{
                  CoroutineFailed{.message = core::formatMessage(messages::BadReturnValue,
                                                                 lua_typename(thread_, type)),
                                  .stop = BudgetStop::None}};
    lua_pop(thread_, results);
    close();
    return step;
  }
  CoroutineFailed failed{.message = std::string{detail::errorMessage(thread_)},
                         .stop = budget.stopped ? budget.lastStop : BudgetStop::None};
  close();
  return failed;
}

} // namespace dwe::script
