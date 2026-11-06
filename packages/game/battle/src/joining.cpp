#include <dwe/game/battle/joining.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;

bool carries(std::span<const std::string> inventory, const std::string& item) {
  return std::ranges::find(inventory, item) != inventory.end();
}

} // namespace

std::vector<bool> rollJoiningEnemies(const BattleRules& rules, const JoinContext& context,
                                     std::span<const DigimonType> candidates,
                                     ruleset::Random& random) {
  const JoiningRules& joining = rules.joining;
  std::vector<bool> joins(candidates.size(), false);
  std::int32_t count = 1;
  for (std::size_t index = 0; index < candidates.size() && count < rules.limits.maxEnemies;
       ++index) {
    std::int32_t roll = random.below(Hundred);
    if (carries(context.inventory, joining.bellItem)) {
      roll += joining.bellRoll;
    } else if (carries(context.inventory, joining.repelItem)) {
      roll += joining.repelRoll;
    }
    if (context.scripted && rules.quirks.on(quirk::ScriptedBattleEveryoneJoins)) {
      roll = 0;
    }
    const DigimonType enemy = candidates[index];
    bool join = false;
    if (context.partnerType != DigimonType::None && enemy != DigimonType::None) {
      const auto row = static_cast<std::size_t>(context.partnerType) - 1;
      const auto column = static_cast<std::size_t>(enemy) - 1;
      join = roll < joining.chances.at(row).at(column);
    } else {
      join = random.below(Hundred) < joining.untypedChance;
    }
    if (join) {
      joins.at(index) = true;
      ++count;
    }
  }
  return joins;
}

} // namespace dwe::game::battle
