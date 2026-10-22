#pragma once

#include <dwe/formats/script/guarded_walk.hpp>

#include <cstdint>
#include <optional>
#include <vector>

// The map changes a section of an event script can make, each with the path that makes it (guarded_walk.hpp).
namespace dwe::formats::script {

// warp_to (0x4B) and teleport (0x58), script_interp.c: scriptInstruction46to58.
struct MapChange {
    GuardedInstruction path; // where the instruction is and what it takes to get there
    // warp_to: the screen, its exit and the section of the new map's script that runs on arrival.
    std::optional<std::int32_t> screen;
    std::optional<std::int32_t> exit;
    std::optional<std::int32_t> arrivalSection;
    // teleport: the variable that holds the screen; the exit is in the next one.
    std::optional<std::int32_t> screenVariable;

    bool operator==(const MapChange&) const = default;
};

// The section of warp_to that names no section: nothing runs on arrival but the end of MAPHEAD.
inline constexpr std::int32_t NoArrivalSection = 0xFF;

// Every map change that the section can reach, once per distinct path.
std::vector<MapChange> findMapChanges(std::uint16_t script, std::uint16_t section, const ScriptLookup& lookup,
                                      const WalkOptions& options = {});

} // namespace dwe::formats::script
