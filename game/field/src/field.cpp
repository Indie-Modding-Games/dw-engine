#include <dwe/game/field/field.hpp>
#include <dwe/game/field/grid.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

namespace dwe::game::field {

namespace {

using assets::CollisionCell;
using scene::AnimationIntent;
using scene::Motion;
using scene::Transform;

constexpr float Turn = 2.0F * std::numbers::pi_v<float>;
constexpr std::int32_t FirstWarpTrigger = 110;
constexpr std::int32_t WarpTriggerCount = 10;
// A move that makes less than this fraction of its distance counts as stuck.
constexpr float StuckFraction = 0.1F;
// Click-to-move gives up on a waypoint after pushing against a wall this long.
constexpr float PathStallSeconds = 0.5F;
// How many cells around a clicked cell to look for one the player can get to.
constexpr int ClickSearchCells = 3;

glm::vec2 ground(glm::vec3 v) {
    return {v.x, v.z};
}

float groundDistance(glm::vec3 a, glm::vec3 b) {
    return glm::length(ground(a) - ground(b));
}

// Turns `from` toward `to` by at most `step` radians (any step when step <= 0), the short way.
float turnToward(float from, float to, float step) {
    const float delta = std::remainder(to - from, Turn);
    if (step <= 0.0F || std::abs(delta) <= step) {
        return std::remainder(from + delta, Turn);
    }
    return std::remainder(from + std::copysign(step, delta), Turn);
}

// tamer.c: loadMapEntities puts the partner 200 units toward the cardinal direction behind the player, in the
// original's axes, by bands of the player's rotation.
glm::ivec3 partnerSpawnOffset(std::int32_t rotation, std::int32_t distance) {
    rotation = ((rotation % 4096) + 4096) % 4096;
    if (rotation <= 0x200 || rotation > 0xE00) {
        return {0, 0, distance};
    }
    if (rotation <= 0x600) {
        return {distance, 0, 0};
    }
    if (rotation <= 0xA00) {
        return {0, 0, -distance};
    }
    return {-distance, 0, 0};
}

} // namespace

Field::Field(scene::World& world, FieldRules rules, std::vector<ScriptedWarp> scriptedWarps)
    : world_(&world), rules_(std::move(rules)), scriptedWarps_(std::move(scriptedWarps)), player_(world.create()),
      partner_(world.create()) {
    entt::registry& registry = world_->registry();
    registry.emplace<Transform>(player_);
    registry.emplace<scene::ModelRef>(player_, rules_.player.model);
    registry.emplace<AnimationIntent>(player_);
    registry.emplace<scene::PlayerTag>(player_);
    registry.emplace<Transform>(partner_);
    registry.emplace<scene::ModelRef>(partner_, rules_.partner.mover.model);
    registry.emplace<AnimationIntent>(partner_);
    registry.emplace<scene::PartnerTag>(partner_);
}

core::Result<void> Field::enterMap(const assets::MapDocument& map, std::int32_t exit) {
    const assets::MapWarp* warp = map.warp(exit);
    if (warp == nullptr) {
        return core::fail(std::format("map '{}' has no exit {}", map.id, exit));
    }
    enterMapAt(map, assets::warpSpawn(*warp));

    const glm::ivec3 offset =
        partnerSpawnOffset(warp->rotation, static_cast<std::int32_t>(std::lround(rules_.partner.spawnDistance)));
    const glm::vec3 position = assets::toWorldPosition(warp->spawn + offset);
    const assets::SpawnPoint spawn = assets::warpSpawn(*warp);
    if (fitsOnGrid(collision_, position, rules_.partner.mover.radius)) {
        scene::place(world_->registry().get<Transform>(partner_), position,
                     assets::yawOfDirection(spawn.position - position));
    }
    return {};
}

void Field::enterMapAt(const assets::MapDocument& map, assets::SpawnPoint spawn) {
    mapId_ = map.id;
    collision_ = map.collision;
    warps_ = map.warps;
    const glm::vec3 view =
        assets::toWorldPosition(map.camera.reference) - assets::toWorldPosition(map.camera.viewpoint);
    cameraForward_ = glm::length(ground(view)) > 0.0F ? glm::normalize(glm::vec3{view.x, 0.0F, view.z})
                                                      : glm::vec3{0.0F, 0.0F, -1.0F};

    entt::registry& registry = world_->registry();
    scene::place(registry.get<Transform>(player_), spawn.position, spawn.yaw);
    registry.get<AnimationIntent>(player_) = {};
    registry.get<AnimationIntent>(partner_) = {};

    moveTarget_.reset();
    path_.clear();
    pathStallSeconds_ = 0.0F;
    pendingChange_.reset();
    waitingForMap_ = false;
    triggerSteps_.clear();
    // The original has no guard against warping again at once; its spawns are never on triggers. Remembering the
    // value under the spawn makes a trigger fire only when stepped onto.
    lastPlayerCell_ = assets::collisionCellAt(spawn.position);
    lastTrigger_ = collision_.trigger(lastPlayerCell_).value_or(0);
    trail_.clear();
    partnerMotion_ = Motion::Idle;
    partnerStuckSeconds_ = 0.0F;
    placePartnerNearPlayer();
    partnerTeleports_ = 0;
}

void Field::setInput(const FieldInput& input) {
    input_ = input;
    if (input.cancelTarget) {
        moveTarget_.reset();
        path_.clear();
    }
    if (input.moveTarget && !mapChangePending()) {
        planPath(*input.moveTarget);
    }
}

void Field::tick(float seconds) {
    triggerSteps_.clear();
    entt::registry& registry = world_->registry();
    scene::beginTick(registry.get<Transform>(player_));
    scene::beginTick(registry.get<Transform>(partner_));
    if (mapChangePending() || mapId_.empty()) {
        registry.get<AnimationIntent>(player_) = {};
        registry.get<AnimationIntent>(partner_) = {};
        return;
    }
    movePlayer(seconds);
    checkTriggers();
    updateTrail();
    movePartner(seconds);
}

std::optional<MapChange> Field::takeMapChange() {
    if (!pendingChange_) {
        return std::nullopt;
    }
    waitingForMap_ = true;
    return std::exchange(pendingChange_, std::nullopt);
}

CharacterState Field::player() const {
    const auto& transform = world_->registry().get<Transform>(player_);
    return {.position = transform.position,
            .yaw = transform.yaw,
            .motion = world_->registry().get<AnimationIntent>(player_).motion};
}

CharacterState Field::partner() const {
    const auto& transform = world_->registry().get<Transform>(partner_);
    return {.position = transform.position,
            .yaw = transform.yaw,
            .motion = world_->registry().get<AnimationIntent>(partner_).motion};
}

bool Field::isWarpTrigger(std::int32_t value) const {
    if (value >= FirstWarpTrigger && value < FirstWarpTrigger + WarpTriggerCount) {
        const auto warp = std::ranges::find(warps_, value - FirstWarpTrigger, &assets::MapWarp::exit);
        if (warp != warps_.end() && warp->targetMapId) {
            return true;
        }
    }
    return std::ranges::any_of(scriptedWarps_, [&](const ScriptedWarp& scripted) {
        return scripted.map == mapId_ && scripted.trigger == value;
    });
}

void Field::planPath(glm::vec3 target) {
    moveTarget_.reset();
    path_.clear();
    pathStallSeconds_ = 0.0F;
    const CollisionCell clicked = assets::collisionCellAt(target);
    const glm::vec3 position = world_->registry().get<Transform>(player_).position;
    // Paths do not cross warps other than the one clicked.
    const auto passable = [this](CollisionCell cell) { return !isWarpTrigger(collision_.trigger(cell).value_or(0)); };
    // The clicked cell, or the nearest ring of cells around it with one the player can get to.
    for (int ring = 0; ring <= ClickSearchCells; ++ring) {
        std::vector<CollisionCell> goals;
        for (int dz = -ring; dz <= ring; ++dz) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) == ring) {
                    goals.push_back({.column = clicked.column + dx, .row = clicked.row + dz});
                }
            }
        }
        path_ = findPath(collision_, position, goals, rules_.player.radius, passable);
        if (path_.empty()) {
            continue;
        }
        // In the clicked cell, the last step goes from its centre to the clicked point if nothing is in the way.
        const glm::vec3 point{target.x, path_.back().y, target.z};
        const glm::vec3 centre = path_.back();
        const GridMove last = moveOnGrid(collision_, centre, ground(point) - ground(centre), rules_.player.radius);
        if (ring == 0 && !last.blockedX && !last.blockedZ) {
            path_.push_back(point);
        }
        moveTarget_ = path_.back();
        return;
    }
}

Field::Step Field::playerStep() {
    const float length = glm::length(input_.stick);
    if (length > rules_.input.deadZone) {
        moveTarget_.reset();
        path_.clear();
        const glm::vec3 right{-cameraForward_.z, 0.0F, cameraForward_.x};
        const glm::vec3 direction = (right * input_.stick.x) + (cameraForward_ * input_.stick.y);
        return {.direction = glm::normalize(ground(direction)), .run = input_.run};
    }
    if (!moveTarget_) {
        return {};
    }
    return {.direction = glm::vec2{0.0F}, .run = input_.run, .followPath = true};
}

void Field::movePlayer(float seconds) {
    entt::registry& registry = world_->registry();
    auto& transform = registry.get<Transform>(player_);
    auto& animation = registry.get<AnimationIntent>(player_);
    const Step step = playerStep();
    if (step.direction == glm::vec2{0.0F} && !step.followPath) {
        animation = {.motion = Motion::Idle, .speedScale = 1.0F};
        return;
    }
    const MoverRules& mover = rules_.player;
    const float expected = (step.run ? mover.runSpeed : mover.walkSpeed) * seconds;
    const glm::vec3 start = transform.position;
    glm::vec2 facing = step.direction;
    if (step.followPath) {
        facing = followPath(transform.position, expected);
        if (facing == glm::vec2{0.0F}) {
            animation = {.motion = Motion::Idle, .speedScale = 1.0F};
            return;
        }
    } else {
        transform.position =
            moveOnGrid(collision_, transform.position, step.direction * expected, mover.radius).position;
    }
    const float moved = groundDistance(transform.position, start);
    // The original faces the way the stick points even against a wall (tamer.c: setTamerDirection).
    transform.yaw =
        turnToward(transform.yaw, assets::yawOfDirection({facing.x, 0.0F, facing.y}), mover.turnSpeed * seconds);
    animation = {.motion = step.run ? Motion::Run : Motion::Walk,
                 .speedScale = expected > 0.0F ? std::clamp(moved / expected, 0.0F, 1.0F) : 1.0F};

    if (moveTarget_) {
        pathStallSeconds_ = moved < expected * StuckFraction ? pathStallSeconds_ + seconds : 0.0F;
        if (pathStallSeconds_ > PathStallSeconds) {
            pathStallSeconds_ = 0.0F;
            if (!path_.empty()) {
                path_.erase(path_.begin());
            }
            if (path_.empty()) {
                moveTarget_.reset();
            }
        }
    }
}

glm::vec2 Field::followPath(glm::vec3& position, float distance) {
    // Walks from waypoint to waypoint, carrying what is left of the tick's distance from one to the next, so the
    // player stays on the lines the path was planned on.
    glm::vec2 facing{0.0F};
    float left = distance;
    while (left > 0.0F && !path_.empty()) {
        const glm::vec3 aim = path_.front();
        const float away = groundDistance(aim, position);
        if (away <= WaypointReach) {
            path_.erase(path_.begin());
            continue;
        }
        facing = (ground(aim) - ground(position)) / away;
        const float length = std::min(left, away);
        const GridMove move = moveOnGrid(collision_, position, facing * length, rules_.player.radius);
        const bool blocked = move.blockedX || move.blockedZ;
        left -= blocked ? groundDistance(move.position, position) : length;
        position = move.position;
        if (!blocked) {
            if (length == away) {
                position.x = aim.x;
                position.z = aim.z;
                path_.erase(path_.begin());
            }
            continue;
        }
        if (groundDistance(aim, position) <= WaypointReach) {
            path_.erase(path_.begin());
            continue;
        }
        // A wall: sliding is all this tick can do. Pressed against it close to the end is there.
        if (path_.size() == 1 && groundDistance(aim, position) <= rules_.input.arriveDistance) {
            path_.clear();
        }
        break;
    }
    if (path_.empty()) {
        moveTarget_.reset();
    }
    return facing;
}

void Field::checkTriggers() {
    const glm::vec3 position = world_->registry().get<Transform>(player_).position;
    const CollisionCell cell = assets::collisionCellAt(position);
    const std::int32_t value = collision_.trigger(cell).value_or(0);
    if (value == lastTrigger_) {
        return;
    }
    lastTrigger_ = value;
    if (value == 0) {
        return;
    }
    if (value >= FirstWarpTrigger && value < FirstWarpTrigger + WarpTriggerCount) {
        const auto warp = std::ranges::find(warps_, value - FirstWarpTrigger, &assets::MapWarp::exit);
        const std::optional<std::string> target = warp != warps_.end() ? warp->targetMapId : std::nullopt;
        if (target) {
            pendingChange_ = MapChange{.map = *target, .exit = warp->targetExit, .trigger = value};
        }
    }
    if (!pendingChange_) {
        const auto scripted = std::ranges::find_if(
            scriptedWarps_, [&](const ScriptedWarp& warp) { return warp.map == mapId_ && warp.trigger == value; });
        if (scripted != scriptedWarps_.end()) {
            pendingChange_ = MapChange{.map = scripted->targetMap, .exit = scripted->targetExit, .trigger = value};
        }
    }
    if (pendingChange_) {
        moveTarget_.reset();
        path_.clear();
        return;
    }
    triggerSteps_.push_back({.trigger = value, .cell = cell});
}

void Field::updateTrail() {
    const glm::vec3 playerPosition = world_->registry().get<Transform>(player_).position;
    const glm::vec3 partnerPosition = world_->registry().get<Transform>(partner_).position;
    const CollisionCell cell = assets::collisionCellAt(playerPosition);
    if (cell == lastPlayerCell_) {
        return;
    }
    const float halfWidth = rules_.partner.sightHalfWidth;
    const auto visible = [&](glm::vec3 point) { return isLineClear(collision_, playerPosition, point, halfWidth); };
    // map.c: partnerTickCollision keeps a trail of the player's cells only while the partner cannot see the player.
    if (visible(partnerPosition)) {
        trail_.clear();
    } else {
        if (trail_.empty() || !visible(assets::collisionCellCenter(trail_.back()))) {
            trail_.push_back(lastPlayerCell_);
            if (std::cmp_greater(trail_.size(), rules_.partner.trailLength)) {
                trail_.pop_front();
            }
        }
        // A short cut: if an older cell is in sight, the newer ones are not needed.
        for (std::size_t back = 4; back >= 2; --back) {
            if (trail_.size() >= back && visible(assets::collisionCellCenter(trail_[trail_.size() - back]))) {
                trail_.resize(trail_.size() - back + 1);
                break;
            }
        }
    }
    lastPlayerCell_ = cell;
}

void Field::movePartner(float seconds) {
    entt::registry& registry = world_->registry();
    auto& transform = registry.get<Transform>(partner_);
    auto& animation = registry.get<AnimationIntent>(partner_);
    const glm::vec3 playerPosition = registry.get<Transform>(player_).position;
    const PartnerRules& rules = rules_.partner;

    // A waypoint is reached on its cell or within a radius of its centre, which a wide partner may not get closer to.
    while (!trail_.empty() &&
           (assets::collisionCellAt(transform.position) == trail_.front() ||
            groundDistance(transform.position, assets::collisionCellCenter(trail_.front())) <= rules.mover.radius)) {
        trail_.pop_front();
    }
    const float distance = groundDistance(transform.position, playerPosition);
    if (distance > rules.teleportDistance) {
        placePartnerNearPlayer();
        return;
    }
    const float radius = rules.mover.radius;
    Motion motion = Motion::Idle;
    if (distance > rules.runDistance * radius) {
        motion = Motion::Run;
    } else if (distance >= rules.walkDistance * radius || !trail_.empty()) {
        motion = Motion::Walk;
    }
    partnerMotion_ = motion;

    const glm::vec3 target = trail_.empty() ? playerPosition : assets::collisionCellCenter(trail_.front());
    const glm::vec2 toTarget = ground(target) - ground(transform.position);
    if (motion == Motion::Idle || glm::length(toTarget) == 0.0F) {
        // partner.c: the partner keeps looking at the player while it waits.
        transform.yaw = turnToward(transform.yaw, assets::yawOfDirection(playerPosition - transform.position),
                                   rules.mover.turnSpeed * seconds);
        animation = {.motion = Motion::Idle, .speedScale = 1.0F};
        partnerStuckSeconds_ = 0.0F;
        return;
    }
    const glm::vec2 direction = glm::normalize(toTarget);
    const float expected = (motion == Motion::Run ? rules.mover.runSpeed : rules.mover.walkSpeed) * seconds;
    const GridMove move =
        moveOnGrid(collision_, transform.position, direction * std::min(expected, glm::length(toTarget)), radius);
    const float moved = groundDistance(move.position, transform.position);
    transform.position = move.position;
    transform.yaw = turnToward(transform.yaw, assets::yawOfDirection({direction.x, 0.0F, direction.y}),
                               rules.mover.turnSpeed * seconds);
    animation = {.motion = motion, .speedScale = expected > 0.0F ? std::clamp(moved / expected, 0.0F, 1.0F) : 1.0F};

    partnerStuckSeconds_ = moved < expected * StuckFraction ? partnerStuckSeconds_ + seconds : 0.0F;
    if (!trail_.empty() && partnerStuckSeconds_ > PathStallSeconds) {
        // Against a wall on the way to a waypoint: head for the next one before giving up.
        trail_.pop_front();
        partnerStuckSeconds_ = 0.0F;
    } else if (partnerStuckSeconds_ > rules.stuckSeconds) {
        placePartnerNearPlayer();
    }
}

void Field::placePartnerNearPlayer() {
    entt::registry& registry = world_->registry();
    const auto& player = registry.get<Transform>(player_);
    const float radius = rules_.partner.mover.radius;
    std::vector<glm::vec3> candidates{player.position -
                                      (assets::forwardOfYaw(player.yaw) * rules_.partner.spawnDistance)};
    for (const CollisionCell cell : std::views::reverse(trail_)) {
        candidates.push_back(assets::collisionCellCenter(cell));
    }
    candidates.push_back(assets::collisionCellCenter(lastPlayerCell_));
    constexpr std::array<glm::vec2, 8> Around = {
        {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
    for (const glm::vec2 offset : Around) {
        candidates.push_back(player.position + (glm::vec3{offset.x, 0.0F, offset.y} * rules_.partner.spawnDistance));
    }
    glm::vec3 chosen = player.position;
    for (const glm::vec3 candidate : candidates) {
        if (fitsOnGrid(collision_, candidate, radius) && isLineClear(collision_, player.position, candidate, 0.0F)) {
            chosen = candidate;
            break;
        }
    }
    chosen.y = player.position.y;
    scene::place(registry.get<Transform>(partner_), chosen, assets::yawOfDirection(player.position - chosen));
    registry.get<AnimationIntent>(partner_) = {};
    trail_.clear();
    partnerStuckSeconds_ = 0.0F;
    ++partnerTeleports_;
}

} // namespace dwe::game::field
