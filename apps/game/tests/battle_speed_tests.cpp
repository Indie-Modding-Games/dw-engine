// Quick battles: two logic ticks a frame while a fight runs at x2 (docs/modulos/apps/game.md,
// «Opciones en partida»).

#include <doctest/doctest.h>

#include <optional>
#include <string>

#include "battle_speed.hpp"
#include "user_settings.hpp"

namespace dwe::app {
namespace {

constexpr BattleSpeedMoment Fighting{.fighting = true, .reacting = false};
constexpr BattleSpeedMoment Reacting{.fighting = true, .reacting = true};
constexpr BattleSpeedMoment Field{.fighting = false, .reacting = false};

} // namespace

TEST_CASE("battle speed names read back and an unknown one is not a speed") {
  for (const BattleSpeed speed : {BattleSpeed::Normal, BattleSpeed::Key, BattleSpeed::Fast}) {
    CHECK(parseBattleSpeed(battleSpeedName(speed)) == speed);
  }
  CHECK_FALSE(parseBattleSpeed("turbo").has_value());
}

TEST_CASE("at the original's speed a fight runs one tick a frame and the key does nothing") {
  UserSettings settings;
  BattleSpeedChanger speed{settings, std::nullopt};
  CHECK(speed.speed() == BattleSpeed::Normal);
  CHECK(speed.ticksPerTick(Fighting) == 1);
  speed.press();
  CHECK_FALSE(speed.fastSwitched());
  CHECK(speed.ticksPerTick(Fighting) == 1);
}

TEST_CASE("fast runs a fight at two ticks a frame, never the field, and the key switches it off") {
  UserSettings settings;
  settings.battleSpeed = "fast";
  BattleSpeedChanger speed{settings, std::nullopt};
  CHECK(speed.ticksPerTick(Fighting) == FastBattleTicks);
  CHECK(speed.ticksPerTick(Field) == 1);
  speed.press();
  CHECK(speed.ticksPerTick(Fighting) == 1);
  speed.press();
  CHECK(speed.ticksPerTick(Fighting) == FastBattleTicks);
}

TEST_CASE("with the key a fight starts at its speed and the key switches x2 on") {
  UserSettings settings;
  BattleSpeedChanger speed{settings, std::nullopt};
  speed.change(std::string{BattleSpeedKey});
  CHECK(settings.battleSpeed == std::optional<std::string>{"key"});
  CHECK(speed.ticksPerTick(Fighting) == 1);
  speed.press();
  CHECK(speed.fastSwitched());
  CHECK(speed.ticksPerTick(Fighting) == FastBattleTicks);
}

TEST_CASE("a fight waits at the original's speed while the player has to react") {
  UserSettings settings;
  settings.battleSpeed = "fast";
  const BattleSpeedChanger speed{settings, std::nullopt};
  CHECK(speed.ticksPerTick(Reacting) == 1);
}

TEST_CASE("a value that is not a battle speed changes nothing, and a saved unknown one is normal") {
  UserSettings settings;
  settings.battleSpeed = "turbo";
  BattleSpeedChanger speed{settings, std::nullopt};
  CHECK(speed.speed() == BattleSpeed::Normal);
  speed.change(std::string{"turbo"});
  speed.change(true);
  CHECK(speed.speed() == BattleSpeed::Normal);
  CHECK(settings.battleSpeed == std::optional<std::string>{"turbo"});
}

} // namespace dwe::app
