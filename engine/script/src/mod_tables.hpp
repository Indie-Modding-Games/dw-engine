#pragma once

#include <dwe/core/error.hpp>

#include <string_view>

#include "vm_state.hpp"

// The Lua tables of a pack's environment (ModTables): making them, running code in them and letting
// them go.
namespace dwe::script::detail {

// Makes a fresh environment for a pack: copies of the pristine globals, its own print and a guarded
// rawset, `_G` naming itself, `hooks`, every published API function, and strict globals.
core::Result<ModTables> makeModTables(VmState& vm, std::string_view pack);

// Lets the Lua registry forget the tables.
void releaseModTables(VmState& vm, ModTables tables);

// Copies every global of the table at source into the table at environment, and the tables among
// them (the libraries) one level deep, so changing a copied library changes only the copy. May
// raise Lua errors.
void copyGlobals(lua_State* state, int source, int environment);

// Compiles Lua source as text and runs it in a pack's environment, as one call into Lua whose
// memory the pack's owner pays for.
core::Result<void> runInEnvironment(VmState& vm, const ModTables& tables, MemoryOwner owner,
                                    std::string_view source, std::string_view chunkName);

// As runInEnvironment, keeping what the chunk returns as a reference in the Lua registry.
core::Result<int> loadInEnvironment(VmState& vm, const ModTables& tables, MemoryOwner owner,
                                    std::string_view source, std::string_view chunkName);

} // namespace dwe::script::detail
