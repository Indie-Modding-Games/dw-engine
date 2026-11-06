#include <dwe/game/inventory/item_effects.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string_view>

#include "fixture.hpp"

using namespace dwe::game::inventory;
using namespace dwe::game::inventory::testing;
using dwe::assets::ItemHandler;

namespace {

ItemEffect resolved(const ItemEffects& effects, std::string_view item) {
  auto effect = effects.resolve(item);
  REQUIRE_MESSAGE(effect.has_value(), errorOf(effect));
  return *effect;
}

} // namespace

TEST_CASE("healing items heal what data/item_effects says, and only a conscious partner") {
  const ItemEffects effects = fixtureItemEffects(originalRules());
  const ItemEffect tonic = resolved(effects, Tonic);
  CHECK(tonic.handler == ItemHandler::HpHealing);
  CHECK(tonic.healing.hp == 500);
  CHECK(tonic.consciousOnly);
  CHECK(resolved(effects, Ether).healing.mp == 400);
  // item.c: handleDoubleFloppy: 1500 HP and MP.
  const ItemEffect floppy = resolved(effects, Floppy);
  CHECK(floppy.healing.hp == 1500);
  CHECK(floppy.healing.mp == 1500);
}

TEST_CASE("status items, revives and disks take their logic from the ruleset") {
  const ItemEffects effects = fixtureItemEffects(originalRules());
  // item.c: handleStatusItems case 0xd: handleMedicineHealing(3, 2).
  const ItemEffect bandage = resolved(effects, Bandage);
  const auto medicine = required(bandage.medicine);
  CHECK(medicine.injuryRoll == 3);
  CHECK(medicine.injuryCure == 3);
  CHECK(medicine.sicknessRoll == 10);
  CHECK(medicine.sicknessCure == 2);
  // item.c: handleRestore case 0xc: full HP and the battle status cured.
  const ItemEffect revival = resolved(effects, Revival);
  const auto revive = required(revival.revive);
  CHECK(revive.hpPercent == 100);
  CHECK(revival.battleStatusCure == 0);
  CHECK_FALSE(revival.consciousOnly);
  // main.c: handleBuffDisks: +20 up to the stat at the start of the battle + 30 %, at most 999.
  const ItemEffect disk = resolved(effects, PowerDisk);
  const auto boost = required(disk.battleBoost);
  CHECK(boost.offense == 20);
  CHECK(boost.limitPercent == 130);
  CHECK(boost.limitCap == 999);
  CHECK_FALSE(boost.ignoresLimit);
  CHECK(required(resolved(effects, OmniDisk).battleBoost).ignoresLimit);
}

TEST_CASE("the omni disk keeps the limit when its quirk is off") {
  InventoryRules rules = originalRules();
  rules.quirks.omniDiskIgnoresLimit = false;
  const ItemEffects effects = fixtureItemEffects(rules);
  CHECK_FALSE(required(resolved(effects, OmniDisk).battleBoost).ignoresLimit);
}

TEST_CASE("chips give their decoded gains; the special chips do what the ruleset says") {
  const ItemEffects effects = fixtureItemEffects(originalRules());
  const ItemEffect brain = resolved(effects, BrainChip);
  const auto gain = required(brain.statGain);
  CHECK(gain.brain == 50);
  CHECK(gain.statLimit == 999);
  CHECK(gain.hpMpLimit == 9999);
  CHECK_FALSE(brain.care.has_value());
  // 0x1c < chipId < 0x20: a day of life and a 10 % chance of a tamer level less.
  const ItemEffect twice = resolved(effects, DoubleChip);
  const auto care = required(twice.care);
  CHECK(care.lifetimeHours == -24);
  CHECK(care.lifetimeStopsAtZero);
  const auto tamerLevel = required(twice.tamerLevel);
  CHECK(tamerLevel.chancePercent == 10);
  CHECK(tamerLevel.amount == -1);
  CHECK(resolved(effects, ScriptChip).scriptSection == 1245);
  CHECK(resolved(effects, Toilet).relievesToilet);
  CHECK_FALSE(resolved(effects, Charm).doesSomething());
}

TEST_CASE(
    "food gives its decoded care changes, the favourite bonus and one less on the poop level") {
  const ItemEffects effects = fixtureItemEffects(originalRules());
  const ItemEffect steak = resolved(effects, Steak);
  const auto care = required(steak.care);
  CHECK(care.energy == 20);
  CHECK(care.happiness == 3);
  CHECK(care.weight == 2);
  CHECK(care.sicknessChance == 5);
  CHECK(care.trainingBoost.hours == 4);
  CHECK(care.favouriteEnergyPercent == 140);
  CHECK(care.favouriteHappiness == 2);
  CHECK(care.poopLevelChange == -1);
  CHECK_FALSE(care.lifetimeStopsAtZero);
  CHECK_FALSE(steak.consciousOnly);
}

TEST_CASE(
    "evolution items lead to the table's target one level below, or to the ruleset's fixed one") {
  const ItemEffects effects = fixtureItemEffects(originalRules());
  const ItemEffect crest = resolved(effects, Crest);
  const auto crestEvolution = required(crest.evolution);
  CHECK(crestEvolution.digimon == "drake");
  CHECK(crestEvolution.requiresPreviousLevel);
  const ItemEffect big = resolved(effects, BigCrest);
  const auto bigEvolution = required(big.evolution);
  CHECK(bigEvolution.digimon == "ggdr");
  CHECK_FALSE(bigEvolution.requiresPreviousLevel);
}

TEST_CASE("an item missing from data/item_effects, or a case nobody decoded, is reported") {
  const ItemEffects effects = fixtureItemEffects(originalRules());
  CHECK_FALSE(effects.resolve("nothing").has_value());
  const auto hidden = effects.resolve(Hidden);
  REQUIRE_FALSE(hidden.has_value());
  CHECK(hidden.error().message.find("food") != std::string::npos);
}

TEST_CASE("the larger items show the healing particles with the ring, medicine only on a cure") {
  // item.c: HEAL_EFFECT_VARIANT {0, 0, 1, 1}; handleRestore passes 1; handleStatusItems cases 0xd
  // and 0xe only when handleMedicineHealing returns 1.
  const ItemEffects effects = fixtureItemEffects(originalRules());
  const auto revival = required(resolved(effects, Revival).healingParticles);
  CHECK(revival.ring);
  CHECK_FALSE(revival.onlyIfSicknessCured);
  const auto bandage = required(resolved(effects, Bandage).healingParticles);
  CHECK_FALSE(bandage.ring);
  CHECK(bandage.onlyIfSicknessCured);
  CHECK_FALSE(resolved(effects, PowerDisk).healingParticles.has_value());
}
