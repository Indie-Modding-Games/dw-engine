#include <dwe/core/log.hpp>
#include <dwe/game/gym/brain_learning.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace dwe::game::gym {
namespace {

bool crosses(std::int32_t threshold, std::int32_t before, std::int32_t total) {
  return before < threshold && total >= threshold;
}

} // namespace

BrainMilestones brainMilestones(const BrainLearning& rules, std::int32_t before,
                                std::int32_t gain) {
  BrainMilestones milestones;
  const std::int32_t total = before + gain;
  for (const std::int32_t threshold : rules.orderThresholds) {
    if (crosses(threshold, before, total)) {
      milestones.orders.push_back(threshold);
    }
  }
  if (milestones.orders.empty()) {
    bool crossed = total == rules.moveCap;
    for (const std::int32_t threshold : rules.moveThresholds) {
      crossed = crossed || crosses(threshold, before, total);
    }
    milestones.mayLearnMove = crossed;
  }
  core::logDebug("[port:gym] TRN_awardBrainsTrainingGains (trn_reward.c:474): brains {} + {}, "
                 "{} new orders, move {}",
                 before, gain, milestones.orders.size(), milestones.mayLearnMove);
  return milestones;
}

std::optional<std::int32_t> trainingMove(const BrainLearning& rules,
                                         const std::array<std::optional<std::int32_t>, 3>& specials,
                                         const std::function<bool(std::int32_t)>& learnable,
                                         std::int32_t roll) {
  std::optional<std::int32_t> best;
  std::int32_t bestWeight = -1;
  for (std::size_t slot = 0; slot < specials.size(); ++slot) {
    const std::optional<std::int32_t>& special = specials.at(slot);
    if (!special || *special < 0 || std::cmp_greater_equal(*special, rules.movesBySpecial.size())) {
      continue;
    }
    const auto& moves = rules.movesBySpecial.at(static_cast<std::size_t>(*special));
    for (std::size_t rank = 0; rank < moves.size(); ++rank) {
      if (!learnable(moves.at(rank))) {
        continue;
      }
      // Ties go to the earlier special, as the original compares with <.
      const std::int32_t weight = rules.weights.at(rank).at(slot);
      if (bestWeight < weight) {
        best = moves.at(rank);
        bestWeight = weight;
      }
      break;
    }
  }
  const bool learned = best.has_value() && bestWeight > roll;
  core::logDebug("[port:gym] TRN_tryLearnMove (trn_reward.c:811): move {}, weight {}, roll {}, "
                 "learned {}",
                 best.value_or(-1), bestWeight, roll, learned);
  return learned ? best : std::nullopt;
}

} // namespace dwe::game::gym
