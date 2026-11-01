#pragma once
#include <dwe/game/story/prosperity.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>

// The player page's City tab (a dwe addition, docs/modulos/apps/screens.md): who already lives in
// the city and who is still missing, with the points each adds to the prosperity. The residents and
// their points come from the city's rules (main.c: recalculatePPandArena), never from a list of
// its own.
namespace dwe::app {

// The tab's lists, one position per resident, grouped by level (rookies, champions, ultimates)
// and by number inside each group, as "3,4,17": what the document reads.
struct CityTab {
  std::string species;
  std::string levels;
  std::string points;
  std::string recruited;
  std::int32_t residents = 0;
  std::int32_t total = 0;
};

[[nodiscard]] CityTab cityTab(std::span<const game::story::CityResident> residents);

// Publishes the tab (player.city.*); the UI passes on only what changed.
void publishCityTab(ui::Ui& ui, const CityTab& tab, std::optional<std::int32_t> prosperity);

} // namespace dwe::app
