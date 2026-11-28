#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "classic/classic.hpp"
#include "core/battle_context.hpp"
#include "core/battle_system.hpp"
#include "folder/folder.hpp"
#include "mechanics/decisions/usability.hpp"

namespace dwe::game::battle {

std::string_view battleSystemName(BattleSystemId system) {
  return system == BattleSystemId::Folder ? FolderBattleName : ClassicBattleName;
}

std::optional<BattleSystemId> battleSystemOf(std::string_view name) {
  if (name == ClassicBattleName) {
    return BattleSystemId::Classic;
  }
  if (name == FolderBattleName) {
    return BattleSystemId::Folder;
  }
  return std::nullopt;
}

std::int32_t BattleSystem::commandedMpCost(const BattleContext& context, FighterId id,
                                           MoveRef move) const {
  const bool unwell = context.state->isPlayerSide(id) &&
                      (context.conditionOf(id).sick || context.conditionOf(id).injured);
  return partnerMpCost(*context.rules, context.moves->at(move).mpCost,
                       context.fighter(id).stats.brain, unwell, MpCostUse::Payment);
}

std::vector<FighterId> BattleSystem::reached(const BattleContext& /*context*/, FighterId /*id*/,
                                             MoveRef /*move*/,
                                             std::vector<FighterId> shared) const {
  return shared;
}

bool BattleSystem::touches(const BattleContext& context, FighterId attacker, FighterId other,
                           MoveRef /*move*/) const {
  return context.state->foes(attacker, other);
}

std::unique_ptr<BattleSystem> makeBattleSystem(BattleSystemId id) {
  switch (id) {
  case BattleSystemId::Classic:
    return makeClassicBattle();
  case BattleSystemId::Folder:
    return makeFolderBattle();
  }
  return makeClassicBattle();
}

} // namespace dwe::game::battle
