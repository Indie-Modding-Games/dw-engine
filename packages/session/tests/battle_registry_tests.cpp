#include <dwe/session/battle_registry.hpp>
#include <dwe/session/game_session.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "session_fixture.hpp"

using dwe::session::BattleRegistry;
using dwe::session::GameSession;
using dwe::session::RegisteredPartner;
using dwe::session::SessionSystem;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

std::unique_ptr<GameSession> sessionWithRegistry(const TestPacks& packs,
                                                 BattleRegistry*& registry) {
  auto adapter = std::make_unique<BattleRegistry>();
  registry = adapter.get();
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(adapter));
  return newGameWith(packs, std::move(systems));
}

RegisteredPartner partner() {
  return {.name = "Ace: \"one\"",
          .species = "agumon",
          .hp = 800,
          .mp = 600,
          .offense = 80,
          .defense = 60,
          .speed = 70,
          .brains = 70,
          .discipline = 50,
          .techniques = {std::string{"spit_fire"}, std::nullopt, std::nullopt}};
}

} // namespace

TEST_CASE("a partner registered at the save machine goes in the save and comes back") {
  const TestPacks packs;
  BattleRegistry* saving = nullptr;
  auto saved = sessionWithRegistry(packs, saving);
  CHECK_FALSE(saving->at(4).has_value());
  REQUIRE(saving->registerAt(4, partner()).has_value());
  auto file = saved->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  REQUIRE(file->sections.contains("battle_registry"));

  BattleRegistry* loading = nullptr;
  auto loaded = sessionWithRegistry(packs, loading);
  const auto read = loaded->loadFile(*file);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  REQUIRE(loading->at(4).has_value());
  CHECK(*loading->at(4) == partner());
  CHECK_FALSE(loading->at(3).has_value());

  REQUIRE(loaded->startNewGame().has_value());
  CHECK_FALSE(loading->at(4).has_value());
}

TEST_CASE("the registry has the original's 40 slots, the machine reaching the first 39") {
  const TestPacks packs;
  BattleRegistry* registry = nullptr;
  auto session = sessionWithRegistry(packs, registry);
  CHECK(BattleRegistry::Slots == 40);
  CHECK(BattleRegistry::MachineSlots == 39);
  CHECK(registry->registerAt(BattleRegistry::Slots - 1, partner()).has_value());
  CHECK_FALSE(registry->registerAt(BattleRegistry::Slots, partner()).has_value());
  CHECK_FALSE(registry->at(BattleRegistry::Slots).has_value());
}

TEST_CASE("the registry counts the slots that hold a partner, as the versus mode asks") {
  // main_menu.c: countRegisteredDigimon: the records with a species, over all 40.
  const TestPacks packs;
  BattleRegistry* registry = nullptr;
  auto session = sessionWithRegistry(packs, registry);
  CHECK(registry->registeredCount() == 0);
  REQUIRE(registry->registerAt(0, partner()).has_value());
  REQUIRE(registry->registerAt(BattleRegistry::Slots - 1, partner()).has_value());
  REQUIRE(registry->registerAt(0, partner()).has_value());
  CHECK(registry->registeredCount() == 2);
  registry->clear(0);
  CHECK(registry->registeredCount() == 1);
}
