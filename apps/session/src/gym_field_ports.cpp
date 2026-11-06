#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/session/gym_field_ports.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::session {
namespace {

using game::gym::Actor;
using game::gym::Award;
using game::gym::CourseRules;
using game::gym::GymStats;
using game::gym::Point;

constexpr std::string_view GymTrainFailed = "game: gym: training failed: {}";
// How far from where the camera looks the gym's view reaches, in world units.
constexpr float OffScreenDistance = 1800.0F;

// A world point in the original's axes, as the courses read the partner's position.
Point originalOf(glm::vec3 world) {
  return Point{.x = static_cast<std::int32_t>(world.x), .z = static_cast<std::int32_t>(-world.z)};
}

GymStats statsOf(const game::partner::StatBlock& block) {
  GymStats stats{};
  for (std::size_t stat = 0; stat < stats.size(); ++stat) {
    stats.at(stat) = block.values.at(stat);
  }
  return stats;
}

std::string_view nameOf(Actor actor) {
  return actor == Actor::Tamer ? "player" : "partner";
}

std::size_t indexOf(Actor actor) {
  return actor == Actor::Tamer ? 0 : 1;
}

} // namespace

glm::vec3 gymWorldOf(Point spot, float height) {
  glm::vec3 world = assets::toWorldPosition({spot.x, 0, spot.z});
  world.y = height;
  return world;
}

GymFieldPorts::GymFieldPorts(const GymFieldPortsDesc& desc)
    : desc_(desc), random_(static_cast<std::uint_fast32_t>(desc.session->partner().randomState())) {
}

void GymFieldPorts::setTraining(bool training) {
  // The player's state 8 and partner.c: partnerSetState(10): both stand for the course. A course
  // the console starts finds the field free and holds it itself.
  game::field::Field& field = this->field();
  if (training) {
    ownHold_ = !field.heldByScript();
    field.holdForScript(true);
    return;
  }
  for (const Actor actor : std::array{Actor::Tamer, Actor::Partner}) {
    field.driveByScript(entityOf(actor), {});
    field.releaseClipByScript(entityOf(actor));
  }
  walks_ = {};
  clips_ = {};
  if (std::exchange(ownHold_, false)) {
    field.holdForScript(false);
  }
}

void GymFieldPorts::animate(Actor actor, std::int32_t animation) {
  if (animation < 0) {
    return;
  }
  if (actor == Actor::Teacher) {
    animateTeacher(animation);
    return;
  }
  clips_.at(indexOf(actor)) = static_cast<std::uint32_t>(animation);
  field().animateByScript(entityOf(actor), static_cast<std::uint32_t>(animation));
}

void GymFieldPorts::holdInPlace(Actor actor, bool held) {
  if (const std::optional<scene::Entity> entity = anyEntityOf(actor)) {
    field().holdClipInPlace(*entity, held);
  }
}

void GymFieldPorts::face(Actor actor, std::int32_t angle) {
  if (actor != Actor::Teacher) {
    field().turnByScript(entityOf(actor), assets::yawFromOriginal(angle));
  }
}

void GymFieldPorts::lookAt(Actor actor, Actor target) {
  // map.c: entityLookAtLocation.
  const std::optional<scene::Entity> entity = anyEntityOf(actor);
  const std::optional<glm::vec3> from = anyPositionOf(actor);
  const std::optional<glm::vec3> to = anyPositionOf(target);
  if (!entity || !from || !to) {
    return;
  }
  glm::vec3 toward = *to - *from;
  toward.y = 0.0F;
  if (toward.x != 0.0F || toward.z != 0.0F) {
    field().turnByScript(*entity, assets::yawOfDirection(toward));
  }
}

void GymFieldPorts::faceEachOther() {
  // map.c: entityLookAtLocation, both ways.
  game::field::Field& field = this->field();
  const glm::vec3 toPartner = field.partner().position - field.player().position;
  if (toPartner.x != 0.0F || toPartner.z != 0.0F) {
    field.turnByScript(field.playerEntity(), assets::yawOfDirection(toPartner));
    field.turnByScript(field.partnerEntity(), assets::yawOfDirection(-toPartner));
  }
}

bool GymFieldPorts::walk(Actor actor, std::optional<Point> spot) {
  // The original's tickEntityWalkTo faces the target every frame and walks at the walk clip's
  // speed; it ends on the spot's cell, or, walking back, on touching the player.
  game::field::Field& field = this->field();
  const scene::Entity entity = entityOf(actor);
  const glm::vec3 position = positionOf(actor);
  const bool toPlayer = !spot.has_value();
  const glm::vec3 target = toPlayer ? field.player().position : gymWorldOf(*spot, position.y);
  glm::vec3 toward = target - position;
  toward.y = 0.0F;
  const float distance = glm::length(toward);
  const game::field::FieldRules& rules = field.rules();
  const float touch = rules.player.radius + rules.partner.mover.radius;
  const bool there = toPlayer
                         ? distance <= touch
                         : assets::collisionCellAt(position) == assets::collisionCellAt(target);
  if (there) {
    field.driveByScript(entity, {});
    walks_.at(indexOf(actor)) = false;
    return true;
  }
  if (!std::exchange(walks_.at(indexOf(actor)), true)) {
    core::logDebug("[port:gym] tickEntityWalkTo({}, {:.0f}, {:.0f})", nameOf(actor), target.x,
                   target.z);
  }
  const float yaw = assets::yawOfDirection(toward);
  field.turnByScript(entity, yaw);
  field.driveByScript(entity, assets::forwardOfYaw(yaw) * walkSpeed(actor));
  return false;
}

void GymFieldPorts::moveCamera(std::optional<Point> spot, std::int32_t speed) {
  // map.c: tickCameraMovement steps the camera to the target in `speed` frames.
  game::field::Field& field = this->field();
  const glm::vec3 focus =
      spot ? gymWorldOf(*spot, field.player().position.y) : field.player().position;
  const double framesPerSecond = session().clock().rules().framesPerSecond;
  field.panCameraByScript(focus, static_cast<float>(speed / framesPerSecond));
}

void GymFieldPorts::playSound(std::int32_t slot, std::int32_t sound) {
  if (game::story::SoundPort* port = session().sound(); port != nullptr) {
    port->playSound(slot, sound);
  }
}

std::optional<std::int64_t> GymFieldPorts::loopSound(std::int32_t slot, std::int32_t sound) {
  // sound.c: playSound2: the voices are kept until thunkStopSoundMask stops them.
  game::story::SoundPort* port = session().sound();
  if (port == nullptr) {
    return std::nullopt;
  }
  const std::optional<std::int64_t> held = port->playHeldSound(slot, sound);
  if (held) {
    held_.push_back(*held);
  }
  return held;
}

void GymFieldPorts::stopSound(std::int64_t handle) {
  if (game::story::SoundPort* port = session().sound(); port != nullptr) {
    port->stopHeldSound(handle);
  }
  std::erase(held_, handle);
}

void GymFieldPorts::stopHeldSounds() {
  for (const std::int64_t handle : std::vector<std::int64_t>{held_}) {
    stopSound(handle);
  }
}

std::optional<Point> GymFieldPorts::partnerPosition() {
  return originalOf(field().partner().position);
}

void GymFieldPorts::movePartner(Point delta) {
  game::field::Field& field = this->field();
  field.placeByScript(field.partnerEntity(), field.partner().position + gymWorldOf(delta, 0.0F));
}

bool GymFieldPorts::partnerOnScreen() {
  // entity.isOnScreen, without the camera's projection: the partner is off screen once it is
  // farther from where the camera looks than the gym's view reaches.
  const game::field::Field& field = this->field();
  glm::vec3 apart =
      field.partner().position - field.scriptCamera().value_or(field.player().position);
  apart.y = 0.0F;
  return glm::length(apart) <= OffScreenDistance;
}

void GymFieldPorts::placePartner(game::gym::Axis axis, std::int32_t value) {
  // In the original's axes: its z is the world's -z.
  game::field::Field& field = this->field();
  glm::vec3 position = field.partner().position;
  if (axis == game::gym::Axis::X) {
    position.x = static_cast<float>(value);
  } else {
    position.z = -static_cast<float>(value);
  }
  field.placeByScript(field.partnerEntity(), position);
}

std::string GymFieldPorts::partnerSpecies() {
  return session().partner().state().species;
}

void GymFieldPorts::closeTextbox() {
  if (desc_.story != nullptr) {
    static_cast<void>(desc_.story->dialogue().closeBox(0));
  }
}

std::int32_t GymFieldPorts::randomBelow(std::int32_t limit) {
  return std::uniform_int_distribution<std::int32_t>{0, limit - 1}(random_);
}

Award GymFieldPorts::award(const CourseRules& course, std::int32_t slotTenths) {
  GameSession& session = this->session();
  // TRN_saveBaseStats: the box counts from the stats before the session.
  Award award{.before = statsOf(session.partner().state().stats.base), .gains = {}};
  const game::partner::TrainingRules& training = session.partnerRules().training;
  bool upgraded = false;
  if (const auto* place = training.findPlace(course.place);
      place != nullptr && desc_.story != nullptr) {
    if (const auto* trained = place->findCourse(course.gains); trained != nullptr) {
      const auto flag = training.upgradeFlags[training.multiplierStat(*trained)];
      const auto set = desc_.story->state().trigger(flag);
      upgraded = set && *set;
    }
  }
  bool manual = false;
  if (desc_.inventory != nullptr) {
    if (const auto item = desc_.inventory->items().handleOf(desc_.rules->manualItem)) {
      manual = desc_.inventory->inventory().countOf(*item) > 0;
    }
  }
  auto trained = session.train({.place = course.place,
                                .course = course.gains,
                                .countsPractice = course.countsPractice,
                                .slotTenths = slotTenths,
                                .gymUpgraded = upgraded,
                                .hasTrainingManual = manual});
  if (!trained) {
    core::logWarning("{}",
                     std::vformat(GymTrainFailed, std::make_format_args(trained.error().message)));
    return award;
  }
  award.gains = statsOf(trained->gains);
  core::logDebug("[port:gym] TRN_award*TrainingGains (trn_reward.c): course {}, upgraded {}, "
                 "manual {}, slot {}",
                 course.id, upgraded, manual, slotTenths);
  awarded(course, award);
  return award;
}

scene::Entity GymFieldPorts::entityOf(Actor actor) const {
  const game::field::Field& field = this->field();
  return actor == Actor::Tamer ? field.playerEntity() : field.partnerEntity();
}

glm::vec3 GymFieldPorts::positionOf(Actor actor) const {
  const game::field::Field& field = this->field();
  return actor == Actor::Tamer ? field.player().position : field.partner().position;
}

std::optional<std::int32_t> GymFieldPorts::teacherId() const {
  const assets::MapNpc* record = field().npcs().record(0);
  return record != nullptr ? std::optional{record->scriptId} : std::nullopt;
}

std::optional<scene::Entity> GymFieldPorts::anyEntityOf(Actor actor) const {
  if (actor != Actor::Teacher) {
    return entityOf(actor);
  }
  const auto id = teacherId();
  return id ? field().npcs().entityOf(*id) : std::nullopt;
}

std::optional<glm::vec3> GymFieldPorts::anyPositionOf(Actor actor) const {
  if (actor != Actor::Teacher) {
    return positionOf(actor);
  }
  const auto id = teacherId();
  return id ? field().npcs().positionOf(*id) : std::nullopt;
}

void GymFieldPorts::animateTeacher(std::int32_t animation) {
  game::field::Field& field = this->field();
  const auto id = teacherId();
  const auto entity = anyEntityOf(Actor::Teacher);
  if (!id || !entity || !field.npcs().setScriptedAnimation(*id, animation)) {
    return;
  }
  if (animation == 0) {
    field.releaseClipByScript(*entity);
  } else {
    field.animateByScript(*entity, static_cast<std::uint32_t>(animation));
  }
}

float GymFieldPorts::walkSpeed(Actor actor) const {
  const game::field::FieldRules& rules = field().rules();
  const float fallback =
      actor == Actor::Tamer ? rules.player.walkSpeed : rules.partner.mover.walkSpeed;
  const std::optional<std::uint32_t> clip = clips_.at(indexOf(actor));
  const float speed = clip ? clipGroundSpeed(entityOf(actor), *clip) : 0.0F;
  return speed > 0.0F ? speed : fallback;
}

} // namespace dwe::session
