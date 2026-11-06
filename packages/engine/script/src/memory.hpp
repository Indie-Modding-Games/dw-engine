#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "budget.hpp"

namespace dwe::script::detail {

// Who pays for a block of Lua memory: the engine, or the pack whose call reserved it.
using MemoryOwner = std::size_t;

inline constexpr MemoryOwner EngineMemory = 0;
inline constexpr std::string_view EngineOwnerName = "engine";

// The part of the memory limit that only the engine may use: when scripts fill the rest, the engine
// can still run the Lua work outside script calls that Lua would abort the program for failing.
inline constexpr std::size_t EngineReserveDivisor = 8;

// Every byte the Lua state holds, by owner, and the limits that apply to them.
struct Memory {
  std::size_t used = 0;
  // What the state may hold outside script calls.
  std::size_t limit = 0;
  // What the state may hold while a script call runs: the limit minus the engine's reserve.
  std::size_t scriptLimit = 0;
  // What the blocks of one pack may add up to.
  std::size_t packLimit = 0;
  // The largest block a script call may reserve.
  std::size_t largestBlock = 0;
  // Who pays for the blocks reserved now.
  MemoryOwner owner = EngineMemory;
  // Indexed by owner.
  std::vector<std::size_t> usedByOwner{0};
  // Indexed by owner: who scripts are told owns their values, the engine or a pack's id.
  std::vector<std::string> ownerNames{std::string{EngineOwnerName}};
  // The budget that pays for the time spent filling new blocks.
  Budget* budget = nullptr;

  // Adds the owner of a new pack's memory.
  MemoryOwner addOwner(std::string name);
};

// Lua's allocator (lua_Alloc), with userData pointing to a Memory. Each block starts with a header
// naming its owner, so the bytes go back to the right owner whenever the collector frees them.
// Returning nullptr makes Lua raise a memory error in the script.
void* allocateLuaMemory(void* userData, void* block, std::size_t oldSize, std::size_t newSize);

} // namespace dwe::script::detail
