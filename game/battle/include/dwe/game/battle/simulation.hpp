#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/battle/battle.hpp>

#include <cstdint>
#include <functional>

namespace dwe::game::battle {

// How a batch of battles ended.
struct WinRates {
    std::int32_t battles = 0;
    std::int32_t victories = 0;
    std::int32_t defeats = 0;
    std::int32_t flights = 0;
    // Battles cut at the frame limit.
    std::int32_t unfinished = 0;

    // The share of victories, from 0 to 1.
    [[nodiscard]] double victoryRate() const {
        return battles == 0 ? 0.0 : static_cast<double>(victories) / static_cast<double>(battles);
    }
};

// The longest battle the simulator plays by default: ten minutes of the original's frames.
inline constexpr std::int64_t DefaultFrameLimit = std::int64_t{FramesPerSecond} * 60 * 10;

// Builds the strategies of one battle: createStrategies over the ruleset's section, or originalStrategies.
using StrategyFactory = std::function<BattleStrategies()>;

// What a batch of battles plays.
struct SimulationPlan {
    std::int32_t count = 0;
    std::uint64_t firstSeed = 0;
    std::int64_t frameLimit = DefaultFrameLimit;
};

// Plays the battles of the plan from the same setup without commands, with the seeds firstSeed, firstSeed + 1...
// No graphics, for the tuning simulator and the balance tests.
core::Result<WinRates> runBattles(const BattleSetup& setup, const BattleRules& rules, const MoveCatalog& moves,
                                  const StrategyFactory& strategies, const SimulationPlan& plan);

} // namespace dwe::game::battle
