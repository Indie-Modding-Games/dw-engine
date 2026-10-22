#pragma once

#include <lua.hpp>

#include <string_view>

namespace dwe::script::detail {

// Gives the math table at index its own math.random and math.randomseed: a generator of the pack's
// own (xoshiro256**, as Lua's), seeded from its id, so no pack can see or steer another's numbers
// and a run gives the same numbers every time. math.randomseed() goes back to that seed;
// math.randomseed(n) seeds it with n.
void installPackRandom(lua_State* state, int mathTable, std::string_view pack);

} // namespace dwe::script::detail
