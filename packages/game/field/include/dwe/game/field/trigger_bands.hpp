#pragma once

#include <dwe/core/collision_triggers.hpp>
#include <dwe/core/error.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::game::field {

// Collision values first to last, both included.
struct TriggerBand {
  std::int32_t first = 0;
  std::int32_t last = 0;

  [[nodiscard]] bool contains(std::int32_t value) const { return value >= first && value <= last; }

  friend bool operator==(const TriggerBand&, const TriggerBand&) = default;
};

// What the player's cell does by its collision value (tamer.c: checkMapInteraction), read from
// `field/triggers` in the game pack (content/schemas/field_triggers.schema.json). The defaults are
// the original's (core/collision_triggers.hpp), which the extraction also reads the disc with.
struct TriggerBands {
  // Stepping on the cell runs the map script's section of that number, every frame on it.
  TriggerBand step{.first = core::collision::FirstStepTrigger,
                   .last = core::collision::FirstConfirmTrigger - 1};
  // Pressing confirm on the cell runs the section of that number.
  TriggerBand confirm{.first = core::collision::FirstConfirmTrigger,
                      .last = core::collision::FirstWarpTrigger - 1};
  // The cell takes the map's table warp of exit (value - first).
  TriggerBand exits{.first = core::collision::FirstWarpTrigger,
                    .last =
                        core::collision::FirstWarpTrigger + core::collision::WarpTriggerCount - 1};
  // The partner that needs the toilet goes (map section 0x4e2).
  std::int32_t toilet = core::collision::ToiletTrigger;
  // A cell value with this bit blocks the way. The map format fixes it (MapCollision::BlockedBit),
  // so the document can only spell it, for the tools that read it.
  std::int32_t blockedBit = 0x80;

  // The exit a cell's value warps through, or nothing.
  [[nodiscard]] std::optional<std::int32_t> exitOf(std::int32_t value) const;
  // The cell value of an exit.
  [[nodiscard]] std::int32_t exitValue(std::int32_t exit) const { return exits.first + exit; }

  friend bool operator==(const TriggerBands&, const TriggerBands&) = default;
};

// The most exits a map has (MapWarp::exit is 0 to 9).
inline constexpr std::int32_t MostExits = 10;

// Parses the text of `field/triggers`: the bands may not overlap, stay below the blocked bit and
// above 0 (a free cell), and give at most MostExits exits. Unknown and missing keys are errors.
core::Result<TriggerBands> parseTriggerBands(std::string_view yaml);

// Reads `field/triggers` from the virtual file system; without one, the original's bands.
core::Result<TriggerBands> loadTriggerBands(const assets::VirtualFileSystem& vfs);

} // namespace dwe::game::field
