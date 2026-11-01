#pragma once

#include <dwe/game/battle/ground.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

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

// The field's ground of one battle, which also knows the fighters' models: the partner and each
// enemy's NPC (enemyNpcs[k - 1] for enemy k), as the field draws them. Both must outlive this.
class FighterGround final : public game::battle::BattleGround {
public:
  FighterGround(const game::battle::BattleGround& ground, const FieldScene& scene,
                const std::vector<std::int32_t>& enemyNpcs)
      : ground_(&ground), scene_(&scene), enemyNpcs_(&enemyNpcs) {}

  [[nodiscard]] std::optional<game::battle::ScreenPoint>
  screenPointOf(game::battle::Point ground, std::int32_t height) const override {
    return ground_->screenPointOf(ground, height);
  }
  [[nodiscard]] bool blocksStep(game::battle::Point from, game::battle::Point to,
                                std::int32_t radius) const override {
    return ground_->blocksStep(from, to, radius);
  }
  [[nodiscard]] std::optional<game::battle::ScreenPoint>
  fighterNodeOnScreen(std::size_t fighter, std::uint32_t node) const override;

private:
  const game::battle::BattleGround* ground_;
  const FieldScene* scene_;
  const std::vector<std::int32_t>* enemyNpcs_;
};

} // namespace dwe::app
