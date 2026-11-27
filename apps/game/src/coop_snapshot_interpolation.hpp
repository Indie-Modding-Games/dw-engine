#pragma once

#include <dwe/game/coop/authority.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/net/peer_link.hpp>

#include <glm/glm.hpp>

#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace dwe::app {

struct PresentedCoopEntity {
  std::uint16_t networkId = 0;
  game::coop::PlayerId owner = game::coop::HostPlayerId;
  game::coop::EntityRole role = game::coop::EntityRole::Npc;
  glm::vec3 position{};
  float yaw = 0.0F;
  game::coop::Motion motion = game::coop::Motion::Idle;
  // The clip a script plays on it, as the newest snapshot says.
  std::optional<game::coop::ActionClip> action = std::nullopt;
};

struct PresentedCoopWorld {
  std::string mapId;
  std::uint32_t worldTick = 0;
  std::uint32_t acknowledgedCommand = 0;
  std::vector<PresentedCoopEntity> entities;
};

// The host's snapshots as a guest draws them (#354): every entity on a track of its own
// (game::coop::PoseTrack) played a little behind the newest snapshot on the guest's clock, so the
// motion is smooth whatever the network's jitter and the local frame rate. Late or repeated
// snapshots are dropped; a map change starts every track over. It never touches the guest's field.
class CoopSnapshotInterpolation {
public:
  // The host stamps a snapshot every SnapshotTicks of its 60 Hz field: its world tick times this
  // is the host's clock the tracks play by.
  static constexpr net::Millis SnapshotPeriod{50};

  // A snapshot received `now`. False for one not newer than the newest kept.
  [[nodiscard]] bool push(game::coop::Snapshot snapshot, net::Millis now);
  // The entities of the newest snapshot, each where its track draws it at `now`.
  [[nodiscard]] std::optional<PresentedCoopWorld> sample(net::Millis now) const;
  // Presence as the guest knows it: the players the newest snapshot shows on `mapId` (the host's
  // map; none on any other), by id.
  [[nodiscard]] std::vector<game::coop::PlayerId> playersOn(std::string_view mapId) const;
  void reset();

private:
  std::optional<game::coop::Snapshot> latest_;
  std::map<std::uint16_t, game::coop::PoseTrack> tracks_;
};

} // namespace dwe::app
