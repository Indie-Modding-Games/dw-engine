#include <dwe/game/inventory/item_effects.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

#include "messages.hpp"

namespace dwe::game::inventory {

namespace {

using assets::ItemHandler;

// The handler names, for messages (content/schemas/item_effects.schema.json).
constexpr std::array<std::string_view, 10> HandlerNames{
    "none",   "hp_healing",  "mp_healing", "double_floppy", "status",
    "revive", "battle_disk", "chip",       "food",          "evolution"};

std::string_view nameOf(ItemHandler handler) {
  return HandlerNames.at(static_cast<std::size_t>(handler));
}

template <typename Rule>
const Rule* ruleFor(const std::vector<Rule>& rules, std::string_view item) {
  const auto found = std::ranges::find(rules, item, &Rule::item);
  return found == rules.end() ? nullptr : &*found;
}

core::Result<void> missingRule(std::string_view item, ItemHandler handler) {
  return core::fail(messages::formatMessage(messages::HandlerRuleMissing, item, nameOf(handler)));
}

bool hasStatGain(const StatGain& gain) {
  return gain.maxHp != 0 || gain.maxMp != 0 || gain.offense != 0 || gain.defense != 0 ||
         gain.speed != 0 || gain.brain != 0;
}

} // namespace

bool ItemEffect::doesSomething() const {
  return healing.hp != 0 || healing.mp != 0 || battleStatusCure.has_value() || protects ||
         medicine.has_value() || revive.has_value() || battleBoost.has_value() ||
         statGain.has_value() || care.has_value() || tamerLevel.has_value() ||
         scriptSection.has_value() || relievesToilet || evolution.has_value();
}

ItemEffects::ItemEffects(assets::ItemEffectTable table, EffectRules rules, InventoryQuirks quirks)
    : table_(std::move(table)), rules_(std::move(rules)), quirks_(quirks) {}

core::Result<ItemEffect> ItemEffects::resolve(std::string_view item) const {
  const assets::ItemEffectEntry* entry = table_.find(item);
  if (entry == nullptr) {
    return core::fail(messages::formatMessage(messages::NoEffect, item));
  }
  ItemEffect effect{.item = std::string{item},
                    .handler = entry->handler,
                    .consciousOnly = std::ranges::contains(rules_.consciousOnly, entry->handler)};
  auto described = describe(*entry, effect);
  if (!described) {
    return std::unexpected{described.error()};
  }
  return effect;
}

core::Result<void> ItemEffects::describe(const assets::ItemEffectEntry& entry,
                                         ItemEffect& effect) const {
  switch (entry.handler) {
  case ItemHandler::None:
    return {};
  case ItemHandler::HpHealing:
  case ItemHandler::MpHealing:
    if (!entry.healing) {
      return core::fail(messages::formatMessage(messages::HealingUnmapped, entry.item));
    }
    (entry.handler == ItemHandler::HpHealing ? effect.healing.hp : effect.healing.mp) =
        *entry.healing;
    return {};
  case ItemHandler::DoubleFloppy:
    effect.healing = {.hp = rules_.doubleFloppyHp, .mp = rules_.doubleFloppyMp};
    return {};
  case ItemHandler::Status:
    return describeStatus(entry.item, effect);
  case ItemHandler::Revive:
    return describeRevive(entry.item, effect);
  case ItemHandler::BattleDisk:
    return describeDisk(entry.item, effect);
  case ItemHandler::Chip:
    return describeChip(entry, effect);
  case ItemHandler::Food:
    return describeFood(entry, effect);
  case ItemHandler::Evolution:
    return describeEvolution(entry, effect);
  }
  return {};
}

core::Result<void> ItemEffects::describeStatus(std::string_view item, ItemEffect& effect) const {
  const StatusItemRule* rule = ruleFor(rules_.status, item);
  if (rule == nullptr) {
    return missingRule(item, ItemHandler::Status);
  }
  effect.battleStatusCure = rule->battleStatusCure;
  effect.healing = {.hp = rule->hp, .mp = rule->mp};
  effect.protects = rule->protects;
  if (rule->injuryCure != 0 || rule->sicknessCure != 0) {
    effect.medicine = Medicine{.injuryRoll = rules_.injuryRoll,
                               .injuryCure = rule->injuryCure,
                               .sicknessRoll = rules_.sicknessRoll,
                               .sicknessCure = rule->sicknessCure};
  }
  return {};
}

core::Result<void> ItemEffects::describeRevive(std::string_view item, ItemEffect& effect) const {
  const ReviveItemRule* rule = ruleFor(rules_.revive, item);
  if (rule == nullptr) {
    return missingRule(item, ItemHandler::Revive);
  }
  effect.revive = Revive{.hpPercent = rule->hpPercent};
  effect.battleStatusCure = rule->battleStatusCure;
  return {};
}

core::Result<void> ItemEffects::describeDisk(std::string_view item, ItemEffect& effect) const {
  const BattleDiskRule* rule = ruleFor(rules_.disks, item);
  if (rule == nullptr) {
    return missingRule(item, ItemHandler::BattleDisk);
  }
  effect.battleBoost = BattleBoost{.offense = rule->offense,
                                   .defense = rule->defense,
                                   .speed = rule->speed,
                                   .limitPercent = rules_.diskLimitPercent,
                                   .limitCap = rules_.diskLimitCap,
                                   .ignoresLimit = rule->omni && quirks_.omniDiskIgnoresLimit};
  return {};
}

core::Result<void> ItemEffects::describeChip(const assets::ItemEffectEntry& entry,
                                             ItemEffect& effect) const {
  if (const ScriptItemRule* script = ruleFor(rules_.scriptChips, entry.item)) {
    effect.scriptSection = script->section;
    return {};
  }
  if (std::ranges::contains(rules_.toiletChips, entry.item)) {
    effect.relievesToilet = true;
    return {};
  }
  if (!entry.chip) {
    return core::fail(
        messages::formatMessage(messages::EffectUnmapped, entry.item, nameOf(entry.handler)));
  }
  const assets::ChipValues& chip = *entry.chip;
  const StatGain gain{.maxHp = chip.maxHp,
                      .maxMp = chip.maxMp,
                      .offense = chip.offense,
                      .defense = chip.defense,
                      .speed = chip.speed,
                      .brain = chip.brain,
                      .statLimit = rules_.statLimit,
                      .hpMpLimit = rules_.hpMpLimit};
  if (hasStatGain(gain)) {
    effect.statGain = gain;
  }
  if (chip.lifetime != 0) {
    effect.care = CareChange{.lifetimeHours = chip.lifetime, .lifetimeStopsAtZero = true};
  }
  if (std::ranges::contains(rules_.tamerLevel.items, entry.item)) {
    effect.tamerLevel = TamerLevelChange{.chancePercent = rules_.tamerLevel.chancePercent,
                                         .amount = rules_.tamerLevel.amount};
  }
  return {};
}

core::Result<void> ItemEffects::describeFood(const assets::ItemEffectEntry& entry,
                                             ItemEffect& effect) const {
  if (!entry.food) {
    return core::fail(
        messages::formatMessage(messages::EffectUnmapped, entry.item, nameOf(entry.handler)));
  }
  const assets::FoodValues& food = *entry.food;
  effect.healing = {.hp = food.hp, .mp = food.mp};
  const StatGain gain{.maxHp = food.maxHp,
                      .maxMp = food.maxMp,
                      .offense = food.offense,
                      .defense = food.defense,
                      .speed = food.speed,
                      .brain = food.brain,
                      .statLimit = rules_.statLimit,
                      .hpMpLimit = rules_.hpMpLimit};
  if (hasStatGain(gain)) {
    effect.statGain = gain;
  }
  effect.care = CareChange{.energy = food.energy,
                           .tirednessRelief = food.tirednessRelief,
                           .happiness = food.happiness,
                           .discipline = food.discipline,
                           .weight = food.weight,
                           .lifetimeHours = food.lifetime,
                           .lifetimeStopsAtZero = false,
                           .poopLevelChange = rules_.poopLevelChange,
                           .sicknessChance = food.sicknessChance,
                           .trainingBoost = {.flag = food.trainingBoostFlag,
                                             .value = food.trainingBoostValue,
                                             .hours = food.trainingBoostHours},
                           .favouriteEnergyPercent = rules_.favouriteEnergyPercent,
                           .favouriteHappiness = rules_.favouriteHappiness};
  return {};
}

core::Result<void> ItemEffects::describeEvolution(const assets::ItemEffectEntry& entry,
                                                  ItemEffect& effect) const {
  if (const FixedEvolutionRule* fixed = ruleFor(rules_.fixedEvolutions, entry.item)) {
    effect.evolution = Evolution{.digimon = fixed->evolvesTo, .requiresPreviousLevel = false};
    return {};
  }
  if (!entry.evolvesTo) {
    return core::fail(messages::formatMessage(messages::EvolutionUnmapped, entry.item));
  }
  effect.evolution = Evolution{.digimon = *entry.evolvesTo,
                               .requiresPreviousLevel = rules_.evolutionRequiresPreviousLevel};
  return {};
}

} // namespace dwe::game::inventory
