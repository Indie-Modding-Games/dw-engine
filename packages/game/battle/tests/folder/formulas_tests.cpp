#include <dwe/game/battle/folder/formulas.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "folder_kit.hpp"

using namespace folder_test;

TEST_CASE("the folder formulas follow the ruleset's numbers") {
  const FolderRules& rules = folderRules();
  CHECK(folder::movementPercent(rules, 0) == 75);
  CHECK(folder::movementPercent(rules, 500) == 100);
  CHECK(folder::movementPercent(rules, 1000) == 125);
  CHECK(folder::movementPercent(rules, 2000) == 125);
  CHECK(folder::executionFrames(rules, 10, 0) == 10);
  CHECK(folder::executionFrames(rules, 10, 1000) == 6);
  CHECK(folder::processingFrames(rules, 16, 1000) == 8);
  CHECK(folder::processingFrames(rules, 16, 500) == 12);

  const folder::Specials fire{0, std::nullopt, std::nullopt};
  const folder::Specials earth{3, std::nullopt, std::nullopt};
  const folder::Specials air{2, std::nullopt, std::nullopt};
  const folder::Specials none{};
  // Fire beats earth, air beats fire; fire on a fire user adds STAB.
  CHECK(folder::elementPercent(rules, 0, none, earth) == 20);
  CHECK(folder::elementPercent(rules, 0, none, air) == -20);
  CHECK(folder::elementPercent(rules, 0, fire, earth) == 40);
  CHECK(folder::elementPercent(rules, 0, none, none) == 0);

  CHECK(folder::typePercent(rules, DigimonType::Vaccine, DigimonType::Virus) == 25);
  CHECK(folder::typePercent(rules, DigimonType::Virus, DigimonType::Vaccine) == -25);
  CHECK(folder::typePercent(rules, DigimonType::None, DigimonType::Data) == 0);

  CHECK(folder::damage(rules, {.power = 66, .attack = 100, .modifierPercent = 20, .defense = 50}) ==
        66 + 2 * 100 + 13 - 50);
  CHECK(folder::damage(rules, {.power = 10, .attack = 0, .modifierPercent = 0, .defense = 500}) ==
        1);
  CHECK(folder::damage(
            rules,
            {.power = 10, .attack = 0, .modifierPercent = 0, .defense = 500, .covered = true}) ==
        0);
  CHECK(folder::damage(
            rules,
            {.power = 100, .attack = 100, .modifierPercent = 0, .defense = 50, .covered = true}) ==
        100 + 2 * 100 - 2 * 50);

  CHECK(folder::coverPercent(rules, 1000) == 30);
  CHECK(folder::coverPercent(rules, 500) == 15);
  CHECK(folder::barCapacity(rules, 100) == 600);
  CHECK(folder::barCapacity(rules, 0) == 1);
  CHECK(folder::statCutPercent(rules, 1000, 1000) == 30);
  CHECK(folder::statCutPercent(rules, 0, 0) == 0);
  CHECK(folder::breakFrames(rules, 0, 0) == 80);
  CHECK(folder::breakFrames(rules, 1000, 1000) == 56);
  CHECK(folder::barDrain(rules, 600, 0, 0) == 5);
  CHECK(folder::brokenStat(rules, 300) == 150);
}

TEST_CASE("a technique's timing comes from its own entry, the finisher's or its MP tier") {
  FolderRules rules = folderRules();
  BattleMove move;
  move.id = "cheap";
  move.mpCost = 10;
  CHECK(folder::timingOf(rules, move) ==
        TechniqueTiming{.preparationFrames = 6, .recoveryFrames = 14});
  move.mpCost = 40;
  CHECK(folder::timingOf(rules, move) ==
        TechniqueTiming{.preparationFrames = 10, .recoveryFrames = 20});
  move.mpCost = 140;
  CHECK(folder::timingOf(rules, move) ==
        TechniqueTiming{.preparationFrames = 18, .recoveryFrames = 32});
  move.finisher = true;
  CHECK(folder::timingOf(rules, move) == rules.finisherTiming);
  rules.techniques.emplace("cheap", TechniqueTiming{.preparationFrames = 3, .recoveryFrames = 4});
  CHECK(folder::timingOf(rules, move) ==
        TechniqueTiming{.preparationFrames = 3, .recoveryFrames = 4});
  rules.timingTiers.clear();
  move.id = "other";
  move.finisher = false;
  move.mpCost = 40;
  CHECK(folder::timingOf(rules, move) ==
        TechniqueTiming{.preparationFrames = rules.preparationFrames,
                        .recoveryFrames = rules.recoveryFrames});
}
