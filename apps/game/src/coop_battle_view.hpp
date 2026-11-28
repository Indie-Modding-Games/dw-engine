#pragma once

#include <dwe/game/coop/protocol.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace dwe::app {

struct CoopBattleSounds {
  std::vector<std::string> enemies;
  std::vector<std::string> partners;
  std::map<std::int16_t, std::int64_t> slots;

  [[nodiscard]] std::int64_t slotOf(std::int16_t figureSlot) const;
};
[[nodiscard]] CoopBattleSounds coopBattleSoundsOf(const game::coop::BattleBegin& begin,
                                                  game::coop::PlayerId self);

class CoopThrowFollower {
public:
  struct Step {
    std::vector<game::coop::PlayerId> started;
    std::vector<game::coop::BattleThrow> landed;
    std::optional<game::coop::BattleThrow> shown;
  };

  [[nodiscard]] Step follow(std::span<const game::coop::BattleThrow> throws);
  void reset();

private:
  std::set<std::uint16_t> started_;
  std::set<std::uint16_t> landed_;
};

// The items this player throws in the host's battle are paid from its own bag once the host's
// frames show the throw (docs/architecture/coop-combat.md §7.1): one the host refused costs
// nothing. One throw at a time waits for the host's answer, at most WaitFrames.
class CoopThrowPurse {
public:
  static constexpr std::uint32_t WaitFrames = 40;

  [[nodiscard]] bool canAsk() const { return !asked_; }
  void asked(std::uint32_t frame) { asked_ = frame; }
  // The items `self`'s throws in this frame that were not paid yet cost.
  [[nodiscard]] std::vector<std::string> due(std::span<const game::coop::BattleThrow> throws,
                                             game::coop::PlayerId self, std::uint32_t frame);
  void reset();

private:
  std::optional<std::uint32_t> asked_;
  std::set<std::uint16_t> paid_;
};

} // namespace dwe::app
