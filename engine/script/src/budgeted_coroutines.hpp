#pragma once

#include <lua.hpp>

namespace dwe::script::detail {

// Replaces coroutine.resume and coroutine.wrap with versions that arm the resumed coroutine's hook
// with the call's budget and charge it, so a coroutine cannot run on after the budget runs out.
void installBudgetedCoroutines(lua_State* state);

} // namespace dwe::script::detail
