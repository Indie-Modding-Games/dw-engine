#include <dwe/core/log.hpp>
#include <dwe/game/story/prosperity.hpp>

#include <algorithm>
#include <string>

#include "messages.hpp"

namespace dwe::game::story {

core::Result<std::vector<CityResident>>
cityResidents(const StoryState& state, const ProsperityRules& rules, const SpeciesLevel& level) {
  std::vector<CityResident> residents;
  for (std::int32_t species = rules.firstSpecies; species < rules.endSpecies; ++species) {
    const std::int32_t speciesLevel = level ? level(species) : 0;
    if (speciesLevel < rules.minLevel) {
      continue;
    }
    auto recruited = state.trigger(rules.recruitTrigger + species);
    if (!recruited) {
      return std::unexpected{recruited.error()};
    }
    const bool onePoint = std::ranges::contains(rules.onePointSpecies, species);
    residents.push_back({.species = species,
                         .level = speciesLevel,
                         .points = onePoint ? 1 : speciesLevel - rules.levelOffset,
                         .recruited = *recruited});
  }
  return residents;
}

core::Result<std::uint8_t> recalculateProsperity(StoryState& state, const ProsperityRules& rules,
                                                 const SpeciesLevel& level) {
  auto residents = cityResidents(state, rules, level);
  if (!residents) {
    return std::unexpected{residents.error()};
  }
  std::uint8_t points = 0;
  for (const CityResident& resident : *residents) {
    if (resident.recruited) {
      points = static_cast<std::uint8_t>(points + resident.points);
    }
  }
  if (auto written = state.setVariable(rules.pointsVariable, points); !written) {
    return std::unexpected{written.error()};
  }
  auto cup = state.variable(rules.cupVariable);
  if (!cup) {
    return std::unexpected{cup.error()};
  }
  if (*cup >= rules.noCupFrom) {
    for (const std::int32_t trigger : rules.registrationTriggers) {
      if (auto cleared = state.setTrigger(trigger, false); !cleared) {
        return std::unexpected{cleared.error()};
      }
    }
  }
  core::logDebug("[port:city] recalculatePPandArena (main.c:3902): {} prosperity points, cup {}",
                 points, *cup);
  return points;
}

core::Result<std::int32_t> clearedCityMap(const StoryState& state,
                                          const std::vector<CityMapChoice>& choices) {
  for (const CityMapChoice& choice : choices) {
    bool all = true;
    for (const std::int32_t trigger : choice.triggers) {
      auto set = state.trigger(trigger);
      if (!set) {
        return std::unexpected{set.error()};
      }
      if (!*set) {
        all = false;
        break;
      }
    }
    if (all) {
      return choice.map;
    }
  }
  return core::fail(std::string{messages::NoClearedCityMap});
}

} // namespace dwe::game::story
