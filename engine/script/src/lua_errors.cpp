#include "lua_errors.hpp"

#include "messages.hpp"

namespace dwe::script::detail {

int raiseError(lua_State* state, std::initializer_list<std::string_view> parts) {
    luaL_where(state, 1);
    for (const std::string_view part : parts) {
        lua_pushlstring(state, part.data(), part.size());
    }
    lua_concat(state, static_cast<int>(parts.size()) + 1);
    return lua_error(state);
}

std::string_view errorMessage(lua_State* state, int index) {
    if (lua_type(state, index) != LUA_TSTRING) {
        return messages::ErrorNotText;
    }
    std::size_t length = 0;
    const char* text = lua_tolstring(state, index, &length);
    return {text, length};
}

} // namespace dwe::script::detail
