#include "memory.hpp"

#include <cstdlib>
#include <cstring>
#include <utility>

namespace dwe::script::detail {
namespace {

// Keeps the block that Lua sees aligned as malloc's are.
constexpr std::size_t HeaderBytes = alignof(std::max_align_t);
static_assert(HeaderBytes >= sizeof(MemoryOwner));

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic): the header sits right before the block Lua sees.
std::byte* headerOf(void* block) {
    return static_cast<std::byte*>(block) - HeaderBytes;
}

void* blockAfter(void* header) {
    return static_cast<std::byte*>(header) + HeaderBytes;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

MemoryOwner readOwner(const std::byte* header) {
    MemoryOwner owner = EngineMemory;
    std::memcpy(&owner, header, sizeof owner);
    return owner;
}

void writeOwner(void* header, MemoryOwner owner) {
    std::memcpy(header, &owner, sizeof owner);
}

// Whether a block may grow from previous bytes, held by previousOwner, to newSize bytes held by the current owner.
bool allowsGrowth(const Memory& memory, MemoryOwner previousOwner, std::size_t previous, std::size_t newSize) {
    const bool inScriptCall = memory.budget->chargesAllocations;
    const std::size_t total = memory.used - previous + newSize;
    if (total > (inScriptCall ? memory.scriptLimit : memory.limit)) {
        return false;
    }
    if (!inScriptCall) {
        return true;
    }
    if (newSize > memory.largestBlock) {
        return false;
    }
    if (memory.owner == EngineMemory) {
        return true;
    }
    const std::size_t ownerUsed = memory.usedByOwner[memory.owner] - (previousOwner == memory.owner ? previous : 0);
    return ownerUsed + newSize <= memory.packLimit;
}

void release(Memory& memory, MemoryOwner owner, std::size_t bytes) {
    memory.used -= bytes;
    memory.usedByOwner[owner] -= bytes;
}

void hold(Memory& memory, MemoryOwner owner, std::size_t bytes) {
    memory.used += bytes;
    memory.usedByOwner[owner] += bytes;
}

} // namespace

MemoryOwner Memory::addOwner(std::string name) {
    usedByOwner.push_back(0);
    ownerNames.push_back(std::move(name));
    return usedByOwner.size() - 1;
}

void* allocateLuaMemory(void* userData, void* block, std::size_t oldSize, std::size_t newSize) {
    auto* memory = static_cast<Memory*>(userData);
    std::byte* header = block != nullptr ? headerOf(block) : nullptr;
    // When block is null, oldSize holds the kind of object being created, not a size.
    const std::size_t previous = block != nullptr ? oldSize : 0;
    const MemoryOwner previousOwner = header != nullptr ? readOwner(header) : memory->owner;
    if (newSize == 0) {
        std::free(header); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): Lua's contract.
        release(*memory, previousOwner, previous);
        return nullptr;
    }
    const bool grows = newSize > previous;
    if (grows && !allowsGrowth(*memory, previousOwner, previous, newSize)) {
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): lua_Alloc must behave as realloc.
    void* resized = std::realloc(header, HeaderBytes + newSize);
    if (resized == nullptr) {
        return nullptr;
    }
    // A block that grows moves to whoever made it grow; a block that shrinks stays with its owner.
    const MemoryOwner owner = grows ? memory->owner : previousOwner;
    release(*memory, previousOwner, previous);
    hold(*memory, owner, newSize);
    writeOwner(resized, owner);
    if (grows) {
        memory->budget->chargeAllocation(newSize - previous);
    }
    return blockAfter(resized);
}

} // namespace dwe::script::detail
