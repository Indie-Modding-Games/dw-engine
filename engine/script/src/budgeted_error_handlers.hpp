#pragma once

#include <lua.hpp>

namespace dwe::script::detail {

// Replaces xpcall with one whose message handler is not run once the budget has stopped the script. Lua runs the
// handler of an error raised inside a hook, which is how the budget stops a script, with hooks turned off: a handler
// that loops would run forever.
void installBudgetedErrorHandlers(lua_State* state);

} // namespace dwe::script::detail
