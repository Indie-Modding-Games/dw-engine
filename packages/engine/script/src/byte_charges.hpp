#pragma once

#include <lua.hpp>

namespace dwe::script::detail {

// Replaces the function `name` of the library table at index with one that charges the budget for
// the bytes it handles: its arguments before it runs (one step per argument plus the bytes of its
// text arguments) and its text results after. For library functions whose work grows with the
// length of their texts (string.upper, utf8.len).
void chargeForBytes(lua_State* state, int library, const char* name);

} // namespace dwe::script::detail
