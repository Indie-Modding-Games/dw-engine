#pragma once

#include <lua.hpp>

namespace dwe::script::detail {

// Replaces the string and utf8 functions whose work grows with the length of their texts with versions that charge
// the budget for it, and string.rep with one that charges for the whole result before building it. Call before
// scripts run and before the string metatable is frozen.
void installBudgetedStrings(lua_State* state);

} // namespace dwe::script::detail
