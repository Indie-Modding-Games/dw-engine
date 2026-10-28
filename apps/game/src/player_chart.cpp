#include "player_chart.hpp"

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>

#include <format>

namespace dwe::app {
void publishPlayerChart(ui::Ui& ui, const assets::EvolutionChart& chart,
                        const assets::DigimonTable* species) {
  for (std::size_t i = 0; i < chart.entries.size(); ++i) {
    const auto& cell = chart.entries[i];
    const auto base = std::format("player.chart.{}.", i);
    for (const auto& [key, value] : {std::pair{"x", cell.x},
                                     {"y", cell.y},
                                     {"u", cell.u},
                                     {"v", cell.v},
                                     {"palette", cell.palette},
                                     {"index", cell.index}})
      ui.setSetting(base + key, static_cast<double>(value));
    ui.setSetting(base + "source", std::format("{}?palette={}", chart.image, cell.palette));
    const auto* row = species ? species->find(cell.digimon) : nullptr;
    ui.setSetting(base + "name", row ? row->name : cell.digimon);
  }
  ui.setSetting("player.chart.count", static_cast<double>(chart.entries.size()));
}
void loadPlayerChart(ui::Ui& ui, const assets::VirtualFileSystem& files) {
  const auto id = assets::AssetId::parse("data/evolution_chart");
  if (files.resolve(*id) == nullptr)
    return;
  auto chart = assets::loadEvolutionChart(files, *id);
  if (!chart) {
    core::logWarning("Chart metadata: {}", chart.error().message);
    return;
  }
  const auto speciesId = assets::AssetId::parse("data/digimon");
  auto species = assets::loadDigimonTable(files, *speciesId);
  publishPlayerChart(ui, *chart, species ? &*species : nullptr);
}
} // namespace dwe::app
