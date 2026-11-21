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

} // namespace dwe::app
