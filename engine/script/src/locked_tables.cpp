#include "locked_tables.hpp"

#include <string_view>

#include "lua_errors.hpp"
#include "messages.hpp"
#include "metafields.hpp"

namespace dwe::script::detail {
namespace {

constexpr int TableArgument = 1;
constexpr int KeyArgument = 2;

int refuseChange(lua_State* state) {
    return raiseError(state, {messages::ReadOnlyTable});
}

// The name of a global in an error, when it is a text.
std::string_view globalName(lua_State* state) {
    if (lua_type(state, KeyArgument) != LUA_TSTRING) {
        return messages::GlobalWithoutName;
    }
    std::size_t length = 0;
    const char* name = lua_tolstring(state, KeyArgument, &length);
    return {name, length};
}

int refuseUnknownGlobal(lua_State* state) {
    return raiseError(state, {messages::UnknownGlobal, globalName(state), messages::UnknownGlobalEnd});
}

int refuseNewGlobal(lua_State* state) {
    return raiseError(state, {messages::NewGlobal, globalName(state), messages::NewGlobalEnd});
}

bool hasLockedMetatable(lua_State* state, int index) {
    return luaL_getmetafield(state, index, MetatableField) != LUA_TNIL;
}

// Upvalue 1 is Lua's rawset.
int guardedRawset(lua_State* state) {
    if (lua_type(state, TableArgument) == LUA_TTABLE && hasLockedMetatable(state, TableArgument)) {
        return raiseError(state, {messages::RawsetOnLockedTable});
    }
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, 1);
    lua_call(state, lua_gettop(state) - 1, LUA_MULTRET);
    return lua_gettop(state);
}

constexpr int MetatableArgument = 2;

// Upvalue 1 is Lua's setmetatable.
int guardedSetmetatable(lua_State* state) {
    if (lua_type(state, MetatableArgument) == LUA_TTABLE) {
        lua_pushstring(state, FinalizerField);
        const bool hasFinalizer = lua_rawget(state, MetatableArgument) != LUA_TNIL;
        lua_pop(state, 1);
        if (hasFinalizer) {
            return raiseError(state, {messages::FinalizerRefused});
        }
    }
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, 1);
    lua_call(state, lua_gettop(state) - 1, LUA_MULTRET);
    return lua_gettop(state);
}

// Sets the locking __metatable field of the metatable on top of the stack.
void lockMetatable(lua_State* state) {
    lua_pushlstring(state, messages::LockedMetatable.data(), messages::LockedMetatable.size());
    lua_setfield(state, -2, MetatableField);
}

} // namespace

void pushReadOnlyView(lua_State* state, int index) {
    const int table = lua_absindex(state, index);
    lua_newtable(state);
    lua_createtable(state, 0, 3);
    lua_pushvalue(state, table);
    lua_setfield(state, -2, IndexField);
    lua_pushcfunction(state, refuseChange);
    lua_setfield(state, -2, NewIndexField);
    lockMetatable(state);
    lua_setmetatable(state, -2);
}

void makeStrictEnvironment(lua_State* state, int index) {
    const int environment = lua_absindex(state, index);
    lua_createtable(state, 0, 3);
    lua_pushcfunction(state, refuseUnknownGlobal);
    lua_setfield(state, -2, IndexField);
    lua_pushcfunction(state, refuseNewGlobal);
    lua_setfield(state, -2, NewIndexField);
    lockMetatable(state);
    lua_setmetatable(state, environment);
}

void guardRawset(lua_State* state) {
    lua_pushcclosure(state, guardedRawset, 1);
}

void refuseFinalizers(lua_State* state) {
    lua_getglobal(state, "setmetatable");
    lua_pushcclosure(state, guardedSetmetatable, 1);
    lua_setglobal(state, "setmetatable");
}

void freezeStringMetatable(lua_State* state) {
    lua_pushliteral(state, "");
    lua_getmetatable(state, -1);
    const int metatable = lua_gettop(state);
    lua_newtable(state);
    lua_getglobal(state, LUA_STRLIBNAME);
    lua_pushnil(state);
    while (lua_next(state, -2) != 0) {
        lua_pushvalue(state, -2);
        lua_insert(state, -2);
        lua_rawset(state, -5);
    }
    lua_pop(state, 1);
    pushReadOnlyView(state, -1);
    lua_setfield(state, metatable, IndexField);
    lua_pop(state, 1);
    lockMetatable(state);
    lua_pop(state, 2);
}

} // namespace dwe::script::detail
