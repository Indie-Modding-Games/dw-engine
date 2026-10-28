#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/battle/effect_runner.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace dwe::app {

class FieldScene;

// The battle's fighters as the techniques' effects read them (game::battle::EffectBodies): the
// battle's places and facings, the species table's sizes, and the nodes of the models the field
// draws, all in the console's axes. The partner is fighter 0; enemy k is the NPC enemyNpcs[k - 1].
class BattleEffectBodies final : public game::battle::EffectBodies {
public:
  // All must outlive this; scene and species may be null (then the fighters stand at their places
  // with no nodes and no size).
  BattleEffectBodies(const game::battle::Battle& battle, const FieldScene* scene,
                     const assets::DigimonTable* species,
                     const std::vector<std::int32_t>& enemyNpcs)
      : battle_(&battle), scene_(scene), species_(species), enemyNpcs_(&enemyNpcs) {}

  [[nodiscard]] std::optional<game::battle::EffectBody>
  body(game::battle::FighterId fighter) const override;
  [[nodiscard]] std::optional<game::battle::EffectMatrix> node(game::battle::FighterId fighter,
                                                               std::int32_t index) const override;
  [[nodiscard]] std::vector<game::battle::FighterId> fighters() const override;

private:
  [[nodiscard]] std::optional<std::optional<std::int32_t>>
  npcOf(game::battle::FighterId fighter) const;

  const game::battle::Battle* battle_;
  const FieldScene* scene_;
  const assets::DigimonTable* species_;
  const std::vector<std::int32_t>* enemyNpcs_;
};

} // namespace dwe::app
