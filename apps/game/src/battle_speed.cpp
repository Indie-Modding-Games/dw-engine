#include "battle_speed.hpp"

#include <dwe/core/log.hpp>

#include <utility>
#include <variant>

#include "messages.hpp"

namespace dwe::app {

namespace {

BattleSpeed startBattleSpeed(const std::optional<std::string>& saved) {
  if (!saved) {
    return BattleSpeed::Normal;
  }
  const auto speed = parseBattleSpeed(*saved);
  if (!speed) {
    core::logWarning(messages::UnknownSavedBattleSpeed, *saved);
  }
  return speed.value_or(BattleSpeed::Normal);
}

} // namespace

std::optional<BattleSpeed> parseBattleSpeed(std::string_view name) {
  if (name == BattleSpeedNormal) {
    return BattleSpeed::Normal;
  }
  if (name == BattleSpeedKey) {
    return BattleSpeed::Key;
  }
  if (name == BattleSpeedFast) {
    return BattleSpeed::Fast;
  }
  return std::nullopt;
}

std::string_view battleSpeedName(BattleSpeed speed) {
  switch (speed) {
  case BattleSpeed::Key:
    return BattleSpeedKey;
  case BattleSpeed::Fast:
    return BattleSpeedFast;
  case BattleSpeed::Normal:
    break;
  }
  return BattleSpeedNormal;
}

BattleSpeedChanger::BattleSpeedChanger(UserSettings& settings,
                                       std::optional<std::filesystem::path> settingsFile)
    : settings_{&settings}, settingsFile_{std::move(settingsFile)},
      speed_{startBattleSpeed(settings.battleSpeed)}, fast_{speed_ == BattleSpeed::Fast} {}

BattleSpeed BattleSpeedChanger::change(const ui::UiValue& value) {
  const std::string* name = std::get_if<std::string>(&value);
  const auto speed = name == nullptr ? std::nullopt : parseBattleSpeed(*name);
  if (!speed) {
    return speed_;
  }
  speed_ = *speed;
  fast_ = speed_ == BattleSpeed::Fast;
  settings_->battleSpeed = std::string{battleSpeedName(speed_)};
  if (settingsFile_) {
    saveUserSettings(*settingsFile_, *settings_);
  }
  return speed_;
}

void BattleSpeedChanger::press() {
  if (speed_ != BattleSpeed::Normal) {
    fast_ = !fast_;
  }
}

int BattleSpeedChanger::ticksPerTick(const BattleSpeedMoment& moment) const {
  return speed_ != BattleSpeed::Normal && fast_ && moment.fighting && !moment.reacting
             ? FastBattleTicks
             : 1;
}

} // namespace dwe::app
