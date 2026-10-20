#include <dwe/game/battle/simulation.hpp>

#include <optional>

namespace dwe::game::battle {

core::Result<WinRates> runBattles(const BattleSetup& setup, const BattleRules& rules, const MoveCatalog& moves,
                                  const StrategyFactory& strategies, const SimulationPlan& plan) {
    WinRates rates;
    for (std::int32_t index = 0; index < plan.count; ++index) {
        auto battle =
            Battle::start(setup, rules, moves, strategies(), plan.firstSeed + static_cast<std::uint64_t>(index));
        if (!battle) {
            return std::unexpected{battle.error()};
        }
        while (!battle->finished() && battle->state().frame < plan.frameLimit) {
            battle->step();
        }
        ++rates.battles;
        const std::optional<BattleResult>& result = battle->result();
        if (!result) {
            ++rates.unfinished;
            continue;
        }
        switch (result->outcome) {
        case Outcome::Victory:
            ++rates.victories;
            break;
        case Outcome::Defeat:
            ++rates.defeats;
            break;
        case Outcome::Fled:
            ++rates.flights;
            break;
        }
    }
    return rates;
}

} // namespace dwe::game::battle
