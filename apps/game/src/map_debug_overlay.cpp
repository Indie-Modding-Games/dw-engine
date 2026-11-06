#include "map_debug_overlay.hpp"

#include <dwe/core/collision_triggers.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace dwe::app {

namespace {

using debug::overlayColor;
namespace collision = core::collision;

constexpr float CellSize = 100.0F;
// How far a facing arrow reaches, in world units.
constexpr float FacingLength = 120.0F;
constexpr debug::OverlayColor LabelColor = overlayColor(255, 255, 255);
constexpr debug::OverlayColor ExitColor = overlayColor(90, 255, 120);
constexpr debug::OverlayColor NpcColor = overlayColor(255, 170, 60);
constexpr debug::OverlayColor WaypointColor = overlayColor(255, 170, 60, 200);

bool outside(glm::vec2 point) {
  return point.x < 0.0F || point.x > 1.0F || point.y < 0.0F || point.y > 1.0F;
}

// The cells of one trigger value: their centre, for its label.
struct ValueCells {
  glm::vec3 sum{0.0F};
  std::int32_t count = 0;
};

std::string labelOf(std::uint8_t value, CellBand band, const assets::MapDocument& map) {
  switch (band) {
  case CellBand::Exit: {
    const std::int32_t exit = value - collision::FirstWarpTrigger;
    const auto warp = std::ranges::find(map.warps, exit, &assets::MapWarp::exit);
    if (warp == map.warps.end()) {
      return std::format("exit {} -> (no warp)", exit);
    }
    return std::format("exit {} -> {}:{}", exit, warp->targetMapId.value_or("(none)"),
                       warp->targetExit);
  }
  case CellBand::Toilet:
    return "toilet";
  case CellBand::Step:
    return std::format("step {}", value);
  case CellBand::Confirm:
    return std::format("confirm {}", value);
  case CellBand::Other:
    return std::format("trigger {}", value);
  case CellBand::Free:
  case CellBand::Wall:
    break;
  }
  return {};
}

void addArrow(glm::vec3 from, float yaw, debug::OverlayColor color, const OverlayProjector& project,
              debug::OverlayShapes& out) {
  const auto start = project(from);
  const auto end = project(from + (assets::forwardOfYaw(yaw) * FacingLength));
  if (!start) {
    return;
  }
  out.dots.push_back({.at = *start, .radius = 4.0F, .color = color});
  if (end) {
    out.lines.push_back({.from = *start, .to = *end, .color = color, .thickness = 2.0F});
  }
}

void addCells(const assets::MapDocument& map, const assets::MapCollision& grid,
              const OverlayProjector& project, debug::OverlayShapes& out) {
  std::map<std::uint8_t, ValueCells> triggers;
  for (std::int32_t row = 0; row < assets::MapCollision::Size; ++row) {
    for (std::int32_t column = 0; column < assets::MapCollision::Size; ++column) {
      const assets::CollisionCell cell{.column = column, .row = row};
      const std::uint8_t value = grid.value(cell);
      const CellBand band = cellBandOf(value);
      if (band == CellBand::Free) {
        continue;
      }
      const glm::vec3 low = assets::collisionCellMin(cell);
      const std::array<glm::vec3, 4> world{low, low + glm::vec3{CellSize, 0.0F, 0.0F},
                                           low + glm::vec3{CellSize, 0.0F, CellSize},
                                           low + glm::vec3{0.0F, 0.0F, CellSize}};
      debug::OverlayQuad quad{.corners = {}, .fill = bandColor(band), .outline = 0};
      bool shown = true;
      bool anyInside = false;
      for (std::size_t corner = 0; corner < world.size(); ++corner) {
        const auto point = project(world[corner]);
        if (!point) {
          shown = false;
          break;
        }
        quad.corners[corner] = *point;
        anyInside = anyInside || !outside(*point);
      }
      if (!shown || !anyInside) {
        continue;
      }
      if (band != CellBand::Wall) {
        // A trigger's cells are outlined too, so neighbouring bands stay apart.
        quad.outline = bandColor(band) | 0xFF000000U;
        ValueCells& cells = triggers[value];
        cells.sum += assets::collisionCellCenter(cell);
        ++cells.count;
      }
      out.quads.push_back(quad);
    }
  }
  for (const auto& [value, cells] : triggers) {
    const glm::vec3 centre = cells.sum / static_cast<float>(cells.count);
    if (const auto at = project(centre)) {
      out.labels.push_back(
          {.at = *at, .text = labelOf(value, cellBandOf(value), map), .color = LabelColor});
    }
  }
}

void addExits(const assets::MapDocument& map, const OverlayProjector& project,
              debug::OverlayShapes& out) {
  for (const assets::MapWarp& warp : map.warps) {
    const assets::SpawnPoint spawn = assets::warpSpawn(warp);
    addArrow(spawn.position, spawn.yaw, ExitColor, project, out);
    if (const auto at = project(spawn.position)) {
      out.labels.push_back({.at = *at + glm::vec2{0.008F, 0.025F},
                            .text = std::format("in {}", warp.exit),
                            .color = ExitColor});
    }
  }
}

void addNpcs(const assets::MapDocument& map, const OverlayProjector& project,
             debug::OverlayShapes& out) {
  for (std::size_t slot = 0; slot < map.npcs.size(); ++slot) {
    const assets::MapNpc& npc = map.npcs[slot];
    const glm::vec3 position = assets::toWorldPosition(npc.position);
    addArrow(position, assets::yawFromOriginal(npc.rotation.y), NpcColor, project, out);
    if (const auto at = project(position)) {
      out.labels.push_back({.at = *at + glm::vec2{0.005F, -0.03F},
                            .text = std::format("npc {} id {}", slot, npc.scriptId),
                            .color = NpcColor});
    }
    std::optional<glm::vec2> previous = project(position);
    for (const glm::ivec3& waypoint : npc.waypoints) {
      const auto point = project(assets::toWorldPosition(waypoint));
      if (point) {
        out.dots.push_back({.at = *point, .radius = 3.0F, .color = WaypointColor});
        if (previous) {
          out.lines.push_back(
              {.from = *previous, .to = *point, .color = WaypointColor, .thickness = 1.0F});
        }
      }
      previous = point;
    }
  }
}

} // namespace

CellBand cellBandOf(std::uint8_t value) {
  if (value == 0) {
    return CellBand::Free;
  }
  if ((value & assets::MapCollision::BlockedBit) != 0) {
    return CellBand::Wall;
  }
  const std::int32_t trigger = value;
  if (trigger >= collision::FirstStepTrigger && trigger < collision::FirstConfirmTrigger) {
    return CellBand::Step;
  }
  if (trigger >= collision::FirstConfirmTrigger && trigger < collision::FirstWarpTrigger) {
    return CellBand::Confirm;
  }
  if (trigger >= collision::FirstWarpTrigger &&
      trigger < collision::FirstWarpTrigger + collision::WarpTriggerCount) {
    return CellBand::Exit;
  }
  if (trigger == collision::ToiletTrigger) {
    return CellBand::Toilet;
  }
  return CellBand::Other;
}

debug::OverlayColor bandColor(CellBand band) {
  switch (band) {
  case CellBand::Wall:
    return overlayColor(220, 40, 40, 90);
  case CellBand::Step:
    return overlayColor(240, 200, 40, 110);
  case CellBand::Confirm:
    return overlayColor(40, 200, 240, 110);
  case CellBand::Exit:
    return overlayColor(40, 220, 80, 130);
  case CellBand::Toilet:
    return overlayColor(200, 80, 220, 130);
  case CellBand::Other:
    return overlayColor(170, 170, 170, 110);
  case CellBand::Free:
    break;
  }
  return 0;
}

void buildMapOverlay(const assets::MapDocument& map, const assets::MapCollision& grid,
                     const OverlayProjector& project, debug::OverlayShapes& out) {
  addCells(map, grid, project, out);
  addExits(map, project, out);
  addNpcs(map, project, out);
}

} // namespace dwe::app
