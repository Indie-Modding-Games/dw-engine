#pragma once

#include <lua.hpp>

#include <initializer_list>
#include <string_view>

namespace dwe::script::detail {

// luaL_error without its C varargs: raises the parts joined, after the script position as
// luaL_error does. Only trivially destructible values may be alive here, because lua_error unwinds
// with longjmp.
int raiseError(lua_State* state, std::initializer_list<std::string_view> parts);

// The message of a failed protected call, at index on the stack, without converting the error
// object: a conversion could run Lua code.
std::string_view errorMessage(lua_State* state, int index = -1);

} // namespace dwe::script::detail
