#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/story_state.hpp>

#include <cstdint>
#include <functional>
#include <vector>

// The city's prosperity and the File City screen a finished game continues on. The scripts raise
// the prosperity variable when a Digimon joins the city and set its recruit trigger; the original
// rebuilds the variable from those triggers when the game loop starts (after a new game or a load)
// and before every save (main.c: recalculatePPandArena), so what lasts are the triggers. The city's
// scenery and shops follow the triggers and the variable through the scripts.
namespace dwe::game::story {

struct ProsperityRules {
  // The variable that holds the points (pstat 1).
  std::int32_t pointsVariable = 0;
  // A species counts when trigger recruitTrigger + its number is set, for the numbers from
  // firstSpecies up to endSpecies (not included), and its level is at least minLevel.
  std::int32_t recruitTrigger = 0;
  std::int32_t firstSpecies = 0;
  std::int32_t endSpecies = 0;
  std::int32_t minLevel = 0;
  // Each counted species adds its level minus levelOffset; these add one point whatever their
  // level.
  std::int32_t levelOffset = 0;
  std::vector<std::int32_t> onePointSpecies;
  // The registered cup's variable: from noCupFrom up it holds no cup, and the registration
  // triggers are cleared.
  std::int32_t cupVariable = 0;
  std::int32_t noCupFrom = 0;
  std::vector<std::int32_t> registrationTriggers;
};

// A species' level by its number (1 baby to 5 ultimate), 0 for a number the table does not have.
using SpeciesLevel = std::function<std::int32_t(std::int32_t species)>;

// Rebuilds the points from the recruit triggers and clears the registration of a cup that is not
// there (main.c: recalculatePPandArena). Returns the points; a trigger or variable outside the
// story is an error. The sum wraps as the original's byte does.
core::Result<std::uint8_t> recalculateProsperity(StoryState& state, const ProsperityRules& rules,
                                                 const SpeciesLevel& level);

// One species that can live in the city: those recalculatePPandArena counts, with the points it
// adds and whether its recruit trigger is set.
struct CityResident {
  std::int32_t species = 0;
  std::int32_t level = 0;
  std::int32_t points = 0;
  bool recruited = false;
};

// Every species that can join the city, in number order, as recalculatePPandArena walks them
// (main.c:3902); a trigger outside the story is an error. Its sum over the recruited ones is the
// prosperity the next rebuild writes.
core::Result<std::vector<CityResident>>
cityResidents(const StoryState& state, const ProsperityRules& rules, const SpeciesLevel& level);

// One File City screen: the first whose triggers are all set is the one.
struct CityMapChoice {
  std::vector<std::int32_t> triggers;
  std::int32_t map = 0;
};

// The File City screen a game saved after the ending continues on, by how the city grew
// (map.c: getFileCityTopMap); nothing when no choice matches.
core::Result<std::int32_t> clearedCityMap(const StoryState& state,
                                          const std::vector<CityMapChoice>& choices);

} // namespace dwe::game::story
