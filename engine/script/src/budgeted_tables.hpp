#pragma once

#include <lua.hpp>

namespace dwe::script::detail {

// Replaces table.insert, table.remove and table.move with versions that charge the instruction
// budget for each element they move: a table whose __len lies can make Lua's own loop over its
// elements run for hours inside one C call. table.sort, table.concat and table.unpack charge for
// their comparisons and bytes. Call before scripts run.
void installBudgetedTables(lua_State* state);

} // namespace dwe::script::detail
