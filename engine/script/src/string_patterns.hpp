#pragma once

#include <lua.hpp>

namespace dwe::script::detail {

// Replaces string.find, string.match, string.gmatch and string.gsub with versions that charge the
// instruction budget for each step of a pattern match (PatternMatcher). Strings' methods use them
// too, since the string metatable indexes the same table. Call before scripts run.
void installBudgetedPatterns(lua_State* state);

} // namespace dwe::script::detail
