#pragma once

#include <lua.hpp>

#include <string_view>

#include "vm_state.hpp"

namespace dwe::script::detail {

// The global of a pack's environment that holds the hook functions. No API namespace may take it.
inline constexpr std::string_view HooksGlobal = "hooks";

// Pushes the read-only `hooks` table of a pack's environment: hooks.on(event, handler) adds handler
// to the handlers table at index (event name -> list of functions). The event must be declared. May
// raise Lua errors.
void pushHooksTable(lua_State* state, VmState& vm, int handlers);

} // namespace dwe::script::detail
