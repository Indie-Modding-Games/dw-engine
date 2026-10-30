#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::app {

// The console's and the scripts' place("<who> <x> <z> <rotation>"): puts the player, the partner
// or an NPC of the map at once at a ground point and facing given in the original's units and axes
// (x and z as its memory holds them, the rotation 4096 a turn), so a run starts where a trace of
// the original starts (docs/guides/traza-de-estado.md). <who> is `player`, `partner` or
// `npc:<script id>`. The partner takes four more numbers, `<clip> <frame> <fx> <fz>`, when the
// trace catches it in the middle of a clip: the original's animation id, the clip's frame whose
// step comes next and the fraction of a unit its location carries (1/32768ths, anim.c: locX, locZ).
// A debug aid: it changes no rule of the field.
inline constexpr std::string_view PlaceCommand = "place";

struct Placement {
  enum class Who : std::uint8_t { Player, Partner, Npc };
  Who who = Who::Player;
  // The NPC's script id, for Who::Npc.
  std::int32_t npc = 0;
  // The ground point in world axes (x, z), and the yaw in radians.
  glm::vec2 ground{0.0F};
  float yaw = 0.0F;
  // The partner's clip under way, in the original's terms.
  struct ClipUnderWay {
    std::int32_t id = 0;
    std::int32_t frame = 0;
    std::int32_t fractionX = 0;
    std::int32_t fractionZ = 0;
  };
  std::optional<ClipUnderWay> clip;
};

// Reads a place command's argument, or says what is wrong with it.
core::Result<Placement> placementFromText(std::string_view text);

} // namespace dwe::app
