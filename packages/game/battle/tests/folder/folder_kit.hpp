#pragma once

#include <dwe/game/battle/battle.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "../fixture.hpp"

// What the Folder battle's tests share: its rules, a Folder duel and the events of a run.
namespace folder_test {

using namespace dwe::game::battle;
using namespace battle_test;

inline const FolderRules& folderRules() {
  REQUIRE(originalRules().folder.has_value());
  return *originalRules().folder;
}

inline BattleSetup folderDuel(CombatStats partner, CombatStats enemy,
                              std::vector<std::string> partnerMoves = {"punch", "shot"},
                              std::vector<std::string> enemyMoves = {"punch"}) {
  BattleSetup setup = duel(partner, enemy, std::move(partnerMoves), std::move(enemyMoves));
  setup.system = BattleSystemId::Folder;
  return setup;
}

inline BattleEvents runFrames(Battle& battle, std::int64_t frames) {
  BattleEvents all;
  for (std::int64_t frame = 0; frame < frames && !battle.finished(); ++frame) {
    const BattleEvents& step = battle.step();
    all.insert(all.end(), step.begin(), step.end());
  }
  return all;
}

inline std::vector<BattleEvent> eventsOf(const BattleEvents& events, EventKind kind,
                                         FighterId fighter) {
  std::vector<BattleEvent> found;
  std::ranges::copy_if(events, std::back_inserter(found), [&](const BattleEvent& event) {
    return event.kind == kind && event.fighter == fighter;
  });
  return found;
}

inline std::vector<OrderPhase> phasesOf(const BattleEvents& events, FighterId fighter) {
  std::vector<OrderPhase> phases;
  for (const BattleEvent& event : eventsOf(events, EventKind::OrderPhaseChanged, fighter)) {
    phases.push_back(static_cast<OrderPhase>(event.amount));
  }
  return phases;
}

} // namespace folder_test
