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
using dwe::app::CoopThrowPurse;

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

TEST_CASE("a guest pays for its thrown item only once the host's frames show the throw") {
  CoopThrowPurse purse;
  const coop::PlayerId self = 2;
  REQUIRE(purse.canAsk());
  purse.asked(10);
  CHECK_FALSE(purse.canAsk());
  // Frames without its throw cost nothing; another player's throw is theirs.
  CHECK(purse.due({}, self, 11).empty());
  const std::vector<coop::BattleThrow> other{thrown(7, 3, coop::BattleThrowStage::Hand)};
  CHECK(purse.due(other, self, 12).empty());
  CHECK_FALSE(purse.canAsk());
  // The host threw it: paid once, however many frames show it.
  const std::vector<coop::BattleThrow> mine{thrown(8, self, coop::BattleThrowStage::Hand)};
  CHECK(purse.due(mine, self, 13) == std::vector<std::string>{"meat"});
  CHECK(purse.canAsk());
  const std::vector<coop::BattleThrow> landed{thrown(8, self, coop::BattleThrowStage::Landed)};
  CHECK(purse.due(landed, self, 14).empty());

  // A throw the host refused never shows: the item stays in the bag and the next one may be asked.
  purse.asked(20);
  CHECK(purse.due({}, self, 20 + CoopThrowPurse::WaitFrames).empty());
  CHECK_FALSE(purse.canAsk());
  CHECK(purse.due({}, self, 21 + CoopThrowPurse::WaitFrames).empty());
  CHECK(purse.canAsk());
  // A new battle pays its own throws again, whatever their serials.
  purse.reset();
  CHECK(purse.due(mine, self, 1) == std::vector<std::string>{"meat"});
}

TEST_CASE("a guest's battle HUD shows its own partner first and the other partners as allies") {
  const coop::BattleBegin begin = fourPlayers();
  const std::vector<coop::BattleFighterState> fighters{
      {.hp = 100, .mp = 10, .maxHp = 200, .maxMp = 20},
      {.hp = 300, .maxHp = 300},
      {.hp = 0, .maxHp = 250, .down = true},
      {.hp = 150, .mp = 30, .maxHp = 150, .maxMp = 40},
      {.hp = 0, .mp = 5, .maxHp = 180, .maxMp = 50, .down = true, .reviveSeconds = 7}};
  coop::BattleFrame frame;
  frame.phase = coop::BattlePhase::Fight;
  frame.command = 2;
  frame.menu = (1U << 1U) | (1U << 2U) | (1U << 8U);
  frame.bagAvailable = true;
  const dwe::screens::NameKeys names{nullptr, nullptr, nullptr};
  CHECK(dwe::app::coopOwnFighterOf(begin, coop::BattleRole::Ally, 3) == 4);
  CHECK(dwe::app::coopOwnFighterOf(begin, coop::BattleRole::Starter, 2) == 0);
  CHECK(dwe::app::coopOwnFighterOf(begin, coop::BattleRole::Helper, 5) == 0);

  const dwe::screens::BattleView ally = dwe::app::coopBattleViewOf({.begin = &begin,
                                                                    .frame = &frame,
                                                                    .fighters = fighters,
                                                                    .role = coop::BattleRole::Ally,
                                                                    .self = 3},
                                                                   names);
  REQUIRE(ally.fighters.size() == 3);
  CHECK(ally.fighters[0].id == 4);
  CHECK(ally.fighters[0].fainted);
  CHECK(ally.fighters[0].reviveSeconds == 7);
  CHECK(ally.reviveSeconds == 7);
  CHECK(ally.fighters[1].id == 1);
  CHECK(ally.fighters[2].fainted);
  REQUIRE(ally.allies.size() == 2);
  CHECK(ally.allies[0].id == 0);
  CHECK(ally.allies[1].id == 3);
  CHECK(ally.allies[1].mp == 30);
  CHECK(ally.bagAvailable);
  REQUIRE(ally.menu.size() == 3);
  CHECK(ally.menu[1].command == dwe::game::battle::Command::Attack);
  CHECK(ally.menu[1].current);
  CHECK(ally.menu[1].icon == "command-2");
  CHECK(ally.menu[2].labelKey == "battle.command.technique_1");
  CHECK(ally.menu[2].icon.empty());

  const dwe::screens::BattleView helper =
      dwe::app::coopBattleViewOf({.begin = &begin,
                                  .frame = &frame,
                                  .fighters = fighters,
                                  .role = coop::BattleRole::Helper,
                                  .self = 5},
                                 names);
  CHECK(helper.fighters[0].id == 0);
  CHECK(helper.allies.size() == 2);
  CHECK(helper.menu.empty());
}

TEST_CASE("a guest's battle HUD letters the foes of one species as the host's does") {
  coop::BattleBegin begin = fourPlayers();
  begin.species[2] = begin.species[1];
  const dwe::screens::NameKeys names{nullptr, nullptr, nullptr};
  dwe::app::CoopBattleViewSource source;
  source.begin = &begin;
  source.role = coop::BattleRole::Starter;
  source.self = 2;
  const dwe::screens::BattleView view = dwe::app::coopBattleViewOf(source, names);
  REQUIRE(view.fighters.size() == 3);
  CHECK(view.fighters[0].tag.empty());
  CHECK(view.fighters[1].tag == "A");
  CHECK(view.fighters[2].tag == "B");
}
