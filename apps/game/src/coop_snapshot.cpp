#include "coop_snapshot.hpp"

#include <cmath>
#include <numbers>

namespace dwe::app {

namespace {
bool validPosition(const glm::vec3& position) {
  return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
         std::abs(position.x) <= game::coop::MaxWorldCoordinate &&
         std::abs(position.y) <= game::coop::MaxWorldCoordinate &&
         std::abs(position.z) <= game::coop::MaxWorldCoordinate;
}

std::int32_t quantizeCoordinate(float value) {
  return static_cast<std::int32_t>(std::lround(value));
}

game::coop::Motion motionOf(scene::Motion motion) {
  switch (motion) {
  case scene::Motion::Idle: return game::coop::Motion::Idle;
  case scene::Motion::Walk: return game::coop::Motion::Walk;
  case scene::Motion::Run: return game::coop::Motion::Run;
  }
  return game::coop::Motion::Idle;
}
} // namespace

core::Result<game::coop::EntityState> toCoopEntityState(
    std::uint16_t networkId, game::coop::PlayerId owner, game::coop::EntityRole role,
    const game::field::CharacterState& state) {
  constexpr double Tau = std::numbers::pi_v<double> * 2.0;
  if (networkId == 0 || owner > game::coop::GuestPlayerId ||
      static_cast<std::uint8_t>(role) > static_cast<std::uint8_t>(game::coop::EntityRole::Npc) ||
      !validPosition(state.position) || !std::isfinite(state.yaw)) {
    return core::fail("cooperative snapshot entity has invalid identity or transform");
  }

  double yaw = std::fmod(static_cast<double>(state.yaw), Tau);
  if (yaw < 0.0) yaw += Tau;
  const auto yawUnits = static_cast<std::uint32_t>(std::lround(yaw * 65536.0 / Tau));
  return game::coop::EntityState{
      .networkId = networkId,
      .owner = owner,
      .role = role,
      .x = quantizeCoordinate(state.position.x),
      .y = quantizeCoordinate(state.position.y),
      .z = quantizeCoordinate(state.position.z),
      .yaw = static_cast<std::uint16_t>(yawUnits & 0xFFFFU),
      .motion = motionOf(state.motion)};
}

core::Result<game::coop::Snapshot> makeCoopSnapshot(const game::field::Field& field,
                                                   std::string mapId,
                                                   std::uint32_t worldTick,
                                                   std::uint32_t acknowledgedCommand) {
  if (mapId.empty()) return core::fail("cooperative snapshot has no map id");
  game::coop::Snapshot snapshot{.mapId = std::move(mapId),
                                .worldTick = worldTick,
                                .acknowledgedCommand = acknowledgedCommand,
                                .entities = {}};
  auto host = toCoopEntityState(1, game::coop::HostPlayerId, game::coop::EntityRole::Player,
                                field.player());
  if (!host) return std::unexpected{host.error()};
  snapshot.entities.push_back(*host);
  auto partner = toCoopEntityState(2, game::coop::HostPlayerId, game::coop::EntityRole::Partner,
                                   field.partner());
  if (!partner) return std::unexpected{partner.error()};
  snapshot.entities.push_back(*partner);
  if (const auto guest = field.player(game::coop::GuestPlayerId)) {
    auto state = toCoopEntityState(3, game::coop::GuestPlayerId, game::coop::EntityRole::Player,
                                   *guest);
    if (!state) return std::unexpected{state.error()};
    snapshot.entities.push_back(*state);
  }
  return snapshot;
}

} // namespace dwe::app
