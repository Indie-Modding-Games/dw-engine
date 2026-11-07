#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "fixture.hpp"

using namespace battle_test;

namespace {

BattleSetup versusMatch(CombatStats first, CombatStats second, std::vector<std::string> firstMoves,
                        std::vector<std::string> secondMoves, std::int32_t seconds = 99) {
  BattleSetup setup = duel(first, second, std::move(firstMoves), std::move(secondMoves));
  setup.kind = BattleKind::Versus;
  setup.scripted = true;
  setup.timeLimit = seconds;
  setup.versus = VersusSetup{.rivalDiscipline = 40};
  return setup;
}

BattleSetup plainVersus(std::int32_t seconds = 99) {
  return versusMatch(evenStats(900, 250), evenStats(900, 250), {"punch", "shot"}, {"punch", "shot"},
                     seconds);
}

// Runs both battles frame by frame, feeding each the same commands, and returns whether every
// frame's checksum agreed.
bool playInStep(Battle& one, Battle& two,
                const std::vector<std::pair<std::int64_t, Command>>& rivalOrders) {
  for (std::int64_t frame = 0; frame < 2400 && !one.finished(); ++frame) {
    for (const auto& [when, order] : rivalOrders) {
      if (when == frame) {
        (void)one.command(order, RivalId);
        (void)two.command(order, RivalId);
      }
    }
    one.step();
    two.step();
    if (one.checksum() != two.checksum()) {
      return false;
    }
  }
  return one.finished() == two.finished();
}

} // namespace

TEST_CASE("vs_combat.c VS__combatInit: the versus menus by brain, without flee or change") {
  // Measured: brain 300 gives [finisher, distance, moderate, attack, your call].
  Battle low =
      startBattle(versusMatch(evenStats(900, 300), evenStats(900, 50), {"punch"}, {"punch"}), 1);
  CHECK(low.menuOf(PartnerId) == std::vector{Command::Finisher, Command::Distance,
                                             Command::Moderate, Command::Attack,
                                             Command::YourCall});
  CHECK(low.menuOf(RivalId) == std::vector{Command::Finisher, Command::YourCall});
  // Measured: brain 600 without a third technique gives [finisher, defend, distance, technique 2,
  // technique 1, your call].
  BattleSetup smart = versusMatch(evenStats(900, 600), evenStats(900, 600), {"punch", "shot"},
                                  {"punch", "shot", "", "big_finish"});
  Battle high = startBattle(smart, 1);
  CHECK(high.menuOf(RivalId) == std::vector{Command::Finisher, Command::Defend, Command::Distance,
                                            Command::Technique2, Command::Technique1,
                                            Command::YourCall});
}

TEST_CASE("vs_combat.c VS___tickVSInput: each side takes its own commands at once") {
  Battle battle = startBattle(plainVersus(), 3);
  REQUIRE(battle.command(Command::Attack, RivalId));
  CHECK(battle.state().rival->commands.delay == 0);
  CHECK(battle.state().rival->commands.buffered == Command::Attack);
  CHECK(battle.state().commands.buffered == Command::YourCall);
  REQUIRE(battle.command(Command::Moderate, PartnerId));
  CHECK(battle.state().commands.delay == 0);
  battle.step();
  CHECK(battle.state().rival->commands.current == Command::Attack);
  CHECK(battle.state().commands.current == Command::Moderate);
  CHECK_FALSE(battle.command(Command::Flee, RivalId));
  CHECK_FALSE(battle.command(Command::Finisher, RivalId));
}

TEST_CASE("a battle that is not a versus match has no commanded rival") {
  Battle battle = startBattle(duel(evenStats(900, 120), evenStats(900, 120)), 1);
  CHECK_FALSE(battle.command(Command::YourCall, RivalId));
  CHECK_FALSE(battle.state().versus());
}

TEST_CASE("a versus match needs one rival, a time limit and the versus kind") {
  BattleSetup crowd = plainVersus();
  crowd.enemies.push_back(crowd.enemies.front());
  CHECK_FALSE(Battle::start(crowd, originalRules(), testCatalog(), originalStrategies(), 1));
  BattleSetup endless = plainVersus();
  endless.timeLimit.reset();
  CHECK_FALSE(Battle::start(endless, originalRules(), testCatalog(), originalStrategies(), 1));
  BattleSetup kindless = plainVersus();
  kindless.kind = BattleKind::Tournament;
  CHECK_FALSE(Battle::start(kindless, originalRules(), testCatalog(), originalStrategies(), 1));
}

TEST_CASE("vs_combat.c VS__tickDigimonAI: the rival decides as a partner, at the other side") {
  Battle battle = startBattle(plainVersus(), 5);
  for (int frame = 0; frame < 200; ++frame) {
    battle.step();
  }
  CHECK(battle.state().fighters.at(RivalId).target == PartnerId);
  CHECK(battle.state().partner().target == RivalId);
}

TEST_CASE("vs_combat.c VS__deinitializeCombat: a versus match has no rewards or consequences") {
  Battle battle =
      startBattle(versusMatch(evenStats(900, 300), evenStats(60, 20), {"punch"}, {"punch"}), 7);
  runToEnd(battle);
  const BattleResult& result = resultOf(battle);
  CHECK(result.kind == BattleKind::Versus);
  CHECK(result.outcome == Outcome::Victory);
  CHECK(result.bits == 0);
  CHECK(result.drops.empty());
  CHECK(result.statGains == StatValues{});
  CHECK(result.care.battles == 0);
  CHECK(result.livesLost == 0);
}

TEST_CASE("two versus battles with the same seed and the same commands stay alike frame by frame") {
  Battle one = startBattle(plainVersus(), 11);
  Battle two = startBattle(plainVersus(), 11);
  CHECK(one.checksum() == two.checksum());
  const std::vector<std::pair<std::int64_t, Command>> orders = {
      {10, Command::Attack}, {300, Command::Moderate}, {700, Command::YourCall}};
  CHECK(playInStep(one, two, orders));
}

TEST_CASE("the checksum tells two versus battles apart once their commands differ") {
  Battle one = startBattle(plainVersus(), 11);
  Battle two = startBattle(plainVersus(), 11);
  REQUIRE(one.command(Command::Attack, RivalId));
  REQUIRE(two.command(Command::Moderate, RivalId));
  one.step();
  two.step();
  CHECK(one.checksum() != two.checksum());
}

TEST_CASE("vs_scene.c VS__BATTLE_COUNTS and VS__combatInit: the original rules' versus") {
  const BattleRules& rules = originalRules();
  CHECK(rules.versusMenus.size() == 6);
  REQUIRE(rules.versus);
  CHECK(rules.versus->series == std::vector<std::int32_t>{1, 3, 5});
  CHECK(rules.versus->timeLimitSeconds == 99);
}
