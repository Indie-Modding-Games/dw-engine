#pragma once

#include <dwe/game/gym/gym_rules.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace dwe::game::gym {

// What a brains session's gain crossed (trn_reward.c: TRN_awardBrainsTrainingGains).
struct BrainMilestones {
  // The order thresholds crossed, lowest first: the partner takes new orders.
  std::vector<std::int32_t> orders;
  // No new orders, but a move threshold was crossed or the cap reached: it may learn a move.
  bool mayLearnMove = false;
};

[[nodiscard]] BrainMilestones brainMilestones(const BrainLearning& rules, std::int32_t before,
                                              std::int32_t gain);

// trn_reward.c: TRN_tryLearnMove: for each of the species' specials, the first move of its list
// that the partner may learn (`learnable`: in its species' list and not known yet) and that rank's
// weight; the heaviest wins if its weight beats `roll` (0 to 99). The original's move number.
[[nodiscard]] std::optional<std::int32_t>
trainingMove(const BrainLearning& rules, const std::array<std::optional<std::int32_t>, 3>& specials,
             const std::function<bool(std::int32_t)>& learnable, std::int32_t roll);

} // namespace dwe::game::gym
