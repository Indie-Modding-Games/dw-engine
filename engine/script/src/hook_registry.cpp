#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/script/hook_registry.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>

#include "api_functions.hpp"
#include "api_names.hpp"
#include "hooks_table.hpp"
#include "locked_tables.hpp"
#include "lua_errors.hpp"
#include "messages.hpp"
#include "print_to_log.hpp"
#include "vm_state.hpp"

namespace dwe::script {
namespace detail {
namespace {

constexpr int EventArgument = 1;
constexpr int HandlerArgument = 2;
constexpr int VmUpvalue = 1;
constexpr int HandlersUpvalue = 2;

// The function of `hooks` that registers a handler.
constexpr const char* RegisterFunction = "on";

// The handlers one pack may register for one event.
constexpr lua_Unsigned MaxHandlersPerEvent = 64;

// hooks.on(event, handler). Upvalue 1 points to the VM, upvalue 2 is the pack's handlers table.
int registerHandler(lua_State* state) {
    std::size_t length = 0;
    const char* event = luaL_checklstring(state, EventArgument, &length);
    luaL_checktype(state, HandlerArgument, LUA_TFUNCTION);
    const auto* vm = static_cast<const VmState*>(lua_touserdata(state, lua_upvalueindex(VmUpvalue)));
    if (!vm->events.contains(std::string_view{event, length})) {
        return raiseError(state, {messages::UnknownEvent, std::string_view{event, length}, messages::UnknownEventEnd});
    }
    lua_pushvalue(state, lua_upvalueindex(HandlersUpvalue));
    lua_pushvalue(state, EventArgument);
    if (lua_rawget(state, -2) != LUA_TTABLE) {
        lua_pop(state, 1);
        lua_newtable(state);
        lua_pushvalue(state, EventArgument);
        lua_pushvalue(state, -2);
        lua_rawset(state, -4);
    }
    const lua_Unsigned registered = lua_rawlen(state, -1);
    if (registered >= MaxHandlersPerEvent) {
        return raiseError(state, {messages::TooManyHandlers, std::string_view{event, length}});
    }
    lua_pushvalue(state, HandlerArgument);
    lua_rawseti(state, -2, static_cast<lua_Integer>(registered) + 1);
    return 0;
}

// Pushes the list of a pack's handlers of an event, or nil.
void pushEventHandlers(lua_State* state, int handlers, std::string_view event) {
    lua_rawgeti(state, LUA_REGISTRYINDEX, handlers);
    lua_pushlstring(state, event.data(), event.size());
    lua_rawget(state, -2);
    lua_remove(state, -2);
}

struct HandlerCount {
    int handlers;
    std::string_view event;
    lua_Unsigned count;
};

int countHandlers(lua_State* state) {
    auto* count = static_cast<HandlerCount*>(lua_touserdata(state, 1));
    pushEventHandlers(state, count->handlers, count->event);
    count->count = lua_type(state, -1) == LUA_TTABLE ? lua_rawlen(state, -1) : 0;
    return 0;
}

struct HandlerCall {
    int handlers;
    std::string_view event;
    lua_Integer index;
    std::span<const ApiValue> arguments;
};

int callHandler(lua_State* state) {
    const auto* call = static_cast<const HandlerCall*>(lua_touserdata(state, 1));
    pushEventHandlers(state, call->handlers, call->event);
    if (lua_type(state, -1) != LUA_TTABLE || lua_rawgeti(state, -1, call->index) != LUA_TFUNCTION) {
        return 0;
    }
    luaL_checkstack(state, static_cast<int>(call->arguments.size()), nullptr);
    for (const ApiValue& argument : call->arguments) {
        pushApiValue(state, argument);
    }
    lua_call(state, static_cast<int>(call->arguments.size()), 0);
    return 0;
}

// Logs that the pack's handlers were skipped, at most once per RepeatReportInterval.
void reportSpentBudget(VmState& vm, ModState& mod, std::string_view event) {
    const auto now = std::chrono::steady_clock::now();
    if (mod.hookBudgetReported && now - *mod.hookBudgetReported < RepeatReportInterval) {
        return;
    }
    mod.hookBudgetReported = now;
    vm.packLogs.error(mod.pack, core::formatMessage(messages::HookBudgetSpent, mod.pack, event));
}

} // namespace

void pushHooksTable(lua_State* state, VmState& vm, int handlers) {
    handlers = lua_absindex(state, handlers);
    lua_createtable(state, 0, 1);
    lua_pushlightuserdata(state, &vm);
    lua_pushvalue(state, handlers);
    lua_pushcclosure(state, registerHandler, 2);
    lua_setfield(state, -2, RegisterFunction);
    pushReadOnlyView(state, -1);
    lua_remove(state, -2);
}

} // namespace detail

core::Result<void> HookRegistry::declare(std::string_view event) {
    if (!detail::isEventName(event)) {
        return core::fail(core::formatMessage(messages::BadEventName, event));
    }
    vm_->events.emplace(event);
    return {};
}

bool HookRegistry::isDeclared(std::string_view event) const {
    return vm_->events.contains(event);
}

core::Result<HookReport> HookRegistry::fire(std::string_view event, std::span<const ApiValue> arguments) {
    if (!isDeclared(event)) {
        return core::fail(core::formatMessage(messages::UndeclaredEvent, event));
    }
    lua_State* state = vm_->state();
    HookReport report;
    // By index: a handler may call an API function that makes another pack's environment, which grows mods and
    // would leave a range-for's iterators dangling.
    // NOLINTNEXTLINE(modernize-loop-convert)
    for (std::size_t mod = 0; mod < vm_->mods.size(); ++mod) {
        const std::string pack = vm_->mods[mod].pack;
        const int handlers = vm_->mods[mod].tables.handlers;
        // Counted outside the pack's call: a pack with no handler of the event opens no call and pays nothing.
        detail::HandlerCount count{.handlers = handlers, .event = event, .count = 0};
        if (auto counted = detail::runProtected(state, detail::countHandlers, &count); !counted) {
            return std::unexpected{counted.error()};
        }
        if (count.count == 0) {
            continue;
        }
        detail::FrameBudget& frame = vm_->mods[mod].hookFrame;
        if (frame.frameNumber != vm_->frameNumber) {
            frame = {.frameNumber = vm_->frameNumber, .left = vm_->hookFrameBudget};
        }
        if (frame.left.instructions <= 0 || frame.left.time <= std::chrono::steady_clock::duration::zero()) {
            report.handlersSkipped += count.count;
            detail::reportSpentBudget(*vm_, vm_->mods[mod], event);
            continue;
        }
        const detail::CallBudget budget{.instructions = std::min(vm_->hookBudget.instructions, frame.left.instructions),
                                        .time = std::min(vm_->hookBudget.time, frame.left.time)};
        const auto started = std::chrono::steady_clock::now();
        // One call per pack: all its handlers of this event share one budget.
        const detail::CallScope scope{*vm_, vm_->mods[mod].memoryOwner, budget};
        for (lua_Unsigned index = 1; index <= count.count; ++index) {
            if (vm_->budget.left <= 0 || vm_->budget.pastDeadline()) {
                report.handlersSkipped += count.count - index + 1;
                detail::reportSpentBudget(*vm_, vm_->mods[mod], event);
                break;
            }
            detail::HandlerCall call{
                .handlers = handlers, .event = event, .index = static_cast<lua_Integer>(index), .arguments = arguments};
            ++report.handlersRun;
            if (auto called = detail::runProtected(state, detail::callHandler, &call); !called) {
                ++report.handlersFailed;
                vm_->packLogs.error(pack, core::formatMessage(messages::HookFailed, pack, event,
                                                              detail::clippedScriptText(called.error().message)));
            }
        }
        // What the handlers spent comes out of the pack's frame budget (mods may have grown: index again).
        detail::FrameBudget& spentFrame = vm_->mods[mod].hookFrame;
        spentFrame.left.instructions -= scope.spent();
        spentFrame.left.time -= std::chrono::steady_clock::now() - started;
    }
    return report;
}

void HookRegistry::beginFrame() {
    ++vm_->frameNumber;
    vm_->packLogs.tick();
}

} // namespace dwe::script
