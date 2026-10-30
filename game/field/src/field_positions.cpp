#include <dwe/core/log.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/field/grid.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
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
// The release rule's search: rings this far apart around where the character stood, out to this
// many (six cells).
constexpr float SpotStep = 25.0F;
constexpr int SpotRings = 24;
constexpr float Turn = 2.0F * std::numbers::pi_v<float>;
// Body::who: the player, the partner, then the NPCs (script id + 2).
constexpr std::int64_t PlayerWho = 0;
constexpr std::int64_t PartnerWho = 1;
constexpr std::int64_t FirstNpcWho = 2;

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

void Field::settle() {
  beginTicks(tickSeconds_);
  playerGlide_.on = false;
  partnerGlide_.on = false;
}

void Field::beginTicks(float seconds) {
  entt::registry& registry = world_->registry();
  scene::beginTick(registry.get<Transform>(player_));
  scene::beginTick(registry.get<Transform>(partner_));
  npcs_.beginTick();
  playerGlide_.seconds += tickSeconds_;
  partnerGlide_.seconds += tickSeconds_;
  tickSeconds_ = seconds;
}

void Field::setPartnerRadius(float radius) {
  if (radius <= 0.0F || radius == rules_.partner.mover.radius) {
    return;
  }
  core::logDebug("[positions] the partner's radius {:.0f} -> {:.0f} (its species')",
                 rules_.partner.mover.radius, radius);
  rules_.partner.mover.radius = radius;
  follow_.setRadius(static_cast<std::int32_t>(std::lround(radius)));
  requestSeparation("partner size");
}

void Field::releaseHandoff(std::string_view why) {
  // Where the partner followed the player before the hold says nothing now: the trail, the stuck
  // time and the player's last cell start over, as on entering a map.
  const glm::vec3 player = world_->registry().get<Transform>(player_).position;
  follow_.forgetTrail({.x = static_cast<std::int32_t>(std::lround(player.x)),
                       .z = static_cast<std::int32_t>(std::lround(-player.z))});
  partnerStuckSeconds_ = 0.0F;
  lastPlayerCell_ = assets::collisionCellAt(player);
  requestSeparation(why);
}

void Field::placeAt(scene::Entity entity, glm::vec3 position, float yaw) {
  scene::place(world_->registry().get<Transform>(entity), position, yaw);
  releaseHandoff("place");
  // The place is exact: a comparison with the original starts from it, and the original moves
  // nobody off where its trace starts.
  separationDue_.reset();
  partnerMakesRoom_ = false;
}

void Field::requestSeparation(std::string_view why) {
  if (!separationDue_) {
    separationDue_ = std::string{why};
  } else if (!separationDue_->contains(why)) {
    separationDue_->append(", ").append(why);
  }
}

bool Field::freeFor(const Body& self, glm::vec3 at, std::span<const Body> others) const {
  if (!fitsOnGrid(collision_, at, self.half) ||
      warpsOn(collision_.trigger(assets::collisionCellAt(at)).value_or(0))) {
    return false;
  }
  const GroundSquare square{.centre = ground(at), .half = self.half};
  return std::ranges::none_of(others, [&](const Body& other) {
    return other.entity != self.entity && squaresOverlap(square, squareOf(other));
  });
}

std::optional<glm::vec3> Field::freeSpotNear(const Body& self, glm::vec3 from, glm::vec2 away,
                                             bool walled, std::span<const Body> others) const {
  const float base = glm::length(away) > 0.0F ? std::atan2(away.y, away.x) : 0.0F;
  for (int ring = 1; ring <= SpotRings; ++ring) {
    const float distance = SpotStep * static_cast<float>(ring);
    const int count = std::max(8, static_cast<int>(std::ceil(Turn * distance / SpotStep)));
    // The spots straight away from what it stands in first, then alternately to either side.
    for (int index = 0; index < count; ++index) {
      const int side = (index + 1) / 2;
      const float angle = base + (static_cast<float>(index % 2 == 0 ? side : -side) * Turn /
                                  static_cast<float>(count));
      const glm::vec3 spot = from + (glm::vec3{std::cos(angle), 0.0F, std::sin(angle)} * distance);
      // Out of a wall it may cross the wall's cells; otherwise it does not go through a wall.
      if (freeFor(self, spot, others) && (walled || isLineClear(collision_, from, spot, 0.0F))) {
        return spot;
      }
    }
  }
  return std::nullopt;
}

std::int32_t Field::separateActors(std::string_view why) {
  entt::registry& registry = world_->registry();
  const std::vector<Body> all = bodies();
  std::int32_t moved = 0;
  for (std::size_t index = 0; index < all.size(); ++index) {
    const Body& body = all[index];
    // An NPC stays where the map, its path or a script put it, even on a wall cell or in someone:
    // the original never moves an NPC it does not walk (measured: Kunemon on its bed in mayo02
    // keeps (143, -1000) through a talk and 300 frames after; room02's Agumon stands behind the
    // counter, on wall cells too). The others step around it.
    if (body.who >= FirstNpcWho) {
      continue;
    }
    auto& transform = registry.get<Transform>(body.entity);
    const bool walled = inWall(transform.position);
    // The player gives way to nobody; the partner to everybody.
    std::optional<std::size_t> under;
    if (body.who == PartnerWho) {
      for (std::size_t other = 0; other < all.size() && !under; ++other) {
        if (other != index && overlaps(squareOf(body), squareOf(all[other]))) {
          under = other;
        }
      }
    }
    if (!walled && !under) {
      continue;
    }
    const std::string reason = walled ? std::string{"in a wall"} : nameOf(all[*under]);
    const glm::vec2 away =
        under ? ground(transform.position) - squareOf(all[*under]).centre : glm::vec2{0.0F};
    const std::optional<glm::vec3> spot = freeSpotNear(body, transform.position, away, walled, all);
    if (!spot) {
      core::logWarning("[positions] release ({}): no free spot near {} ({}); it stays", why,
                       nameOf(body), reason);
      continue;
    }
    core::logDebug("[positions] release ({}): {} from ({:.0f}, {:.0f}) to ({:.0f}, {:.0f}): {}",
                   why, nameOf(body), transform.position.x, transform.position.z, spot->x, spot->z,
                   reason);
    scene::place(transform, {spot->x, transform.position.y, spot->z}, transform.yaw);
    ++moved;
    if (body.who == PlayerWho) {
      lastPlayerCell_ = assets::collisionCellAt(*spot);
      lastTrigger_ = collision_.trigger(lastPlayerCell_).value_or(0);
      moveTarget_.reset();
      path_.clear();
    } else {
      partnerStuckSeconds_ = 0.0F;
      partnerMakesRoom_ = false;
      registry.get<scene::AnimationIntent>(partner_) = {};
    }
  }
  return moved;
}

void Field::watchOverlaps(float seconds) {
  const std::vector<Body> all = bodies();
  std::vector<OverlapWatch> now;
  const auto note = [&](scene::Entity a, scene::Entity b, std::string names) {
    const auto old = std::ranges::find_if(
        overlapWatch_, [&](const OverlapWatch& watch) { return watch.a == a && watch.b == b; });
    OverlapWatch watch =
        old != overlapWatch_.end() ? *old : OverlapWatch{.a = a, .b = b, .names = std::move(names)};
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
    // An NPC on a wall cell stands where the original keeps it (separateActors).
    if (all[i].who < FirstNpcWho &&
        inWall(world_->registry().get<Transform>(all[i].entity).position)) {
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
