#include <dwe/game/battle/strategies.hpp>

#include <string>

#include "original_strategies.hpp"

namespace dwe::game::battle {

ruleset::StrategyRegistry<PartnerAi>& partnerAiStrategies() {
  static ruleset::StrategyRegistry<PartnerAi> registry = [] {
    ruleset::StrategyRegistry<PartnerAi> made{"partner battle AI"};
    made.add(std::string{OriginalStrategy}, &makeOriginalPartnerAi);
    return made;
  }();
  return registry;
}

ruleset::StrategyRegistry<EnemyAi>& enemyAiStrategies() {
  static ruleset::StrategyRegistry<EnemyAi> registry = [] {
    ruleset::StrategyRegistry<EnemyAi> made{"enemy battle AI"};
    made.add(std::string{OriginalStrategy}, &makeOriginalEnemyAi);
    return made;
  }();
  return registry;
}

ruleset::StrategyRegistry<DamageFormula>& damageStrategies() {
  static ruleset::StrategyRegistry<DamageFormula> registry = [] {
    ruleset::StrategyRegistry<DamageFormula> made{"battle damage formula"};
    made.add(std::string{OriginalStrategy}, &makeOriginalDamage);
    return made;
  }();
  return registry;
}

ruleset::StrategyRegistry<Obedience>& obedienceStrategies() {
  static ruleset::StrategyRegistry<Obedience> registry = [] {
    ruleset::StrategyRegistry<Obedience> made{"battle obedience"};
    made.add(std::string{OriginalStrategy}, &makeOriginalObedience);
    return made;
  }();
  return registry;
}

core::Result<void> checkStrategyNames(const ruleset::Section& section) {
  if (auto checked = partnerAiStrategies().check(section, PartnerAiPath); !checked) {
    return checked;
  }
  if (auto checked = enemyAiStrategies().check(section, EnemyAiPath); !checked) {
    return checked;
  }
  if (auto checked = damageStrategies().check(section, DamagePath); !checked) {
    return checked;
  }
  return obedienceStrategies().check(section, ObediencePath);
}

core::Result<BattleStrategies> createStrategies(const ruleset::Section& section) {
  auto partnerAi = partnerAiStrategies().create(section, PartnerAiPath);
  if (!partnerAi) {
    return std::unexpected{partnerAi.error()};
  }
  auto enemyAi = enemyAiStrategies().create(section, EnemyAiPath);
  if (!enemyAi) {
    return std::unexpected{enemyAi.error()};
  }
  auto damage = damageStrategies().create(section, DamagePath);
  if (!damage) {
    return std::unexpected{damage.error()};
  }
  auto obedience = obedienceStrategies().create(section, ObediencePath);
  if (!obedience) {
    return std::unexpected{obedience.error()};
  }
  return BattleStrategies{.partnerAi = std::move(*partnerAi),
                          .enemyAi = std::move(*enemyAi),
                          .damage = std::move(*damage),
                          .obedience = std::move(*obedience)};
}

BattleStrategies originalStrategies() {
  return {.partnerAi = makeOriginalPartnerAi(),
          .enemyAi = makeOriginalEnemyAi(),
          .damage = makeOriginalDamage(),
          .obedience = makeOriginalObedience()};
}

} // namespace dwe::game::battle
