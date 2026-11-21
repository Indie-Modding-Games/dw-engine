#include <dwe/game/coop/protocol.hpp>
#include <dwe/session/session_sound.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "coop_battle_view.hpp"

namespace coop = dwe::game::coop;
using dwe::app::coopBattleSoundsOf;
using dwe::app::CoopThrowFollower;

namespace {

coop::BattleBegin fourPlayers() {
  return {.battle = 1,
          .starter = 2,
          .ask = 5,
          .mapId = "m",
          .enemyNpcs = {4, 7},
          .species = {"gabumon", "kunemon", "betamon", "agumon", "palmon"},
          .fixedFight = false,
          .allies = {coop::HostPlayerId, 3}};
}

coop::BattleThrow thrown(std::uint16_t serial, coop::PlayerId thrower, coop::BattleThrowStage stage,
                         std::uint8_t target = 0) {
  return {.serial = serial,
          .thrower = thrower,
          .item = "meat",
          .stage = stage,
          .x = 0,
          .z = 0,
          .height = 0,
          .target = target};
}

} // namespace

TEST_CASE("a guest plays every body of the host's battle from its own species' bank") {
  using dwe::session::FirstAllySoundSlot;
  const auto sounds = coopBattleSoundsOf(fourPlayers(), 3);
  CHECK(sounds.enemies == std::vector<std::string>{"kunemon", "betamon"});
  CHECK(sounds.partners == std::vector<std::string>{"gabumon", "agumon"});
  CHECK(sounds.slotOf(4) == 5);
  CHECK(sounds.slotOf(7) == 6);
  CHECK(sounds.slotOf(coop::PartnerFigureSlot) == FirstAllySoundSlot);
  CHECK(sounds.slotOf(coop::allyFigureSlot(coop::HostPlayerId)) == FirstAllySoundSlot + 1);
  CHECK(sounds.slotOf(coop::allyFigureSlot(3)) == 4);
  const auto starter = coopBattleSoundsOf(fourPlayers(), 2);
  CHECK(starter.partners == std::vector<std::string>{"agumon", "palmon"});
  CHECK(starter.slotOf(coop::PartnerFigureSlot) == 4);
  CHECK(starter.slotOf(coop::allyFigureSlot(3)) == FirstAllySoundSlot + 1);
  const auto helper = coopBattleSoundsOf(fourPlayers(), 1);
  CHECK(helper.partners.size() == 3);
  CHECK(helper.slotOf(coop::allyFigureSlot(3)) == FirstAllySoundSlot + 2);
}

TEST_CASE("a guest starts each throw's clip once and shows each landing's glow once") {
  CoopThrowFollower follower;
  auto step = follower.follow(std::vector{thrown(1, 0, coop::BattleThrowStage::Hand)});
  CHECK(step.started == std::vector<coop::PlayerId>{0});
  REQUIRE(step.shown);
  CHECK(step.shown->serial == 1);
  CHECK(step.landed.empty());
  step = follower.follow(std::vector{thrown(1, 0, coop::BattleThrowStage::Flight)});
  CHECK(step.started.empty());
  CHECK(step.shown);
  step = follower.follow(std::vector{thrown(1, 0, coop::BattleThrowStage::Flight)});
  CHECK(step.started.empty());
  step = follower.follow(std::vector{thrown(1, 0, coop::BattleThrowStage::Landed, 4),
                                     thrown(2, 2, coop::BattleThrowStage::Hand)});
  REQUIRE(step.landed.size() == 1);
  CHECK(step.landed.front().target == 4);
  CHECK(step.started == std::vector<coop::PlayerId>{2});
  REQUIRE(step.shown);
  CHECK(step.shown->serial == 2);
  step = follower.follow(std::vector{thrown(1, 0, coop::BattleThrowStage::Landed, 4),
                                     thrown(1, 0, coop::BattleThrowStage::Flight)});
  CHECK(step.landed.empty());
  CHECK_FALSE(step.shown);
  step = follower.follow(std::vector{thrown(3, 1, coop::BattleThrowStage::Landed, 0)});
  CHECK(step.started.empty());
  CHECK(step.landed.size() == 1);
  follower.reset();
  step = follower.follow(std::vector{thrown(1, 0, coop::BattleThrowStage::Hand)});
  CHECK(step.started.size() == 1);
}
