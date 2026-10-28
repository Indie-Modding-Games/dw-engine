#include <dwe/core/log.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/field/grid.hpp>

#include <algorithm>
#include <format>
#include <utility>

// Where the characters stand against each other (docs/modulos/game/field.md, "Posiciones"): the
// overlap watch and the release rule, both the engine's.
namespace dwe::game::field {

namespace {

using scene::Transform;

// The part of two squares' summed half sides they must overlap by, on both axes, to stand in
// each other.
constexpr float DeepOverlap = 0.5F;
// How long two characters may stand in each other while the player plays before the watch logs
// it: a player walking through the partner takes about half a second.
constexpr float OverlapGraceSeconds = 1.0F;

glm::vec2 ground(glm::vec3 v) {
  return {v.x, v.z};
}

} // namespace

bool Field::overlaps(const GroundSquare& a, const GroundSquare& b) {
  return squaresOverlap(a, b, (a.half + b.half) * DeepOverlap);
}

std::vector<Field::Body> Field::bodies() const {
  std::vector<Body> bodies{
      {.entity = player_, .half = rules_.player.radius, .who = 0},
      {.entity = partner_, .half = rules_.partner.mover.radius, .who = 1},
  };
  for (const NpcBody& npc : npcs_.bodies()) {
    bodies.push_back({.entity = npc.entity, .half = npc.half, .who = npc.scriptId + 2});
  }
  return bodies;
}

std::string Field::nameOf(const Body& body) const {
  if (body.who == 0) {
    return "player";
  }
  if (body.who == 1) {
    return "partner";
  }
  return std::format("npc{}", body.who - 2);
}

GroundSquare Field::squareOf(const Body& body) const {
  return {.centre = ground(world_->registry().get<Transform>(body.entity).position),
          .half = body.half};
}

bool Field::inWall(glm::vec3 position) const {
  return collision_.isBlocked(assets::collisionCellAt(position));
}

void Field::watchOverlaps(float seconds) {
  const std::vector<Body> all = bodies();
  std::vector<OverlapWatch> now;
  const auto note = [&](scene::Entity a, scene::Entity b, std::string names) {
    const auto old = std::ranges::find_if(
        overlapWatch_, [&](const OverlapWatch& watch) { return watch.a == a && watch.b == b; });
    OverlapWatch watch = old != overlapWatch_.end()
                             ? *old
                             : OverlapWatch{.a = a, .b = b, .names = std::move(names)};
    ++watch.ticks;
    watch.seconds += seconds;
    if (!watch.reported && watch.seconds > OverlapGraceSeconds) {
      watch.reported = true;
      ++overlapEpisodes_;
      core::logWarning("[positions] overlap {} ticks={} on {}", watch.names, watch.ticks, mapId_);
    }
    now.push_back(std::move(watch));
  };
  for (std::size_t i = 0; i < all.size(); ++i) {
    const GroundSquare square = squareOf(all[i]);
    if (inWall(world_->registry().get<Transform>(all[i].entity).position)) {
      note(all[i].entity, entt::null, std::format("a={} b=wall", nameOf(all[i])));
    }
    for (std::size_t j = i + 1; j < all.size(); ++j) {
      if (overlaps(square, squareOf(all[j]))) {
        note(all[i].entity, all[j].entity,
             std::format("a={} b={}", nameOf(all[i]), nameOf(all[j])));
      }
    }
  }
  for (const OverlapWatch& watch : overlapWatch_) {
    const bool goesOn = std::ranges::any_of(
        now, [&](const OverlapWatch& kept) { return kept.a == watch.a && kept.b == watch.b; });
    if (!goesOn && watch.reported) {
      core::logDebug("[positions] overlap over {} ticks={}", watch.names, watch.ticks);
    }
  }
  overlapWatch_ = std::move(now);
}

} // namespace dwe::game::field
