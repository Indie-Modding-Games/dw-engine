#include "coop_snapshot.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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
  case scene::Motion::Idle:
    return game::coop::Motion::Idle;
  case scene::Motion::Walk:
    return game::coop::Motion::Walk;
  case scene::Motion::Run:
    return game::coop::Motion::Run;
  }
  return game::coop::Motion::Idle;
}
} // namespace

std::optional<game::coop::ActionClip> coopActionOf(std::optional<game::field::PlayedClip> clip) {
  if (!clip || clip->clip > std::numeric_limits<std::uint16_t>::max()) {
    return std::nullopt;
  }
  return game::coop::ActionClip{.clip = static_cast<std::uint16_t>(clip->clip),
                                .starts = static_cast<std::uint8_t>(clip->starts & 0xFFU)};
}

std::optional<game::field::PlayedClip> playedClipOf(std::optional<game::coop::ActionClip> action) {
  if (!action) {
    return std::nullopt;
  }
  return game::field::PlayedClip{.clip = action->clip, .starts = action->starts};
}

core::Result<game::coop::EntityState> toCoopEntityState(std::uint16_t networkId,
                                                        game::coop::PlayerId owner,
                                                        game::coop::EntityRole role,
                                                        const game::field::CharacterState& state) {
  constexpr double Tau = std::numbers::pi_v<double> * 2.0;
  if (networkId == 0 || !game::coop::validPlayer(owner) ||
      static_cast<std::uint8_t>(role) > static_cast<std::uint8_t>(game::coop::EntityRole::Npc) ||
      !validPosition(state.position) || !std::isfinite(state.yaw)) {
    return core::fail("cooperative snapshot entity has invalid identity or transform");
  }

  double yaw = std::fmod(static_cast<double>(state.yaw), Tau);
  if (yaw < 0.0)
    yaw += Tau;
  const auto yawUnits = static_cast<std::uint32_t>(std::lround(yaw * 65536.0 / Tau));
  return game::coop::EntityState{.networkId = networkId,
                                 .owner = owner,
                                 .role = role,
                                 .x = quantizeCoordinate(state.position.x),
                                 .y = quantizeCoordinate(state.position.y),
                                 .z = quantizeCoordinate(state.position.z),
                                 .yaw = static_cast<std::uint16_t>(yawUnits & 0xFFFFU),
                                 .motion = motionOf(state.motion),
                                 .action = std::nullopt};
}

core::Result<game::coop::Snapshot> makeCoopSnapshot(const game::field::Field& field,
                                                    std::string mapId, std::uint32_t worldTick,
                                                    std::uint32_t acknowledgedCommand,
                                                    std::span<const game::coop::PlayerId> away) {
  if (mapId.empty())
    return core::fail("cooperative snapshot has no map id");
  game::coop::Snapshot snapshot{.mapId = std::move(mapId),
                                .worldTick = worldTick,
                                .acknowledgedCommand = acknowledgedCommand,
                                .entities = {}};
  auto host = toCoopEntityState(1, game::coop::HostPlayerId, game::coop::EntityRole::Player,
                                field.player());
  if (!host)
    return std::unexpected{host.error()};
  host->action = coopActionOf(field.scriptedClipOf(field.playerEntity()));
  snapshot.entities.push_back(*host);
  auto partner = toCoopEntityState(2, game::coop::HostPlayerId, game::coop::EntityRole::Partner,
                                   field.partner());
  if (!partner)
    return std::unexpected{partner.error()};
  partner->action = coopActionOf(field.scriptedClipOf(field.partnerEntity()));
  snapshot.entities.push_back(*partner);
  // Every guest body in the host's field, and its partner: ids 2n+1 and 2n+2 for player n.
  for (game::coop::PlayerId id = game::coop::GuestPlayerId; game::coop::validPlayer(id); ++id) {
    const auto playerId = static_cast<std::uint16_t>((id * 2) + 1);
    if (std::ranges::contains(away, id)) {
      continue;
    }
    if (const auto guest = field.player(id)) {
      auto state = toCoopEntityState(playerId, id, game::coop::EntityRole::Player, *guest);
      if (!state)
        return std::unexpected{state.error()};
      if (const auto entity = field.playerEntity(id)) {
        state->action = coopActionOf(field.scriptedClipOf(*entity));
      }
      snapshot.entities.push_back(*state);
    }
    if (const auto guestPartner = field.partner(id)) {
      auto state = toCoopEntityState(static_cast<std::uint16_t>(playerId + 1), id,
                                     game::coop::EntityRole::Partner, *guestPartner);
      if (!state)
        return std::unexpected{state.error()};
      if (const auto entity = field.partnerEntity(id)) {
        state->action = coopActionOf(field.scriptedClipOf(*entity));
      }
      snapshot.entities.push_back(*state);
    }
  }
  // None when the NPCs don't all fit one datagram, so a guest never takes a listed one for gone.
  const std::size_t players = snapshot.entities.size();
  snapshot.npcsWhole = true;
  for (const game::field::NpcPose& pose : field.npcs().poses()) {
    if (snapshot.entities.size() >= game::coop::MaxSnapshotEntities ||
        pose.slot > std::numeric_limits<std::uint16_t>::max() - game::coop::NpcNetworkBase) {
      snapshot.npcsWhole = false;
      break;
    }
    auto state =
        toCoopEntityState(static_cast<std::uint16_t>(game::coop::NpcNetworkBase + pose.slot),
                          game::coop::HostPlayerId, game::coop::EntityRole::Npc,
                          {.position = pose.position, .yaw = pose.yaw, .motion = pose.motion});
    if (state) {
      snapshot.entities.push_back(*state);
    }
  }
  if (game::coop::encode(snapshot).size() + 1 > net::MaxDatagram) {
    snapshot.entities.resize(players);
    snapshot.npcsWhole = false;
  }
  return snapshot;
}

} // namespace dwe::app
