#include "mod_tables.hpp"

#include <string>

#include "api_functions.hpp"
#include "hooks_table.hpp"
#include "locked_tables.hpp"
#include "pack_random.hpp"
#include "print_to_log.hpp"

namespace dwe::script::detail {

void copyGlobals(lua_State* state, int source, int environment) {
  lua_pushnil(state);
  while (lua_next(state, source) != 0) {
    if (lua_type(state, -1) == LUA_TTABLE) {
      const int library = lua_gettop(state);
      lua_newtable(state);
      lua_pushnil(state);
      while (lua_next(state, library) != 0) {
        lua_pushvalue(state, -2);
        lua_insert(state, -2);
        lua_rawset(state, library + 1);
      }
      lua_remove(state, library);
    }
    lua_pushvalue(state, -2);
    lua_insert(state, -2);
    lua_rawset(state, environment);
  }
}

namespace {

// The standard functions that a pack's environment has its own versions of.
constexpr const char* PrintFunction = "print";
constexpr const char* RawsetFunction = "rawset";

struct TablesBuild {
  VmState* vm;
  std::string_view pack;
  ModTables tables;
};

int buildTables(lua_State* state) {
  auto* build = static_cast<TablesBuild*>(lua_touserdata(state, 1));
  VmState& vm = *build->vm;
  lua_newtable(state);
  const int environment = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, vm.pristineGlobals);
  copyGlobals(state, environment + 1, environment);
  lua_pop(state, 1);
  lua_getfield(state, environment, LUA_MATHLIBNAME);
  installPackRandom(state, -1, build->pack);
  lua_pop(state, 1);
  pushModPrint(state, build->pack, vm.packLogs);
  lua_setfield(state, environment, PrintFunction);
  lua_getfield(state, environment, RawsetFunction);
  guardRawset(state);
  lua_setfield(state, environment, RawsetFunction);
  lua_pushvalue(state, environment);
  lua_setfield(state, environment, LUA_GNAME);
  lua_newtable(state);
  const int handlers = lua_gettop(state);
  pushHooksTable(state, vm, handlers);
  lua_pushlstring(state, HooksGlobal.data(), HooksGlobal.size());
  lua_insert(state, -2);
  lua_rawset(state, environment);
  lua_newtable(state);
  const int namespaces = lua_gettop(state);
  for (const auto& function : vm.functions) {
    addApiFunction(state, environment, namespaces, *function, build->pack);
  }
  makeStrictEnvironment(state, environment);
  build->tables.namespaces = luaL_ref(state, LUA_REGISTRYINDEX);
  build->tables.handlers = luaL_ref(state, LUA_REGISTRYINDEX);
  build->tables.environment = luaL_ref(state, LUA_REGISTRYINDEX);
  return 0;
}

int releaseTables(lua_State* state) {
  const auto* tables = static_cast<const ModTables*>(lua_touserdata(state, 1));
  luaL_unref(state, LUA_REGISTRYINDEX, tables->environment);
  luaL_unref(state, LUA_REGISTRYINDEX, tables->handlers);
  luaL_unref(state, LUA_REGISTRYINDEX, tables->namespaces);
  return 0;
}

struct ChunkRun {
  std::string_view source;
  const char* chunkName;
  int environment;
};

int runChunk(lua_State* state) {
  const auto* chunk = static_cast<const ChunkRun*>(lua_touserdata(state, 1));
  // Text only: precompiled bytecode is not checked by Lua and could break out of the sandbox.
  if (luaL_loadbufferx(state, chunk->source.data(), chunk->source.size(), chunk->chunkName, "t") !=
      LUA_OK) {
    return lua_error(state);
  }
  // A main chunk's only upvalue is its _ENV.
  lua_rawgeti(state, LUA_REGISTRYINDEX, chunk->environment);
  lua_setupvalue(state, -2, 1);
  lua_call(state, 0, 0);
  return 0;
}

} // namespace

core::Result<ModTables> makeModTables(VmState& vm, std::string_view pack) {
  TablesBuild build{.vm = &vm, .pack = pack, .tables = {}};
  if (auto built = runProtected(vm.state(), buildTables, &build); !built) {
    return std::unexpected{built.error()};
  }
  return build.tables;
}

void releaseModTables(VmState& vm, ModTables tables) {
  // Forgetting a reference only writes to the registry; failing to would only keep the tables
  // alive.
  [[maybe_unused]] const auto released = runProtected(vm.state(), releaseTables, &tables);
}

core::Result<void> runInEnvironment(VmState& vm, const ModTables& tables, MemoryOwner owner,
                                    std::string_view source, std::string_view chunkName) {
  const std::string name{chunkName};
  ChunkRun chunk{.source = source, .chunkName = name.c_str(), .environment = tables.environment};
  const CallScope scope{vm, owner};
  return runProtected(vm.state(), runChunk, &chunk);
}

} // namespace dwe::script::detail
