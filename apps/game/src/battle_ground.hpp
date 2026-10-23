#pragma once

#include <dwe/game/battle/ground.hpp>

#include <cstdint>
#include <optional>

namespace dwe::app {

class FieldScene;

// The ground of a battle on a field map (map.c: entityCheckCollision): the map camera frames the
// combat area and the map's collision grid stops the steps. The scene must outlive this object.
class FieldBattleGround final : public game::battle::BattleGround {
public:
  explicit FieldBattleGround(const FieldScene& scene) : scene_(&scene) {}

  [[nodiscard]] std::optional<game::battle::ScreenPoint>
  screenPointOf(game::battle::Point ground, std::int32_t height) const override;
  [[nodiscard]] bool blocksStep(game::battle::Point from, game::battle::Point to,
                                std::int32_t radius) const override;

private:
  const FieldScene* scene_;
};

} // namespace dwe::app
