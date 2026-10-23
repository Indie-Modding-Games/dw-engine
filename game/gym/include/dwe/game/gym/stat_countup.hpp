#pragma once

#include <dwe/game/gym/gym_rules.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace dwe::game::gym {

// The six stats in the original's order: HP, MP, offense, defense, speed and brains.
inline constexpr std::size_t GymStatCount = 6;
using GymStats = std::array<std::int32_t, GymStatCount>;

// The stats box after a session (trn_hud.c: TRN_createPostTrainingStatsBox and its tick and
// render): it shows the stats from before the session and counts the gains into them, one point a
// frame after a delay, the first stat with a gain left first. Confirm or cancel adds the rest at
// once. Only the view counts: the session has already given the gains.
class StatCountup {
public:
  StatCountup(const CountRules& rules, const GymStats& before, const GymStats& gains);

  // One frame of the original. Whether a point was counted, which plays the count sound.
  bool tickFrame();
  // The player pressed confirm or cancel: every gain left is added.
  void finish();

  // TRN_statGainsAreZero.
  [[nodiscard]] bool done() const;
  [[nodiscard]] const GymStats& shown() const { return shown_; }
  [[nodiscard]] const GymStats& left() const { return left_; }
  // Which stats the session raised or lowered, for their bars' colour.
  [[nodiscard]] const std::array<bool, GymStatCount>& changed() const { return changed_; }

private:
  [[nodiscard]] std::int32_t capOf(std::size_t stat) const;

  CountRules rules_;
  GymStats shown_;
  GymStats left_;
  std::array<bool, GymStatCount> changed_{};
  std::int32_t delay_ = 0;
};

} // namespace dwe::game::gym
