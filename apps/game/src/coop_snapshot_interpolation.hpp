#pragma once

#include <dwe/game/coop/protocol.hpp>

#include <glm/glm.hpp>

#include <optional>
#include <vector>

namespace dwe::app {

struct PresentedCoopEntity {
  std::uint16_t networkId = 0;
  game::coop::PlayerId owner = game::coop::HostPlayerId;
  game::coop::EntityRole role = game::coop::EntityRole::Npc;
  glm::vec3 position{};
  float yaw = 0.0F;
  game::coop::Motion motion = game::coop::Motion::Idle;
};

struct PresentedCoopWorld {
  std::string mapId;
  std::uint32_t worldTick = 0;
  std::uint32_t acknowledgedCommand = 0;
  std::vector<PresentedCoopEntity> entities;
};

// Keeps the two newest authoritative samples for render-only interpolation. It never advances or
// mutates the simulated Field; world/map changes replace the interpolation history immediately.
class CoopSnapshotInterpolation {
public:
  [[nodiscard]] bool push(game::coop::Snapshot snapshot);
  [[nodiscard]] std::optional<PresentedCoopWorld> sample(float alpha) const;
  void reset();

private:
  std::optional<game::coop::Snapshot> previous_;
  std::optional<game::coop::Snapshot> latest_;
};

} // namespace dwe::app
