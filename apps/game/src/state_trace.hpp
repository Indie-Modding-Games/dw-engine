#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dwe::app {

// A line per logic tick of what the game simulates (--trace <file>,
// docs/guides/traza-de-estado.md): where the player, the partner and the NPCs stand and the
// battle's fighters, so a run can be compared with the same inputs played on the original.
// Positions and facings are written in the original's axes and units (x, -y, -z of the world; 4096
// a turn), the ones its memory holds.

// The buttons held in a tick, with the original's pad bits (libetc PadRead), so both traces say
// the same thing: the d-pad and the face buttons the field and the battle read.
namespace trace_buttons {
inline constexpr std::uint32_t Triangle = 0x10; // menu
inline constexpr std::uint32_t Circle = 0x20;   // cancel
inline constexpr std::uint32_t Cross = 0x40;    // confirm
inline constexpr std::uint32_t Square = 0x80;   // walk
inline constexpr std::uint32_t Up = 0x1000;
inline constexpr std::uint32_t Right = 0x2000;
inline constexpr std::uint32_t Down = 0x4000;
inline constexpr std::uint32_t Left = 0x8000;
} // namespace trace_buttons

// A character in world space.
struct TraceCharacter {
  glm::vec3 position{};
  float yaw = 0.0F;
  // The NPC's script id; nothing for the player and the partner.
  std::optional<std::int32_t> id;
};

// A fighter of the running battle, on the battle's ground (world axes, world units).
struct TraceFighter {
  std::int32_t x = 0;
  std::int32_t z = 0;
  float yaw = 0.0F;
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  std::int32_t maxHp = 0;
  std::int32_t maxMp = 0;
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
  std::int32_t brain = 0;
  std::string pose;
};

struct TraceBattle {
  std::string phase;
  std::int64_t frame = 0;
  // The partner first, then the enemies in their order.
  std::vector<TraceFighter> fighters;
};

struct TraceFrame {
  std::uint64_t tick = 0;
  double seconds = 0.0;
  std::uint32_t buttons = 0;
  std::string map;
  std::optional<TraceCharacter> player;
  std::optional<TraceCharacter> partner;
  std::vector<TraceCharacter> npcs;
  std::optional<TraceBattle> battle;
};

// The frame as one JSON line (no newline).
std::string traceLine(const TraceFrame& frame);

// Writes the trace file: a first line that says what it is, then a line per frame.
class StateTrace {
public:
  static core::Result<StateTrace> open(const std::filesystem::path& path);

  void write(const TraceFrame& frame);

private:
  explicit StateTrace(std::ofstream file) : file_(std::move(file)) {}

  std::ofstream file_;
};

} // namespace dwe::app
