#pragma once

#include <lua.hpp>

namespace dwe::script::detail {

// The text of a value as tostring gives it, but never with an address: a table, function, thread or userdata without
// __tostring reads `function: my_pack`, naming who runs the call, so scripts learn nothing of the memory layout and
// their output is the same from run to run. Pushes the text.
void pushAddressFreeText(lua_State* state, int index);

// Replaces the global tostring with one that gives address-free texts, and string.format with one whose %p and %s
// give them too.
void installAddressFreeText(lua_State* state);

} // namespace dwe::script::detail
