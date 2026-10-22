#include <dwe/game/evolution/simulation.hpp>

namespace dwe::game::evolution {

EvolutionHistogram simulateRaisings(const Evolution& evolution, const RaisingGenerator& generator,
                                    std::int32_t raisings, std::uint64_t seed) {
  ruleset::Random random{seed};
  EvolutionHistogram histogram;
  for (std::int32_t raising = 0; raising < raisings; ++raising) {
    const EvolutionInputs inputs = generator(random);
    const NaturalCheck check = evolution.checkNatural(inputs, EvolutionState{}, 0, random);
    ++histogram.raisings;
    if (check.evolution) {
      ++histogram.targets[check.evolution->target];
    } else {
      ++histogram.unchanged;
    }
  }
  return histogram;
}

} // namespace dwe::game::evolution
