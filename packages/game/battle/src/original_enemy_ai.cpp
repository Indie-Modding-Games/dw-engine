#include <array>
#include <cstddef>
#include <optional>

#include "original_strategies.hpp"
#include "targeting.hpp"

namespace dwe::game::battle {

namespace {

// battle_main.c: BTL_selectEnemyMove: the NPC record's priority of each slot, none for an unusable
// move, more for an area move while another enemy stands (unless it only targets the partner) and
// for a support move while its priority lasts. An empty slot keeps its priority, and picking it
// means waiting.
class OriginalEnemyAi final : public EnemyAi {
public:
  MoveDecision chooseMove(const BattleView& view, FighterId selfId,
                          const std::array<bool, EquippedSlots>& usable) override {
    const Fighter& self = view.state->fighters.at(selfId);
    std::array<std::int32_t, EquippedSlots> weights = self.combatant.movePriorities;
    bool anotherEnemyStands = false;
    for (FighterId other = 1; other < view.state->fighters.size(); ++other) {
      if (other != selfId && !view.state->fighters[other].defeated()) {
        anotherEnemyStands = true;
      }
    }
    for (std::size_t slot = 0; slot < EquippedSlots; ++slot) {
      const std::optional<MoveRef> equipped = self.moves.at(slot);
      if (!equipped) {
        continue;
      }
      if (!usable.at(slot)) {
        weights.at(slot) = 0;
        continue;
      }
      const BattleMove& move = view.moves->at(*equipped);
      if (anotherEnemyStands && !self.combatant.focusesPartner && move.range == MoveRange::Area) {
        weights.at(slot) += view.rules->enemyAi.areaWeight;
      }
      if (move.range == MoveRange::Self) {
        weights.at(slot) += self.buffPriority;
      }
    }
    const std::size_t picked = weightedPick(*view.random, weights);
    if (picked < EquippedSlots && self.moves.at(picked).has_value()) {
      return {.slot = picked};
    }
    return {.slot = std::nullopt};
  }
};

} // namespace

std::unique_ptr<EnemyAi> makeOriginalEnemyAi() {
  return std::make_unique<OriginalEnemyAi>();
}

} // namespace dwe::game::battle
