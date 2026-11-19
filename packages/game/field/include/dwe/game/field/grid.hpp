#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/core/math.hpp>

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace dwe::game::field {

// Movement and sight against a map's collision grid, in world space.

// Where a move ended and which of its two components a blocked cell stopped.
struct GridMove {
  glm::vec3 position{};
  bool blockedX = false;
  bool blockedZ = false;
};

// Moves a character of this radius by a ground displacement, X and then Z, so a move against a wall
// slides along it. The collision is the original's (map_collision.c: checkMapCollisionX/Z): moving
// along an axis, the line of cells at `radius` ahead must be free across `radius / 2` to each side.
// A component that would reach a blocked cell stops just before it, however long the step, so
// nothing tunnels at any tick rate. Cells outside the grid are blocked.
GridMove moveOnGrid(const assets::MapCollision& collision, glm::vec3 position,
                    glm::vec2 displacement, float radius);

// Whether a step collides (map.c: checkMapCollision, as entityCheckCollision tests it): the
// character is put the whole step ahead and, along each axis the step moves on, the line of cells
// `radius` ahead of it there must be free across `radius / 2` to each side. Components shorter than
// half a unit do not move the original's integer position, so they test nothing. The step is not
// walked: a step longer than a cell could pass a wall, which the original's steps never are.
bool stepCollides(const assets::MapCollision& collision, glm::vec3 position, glm::vec2 step,
                  float radius);

// Whether a character of this radius may stand here: no blocked cell under its probes on any side.
bool fitsOnGrid(const assets::MapCollision& collision, glm::vec3 position, float radius);

// An NPC the battle lets go of: where the fight left it, the spot its map record loads it at, its
// species' radius and whether the scene after the fight keeps it.
struct ReleasedNpc {
  glm::vec3 at{};
  glm::vec3 home{};
  float radius = 0.0F;
  // A fixed fight's enemy (battle_main.c:794 keeps it on the map): the script goes on with it
  // where the fight left it, as Birdramon's nest scene talks with it there.
  bool keptForScene = false;
};

// Where a released NPC stands; nothing when it stays where the fight left it, as the original
// leaves every fighter (battle_main.c:778). A fight can end with one on ground no NPC may stand on
// (inside a wall it crossed thin, past the map's edge) where the player could not reach it again:
// it goes back to its record's spot (an engine improvement, #163), unless the scene keeps it or
// that spot is no free ground either (a flier's record up in the sky).
std::optional<glm::vec3> releasedNpcSpot(const assets::MapCollision& collision,
                                         const ReleasedNpc& npc);

// Whether the straight ground line between two points is free of blocked cells along five parallel
// lines spread over `halfWidth` to each side (map.c: isFiveTileWidePathBlocked).
bool isLineClear(const assets::MapCollision& collision, glm::vec3 from, glm::vec3 to,
                 float halfWidth);

// Whether a cell may be part of a path.
using CellFilter = std::function<bool(assets::CollisionCell)>;

// Ground waypoints for a character of this radius from `from` to the centre of whichever of `goals`
// is nearest by path, the last waypoint being that centre; empty when none can be reached. The
// search runs on the points every half cell (cell centres, edges and corners) in 8 directions, and
// only takes a step that the character walks through without a blocked cell stopping it
// (moveOnGrid) even when it is `Leeway` off the line to either side, so a follower that is not
// exactly on the path still gets through. Two cells is the narrowest passage that allows: the
// probes span a whole cell across. Points next to walls cost more, so paths keep away from them.
// `passable` says which cells the path may touch (by default, those that are not blocked); the
// goals may always be touched.
std::vector<glm::vec3> findPath(const assets::MapCollision& collision, glm::vec3 from,
                                std::span<const assets::CollisionCell> goals, float radius,
                                const CellFilter& passable = {});

// How far off the line a path step must still be walkable.
inline constexpr float Leeway = 20.0F;
// How close to a waypoint a follower must get: a wall may stop it a hair before.
inline constexpr float WaypointReach = 0.5F;

} // namespace dwe::game::field
