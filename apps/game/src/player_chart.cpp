#include "player_chart.hpp"

#include <dwe/assets/evolution_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>

#include <format>

namespace dwe::app {
void publishChartDetailLayout(ui::Ui& ui, const assets::ChartDetailLayout& detail) {
  const auto publishMatrix = [&](std::string_view name, const auto& matrix) {
    for (std::size_t i = 0; i < matrix.size(); ++i)
      for (std::size_t j = 0; j < matrix[i].size(); ++j)
        ui.setSetting(std::format("player.chart.detail.{}.{}.{}", name, i, j),
                      static_cast<double>(matrix[i][j]));
  };
  publishMatrix("lines", detail.lines);
  publishMatrix("colors", detail.colors);
  publishMatrix("sprites", detail.sprites);
  publishMatrix("insets", detail.insets);
  ui.setSetting("player.chart.detail.layout_ready", true);
}

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
    ui.setSetting(base + "level", static_cast<double>(row ? row->level : 0));
  }
  ui.setSetting("player.chart.count", static_cast<double>(chart.entries.size()));
}
void loadPlayerChartDetails(ui::Ui& ui, const assets::VirtualFileSystem& files,
                            const assets::EvolutionChart& chart) {
  const auto evolutionId = assets::AssetId::parse("data/evolution");
  auto evolution = assets::loadEvolutionTable(files, *evolutionId);
  if (evolution) {
    for (std::size_t i = 0; i < chart.entries.size(); ++i) {
      for (const auto& path : evolution->paths) {
        if (!path.digimon || *path.digimon != chart.entries[i].digimon)
          continue;
        const auto publishPaths = [&](std::string_view direction, const auto& refs) {
          for (std::size_t j = 0; j < refs.size(); ++j) {
            int target = refs[j] ? -2 : -1;
            if (refs[j]) {
              for (std::size_t k = 0; k < chart.entries.size(); ++k)
                if (chart.entries[k].digimon == *refs[j])
                  target = static_cast<int>(k);
            }
            ui.setSetting(std::format("player.chart.{}.{}.{}", i, direction, j),
                          static_cast<double>(target));
          }
        };
        publishPaths("from", path.from);
        publishPaths("to", path.to);
      }
    }
    ui.setSetting("player.chart.detail.paths_ready", true);
    core::logInfo("Chart detail: published {} evolution paths", evolution->paths.size());
  }
  const auto detailId = assets::AssetId::parse("data/evolution_chart_detail");
  if (files.resolve(*detailId) != nullptr) {
    auto detail = assets::loadChartDetailLayout(files, *detailId);
    if (detail)
      publishChartDetailLayout(ui, *detail);
    else
      core::logWarning("Chart detail metadata: {}", detail.error().message);
  }
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
  loadPlayerChartDetails(ui, files, *chart);
}
} // namespace dwe::app
