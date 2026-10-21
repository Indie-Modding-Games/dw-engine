#pragma once

#include <lua.hpp>

namespace dwe::script::detail {

// Tables that scripts can read but not change. Their metatables are locked: getmetatable gives a text instead of the
// metatable, setmetatable refuses them, and so does the rawset of a pack's environment (pushGuardedRawset).

// Pushes a read-only view of the table at index: reads go to the table, writes are errors.
void pushReadOnlyView(lua_State* state, int index);

// Makes the table at index a strict environment: reading or assigning a global it does not have is an error.
void makeStrictEnvironment(lua_State* state, int index);

// Replaces Lua's rawset, on top of the stack, with one that refuses tables with a locked metatable.
void guardRawset(lua_State* state);

// Replaces the global setmetatable with one that refuses metatables with __gc: Lua runs a finalizer when the
// collector frees the table, with hooks off, so no budget could stop it, and outside the call that made it or inside
// another pack's.
void refuseFinalizers(lua_State* state);

// Points the metatable that strings share at a read-only copy of the string library as it is now, and locks it: with
// Lua's own __index, the global string table, the engine's scripts could change the string methods (("").upper) of
// every pack, and a pack could reach them through getmetatable. Call after the string library is complete.
void freezeStringMetatable(lua_State* state);

} // namespace dwe::script::detail
