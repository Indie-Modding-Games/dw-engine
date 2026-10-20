#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/species.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/sim/scenario.hpp>

#include <cstdint>
#include <memory>

namespace dwe::sim {

// Game sessions for a scenario's runs, without a window, a field or saves: the whole game as the session plays it
// (partner care, autosave requests, later evolution, battle and inventory), on the scenario's ruleset with its --set
// values. Prepared once in setup(); start() reads only what was prepared, so runs on several threads may call it.
class HeadlessSessions {
public:
    // Reads the species table of the context's ruleset, unless `species` gives one (tests without the disc).
    static core::Result<HeadlessSessions> prepare(const ScenarioContext& context,
                                                  std::shared_ptr<const game::partner::SpeciesTable> species = nullptr);

    // A new game with this seed, its clock at the context's speed.
    [[nodiscard]] core::Result<std::unique_ptr<session::GameSession>> start(std::uint64_t seed) const;

private:
    HeadlessSessions(const ScenarioContext& context, std::shared_ptr<const game::partner::SpeciesTable> species)
        : context_(context), species_(std::move(species)) {}

    ScenarioContext context_;
    std::shared_ptr<const game::partner::SpeciesTable> species_;
};

} // namespace dwe::sim
