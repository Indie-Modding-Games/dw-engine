#pragma once

#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/inputs.hpp>
#include <dwe/ruleset/random.hpp>

#include <cstdint>
#include <functional>
#include <map>

namespace dwe::game::evolution {

// Makes the inputs of one raising at its evolution check. It may roll the random numbers it is given.
using RaisingGenerator = std::function<EvolutionInputs(ruleset::Random& random)>;

// How many raisings ended in each form.
struct EvolutionHistogram {
    std::int32_t raisings = 0;
    // Raisings that did not evolve.
    std::int32_t unchanged = 0;
    // Target form -> raisings that evolved into it.
    std::map<Species, std::int32_t> targets;
};

// Runs the periodic check on `raisings` raisings, each with the inputs `generator` makes and an empty evolution state,
// and counts the outcomes: the balance simulations' "1000 raisings". The same seed gives the same histogram.
EvolutionHistogram simulateRaisings(const Evolution& evolution, const RaisingGenerator& generator,
                                    std::int32_t raisings, std::uint64_t seed);

} // namespace dwe::game::evolution
