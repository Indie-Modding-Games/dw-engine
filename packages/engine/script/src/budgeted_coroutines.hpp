#pragma once

#include <lua.hpp>

namespace dwe::script::detail {

// Replaces coroutine.resume and coroutine.wrap with versions that arm the resumed coroutine's hook
// with the call's budget and charge it, so a coroutine cannot run on after the budget runs out; and
// guards coroutine.close, which like coroutine.resume refuses the threads the engine runs.
void installBudgetedCoroutines(lua_State* state);

// coroutine.resume and coroutine.close refuse the thread at index from now on. May raise Lua
// errors.
void markEngineThread(lua_State* state, int index);

} // namespace dwe::script::detail
