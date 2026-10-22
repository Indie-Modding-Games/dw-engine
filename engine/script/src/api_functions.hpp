#pragma once

#include <dwe/script/api_registry.hpp>

#include <lua.hpp>

#include <string_view>

#include "vm_state.hpp"

// The Lua side of the API: values, the closures that call published functions, and the namespaces
// that hold them. These functions may raise Lua errors, so they run inside protected calls.
namespace dwe::script::detail {

void pushApiValue(lua_State* state, const ApiValue& value);

// Adds a published function to an environment, as seen by the scripts of a pack (empty for the
// engine's globals): its namespace's table behind namespaces, and the read-only view of it in the
// environment, are made the first time.
void addApiFunction(lua_State* state, int environment, int namespaces, PublishedFunction& function,
                    std::string_view pack);

} // namespace dwe::script::detail
