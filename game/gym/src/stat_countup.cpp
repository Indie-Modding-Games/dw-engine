#include <dwe/core/log.hpp>
#include <dwe/game/gym/growing_box.hpp>
#include <dwe/game/gym/stat_countup.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace dwe::game::gym {
namespace {

// trn_hud.c: HP and MP are the first two stats.
constexpr std::size_t HpMpStats = 2;

} // namespace

StatCountup::StatCountup(const CountRules& rules, const GymStats& before, const GymStats& gains)
    // The frame that creates the box draws its first outline; four more and it stands.
    : rules_(rules), shown_(before), left_(gains), delay_(rules.delayFrames),
      growing_(GrowingBox::Frames - 1) {
  for (std::size_t stat = 0; stat < GymStatCount; ++stat) {
    changed_.at(stat) = gains.at(stat) != 0;
  }
  core::logDebug(
      "[port:gym] TRN_createPostTrainingStatsBox (trn_hud.c:162): gains {} {} {} {} {} {}",
      gains[0], gains[1], gains[2], gains[3], gains[4], gains[5]);
}

std::int32_t StatCountup::capOf(std::size_t stat) const {
  return stat < HpMpStats ? rules_.hpMpMax : rules_.statMax;
}

bool StatCountup::tickFrame() {
  // trn_hud.c: TRN_tickPostTrainingStatsBox counts the delay down; the render of the same frame
  // then moves one point of the first stat with a positive gain left once it is 0. A loss waits
  // for confirm.
  if (growing_ > 0) {
    --growing_;
    return false;
  }
  if (delay_ > 0) {
    --delay_;
  }
  if (delay_ > 0) {
    return false;
  }
  for (std::size_t stat = 0; stat < GymStatCount; ++stat) {
    if (left_.at(stat) <= 0) {
      continue;
    }
    --left_.at(stat);
    shown_.at(stat) = std::min(shown_.at(stat) + 1, capOf(stat));
    return true;
  }
  return false;
}

void StatCountup::finish() {
  if (!standing()) {
    return;
  }
  // trn_hud.c: TRN_tickPostTrainingStatsBox on confirm or cancel; only a gain is capped.
  for (std::size_t stat = 0; stat < GymStatCount; ++stat) {
    if (left_.at(stat) == 0) {
      continue;
    }
    shown_.at(stat) += left_.at(stat);
    if (left_.at(stat) > 0) {
      shown_.at(stat) = std::min(shown_.at(stat), capOf(stat));
    }
    left_.at(stat) = 0;
  }
  delay_ = 0;
  core::logDebug("[port:gym] TRN_tickPostTrainingStatsBox (trn_hud.c:202): count finished");
}

bool StatCountup::done() const {
  return std::ranges::all_of(left_, [](std::int32_t gain) { return gain == 0; });
}

} // namespace dwe::game::gym
