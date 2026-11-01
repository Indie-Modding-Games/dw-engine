#include "player_city.hpp"

#include <algorithm>
#include <vector>

namespace dwe::app {
namespace {

void append(std::string& list, std::int32_t value) {
  if (!list.empty()) {
    list += ',';
  }
  list += std::to_string(value);
}

} // namespace

CityTab cityTab(std::span<const game::story::CityResident> residents) {
  std::vector<game::story::CityResident> ordered{residents.begin(), residents.end()};
  std::ranges::stable_sort(ordered, {}, &game::story::CityResident::level);
  CityTab tab;
  for (const game::story::CityResident& resident : ordered) {
    append(tab.species, resident.species);
    append(tab.levels, resident.level);
    append(tab.points, resident.points);
    append(tab.recruited, resident.recruited ? 1 : 0);
    tab.residents += resident.recruited ? 1 : 0;
  }
  tab.total = static_cast<std::int32_t>(ordered.size());
  return tab;
}

void publishCityTab(ui::Ui& ui, const CityTab& tab, std::optional<std::int32_t> prosperity) {
  ui.setSetting("player.city.species", tab.species);
  ui.setSetting("player.city.levels", tab.levels);
  ui.setSetting("player.city.points", tab.points);
  ui.setSetting("player.city.recruited", tab.recruited);
  ui.setSetting("player.city.residents", static_cast<double>(tab.residents));
  ui.setSetting("player.city.total", static_cast<double>(tab.total));
  ui.setSetting("player.city.prosperity", static_cast<double>(prosperity.value_or(0)));
}

} // namespace dwe::app
