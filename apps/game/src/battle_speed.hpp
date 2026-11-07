#pragma once

#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "user_settings.hpp"

namespace dwe::app {

// Quick battles, an engine improvement (docs/modulos/apps/game.md, «Opciones en partida»): a fight
// runs two logic ticks a frame, so it plays at twice the speed and ends in half the real time. Each
// tick is the original's, so what the fight does, what the partner gains and the game time it
// costs do not change; only the time the player waits does. normal is the original; key starts
// every fight at its speed and the battle_speed key switches x2 on and off; fast starts at x2 and
// the key switches it off. A UI setting and the pause menu's command with the same name.
inline constexpr std::string_view BattleSpeedSetting = "battle_speed";
inline constexpr std::string_view BattleSpeedNormal = "normal";
inline constexpr std::string_view BattleSpeedKey = "key";
inline constexpr std::string_view BattleSpeedFast = "fast";
// The input action that switches x2 during a fight.
inline constexpr std::string_view BattleSpeedAction = "battle_speed";
// Whether the battle document shows its x2 plate.
inline constexpr std::string_view BattleFastSetting = "battle.fast";
// How many logic ticks a fast fight runs for each of the clock's.
inline constexpr int FastBattleTicks = 2;

enum class BattleSpeed : std::uint8_t { Normal, Key, Fast };

[[nodiscard]] std::optional<BattleSpeed> parseBattleSpeed(std::string_view name);
[[nodiscard]] std::string_view battleSpeedName(BattleSpeed speed);

// What the fight on screen asks of the speed.
struct BattleSpeedMoment {
  // A fight runs (battle_main.c: the battle's ticks, from its start to its end box).
  bool fighting = false;
  // The player has to react in time: the finisher's charge, the fallen partner's countdown, the
  // bag or an item on its way. Those run at the original's speed, so x2 never costs a reaction.
  bool reacting = false;
};

// The chosen battle speed and the key's switch, saved with the other settings (settings.yaml).
class BattleSpeedChanger {
public:
  // settingsFile is where the choice is saved; nothing for runs that must not touch the user's
  // settings (autotests). The settings must outlive the changer.
  BattleSpeedChanger(UserSettings& settings, std::optional<std::filesystem::path> settingsFile);

  [[nodiscard]] BattleSpeed speed() const { return speed_; }
  // Takes the value of a battle_speed command and returns the speed now in use; a value that is
  // not a battle speed changes nothing. A change puts the key's switch back where the speed starts.
  BattleSpeed change(const ui::UiValue& value);
  // The battle_speed key during a fight: switches x2 on or off, until the next change of the
  // option. It does nothing at the original's speed.
  void press();
  // Whether x2 is switched on (the battle document's plate shows it during a fight).
  [[nodiscard]] bool fastSwitched() const { return fast_; }
  // How many logic ticks the frame runs for each of the clock's: FastBattleTicks while a fight runs
  // switched to x2 and the player has nothing to react to, 1 otherwise.
  [[nodiscard]] int ticksPerTick(const BattleSpeedMoment& moment) const;

private:
  UserSettings* settings_;
  std::optional<std::filesystem::path> settingsFile_;
  BattleSpeed speed_;
  bool fast_;
};

} // namespace dwe::app
