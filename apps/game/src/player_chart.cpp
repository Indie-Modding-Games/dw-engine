#include "player_chart.hpp"

#include <dwe/assets/card_table.hpp>
#include <dwe/assets/evolution_table.hpp>
#include <dwe/assets/medal_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/ruleset/species_extra.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string>

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
    ui.setSetting(base + "id", cell.digimon);
    ui.setSetting(base + "name", row ? row->name : cell.digimon);
    ui.setSetting(base + "level", static_cast<double>(row ? row->level : 0));
  }
  ui.setSetting("player.chart.count", static_cast<double>(chart.entries.size()));
}
void publishChartPaths(ui::Ui& ui, const assets::EvolutionChart& chart,
                       const assets::EvolutionTable& evolution) {
  for (std::size_t i = 0; i < chart.entries.size(); ++i) {
    // A cell with no path row (a new species with none) has no arrows.
    for (std::size_t j = 0; j < 6; ++j) {
      ui.setSetting(std::format("player.chart.{}.from.{}", i, j), -1.0);
      ui.setSetting(std::format("player.chart.{}.to.{}", i, j), -1.0);
    }
    for (const auto& path : evolution.paths) {
      if (!path.digimon || *path.digimon != chart.entries[i].digimon) {
        continue;
      }
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
  core::logInfo("Chart detail: published {} evolution paths", evolution.paths.size());
}

void loadPlayerChartDetails(ui::Ui& ui, const assets::VirtualFileSystem& files,
                            const assets::EvolutionChart& chart) {
  const auto evolutionId = assets::AssetId::parse("data/evolution");
  if (auto evolution = assets::loadEvolutionTable(files, *evolutionId)) {
    publishChartPaths(ui, chart, *evolution);
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

assets::EvolutionChart chartWithExtraCells(const assets::EvolutionChart& chart,
                                           const ruleset::SpeciesExtra& extra,
                                           const assets::EvolutionTable& evolution,
                                           const assets::DigimonTable* species) {
  assets::EvolutionChart result = chart;
  for (const auto& [id, patch] : extra.species) {
    if (!patch.chart) {
      continue;
    }
    const bool onChart = std::ranges::any_of(
        result.entries, [&](const assets::ChartCell& cell) { return cell.digimon == id; });
    // Only a form some path leads to: a patch that adds none leaves the chart alone.
    const bool reached = std::ranges::any_of(evolution.paths, [&](const auto& path) {
      return std::ranges::find(path.to, std::optional<std::string>{id}) != path.to.end();
    });
    const auto icon =
        std::ranges::find(chart.entries, patch.chart->icon, &assets::ChartCell::digimon);
    if (onChart || !reached || icon == chart.entries.end()) {
      continue;
    }
    const auto* row = species != nullptr ? species->find(id) : nullptr;
    result.entries.push_back({.digimon = id,
                              .index = row != nullptr ? row->index : 0,
                              .x = patch.chart->x,
                              .y = patch.chart->y,
                              .u = icon->u,
                              .v = icon->v,
                              .palette = patch.chart->palette});
  }
  return result;
}

void publishRulesetChart(ui::Ui& ui, const assets::VirtualFileSystem& files,
                         const ruleset::Ruleset& rules) {
  const auto id = assets::AssetId::parse("data/evolution_chart");
  if (files.resolve(*id) == nullptr) {
    return;
  }
  auto chart = assets::loadEvolutionChart(files, *id);
  if (!chart) {
    core::logWarning("Chart metadata: {}", chart.error().message);
    return;
  }
  auto evolution = ruleset::loadPatchedEvolutionTable(files, rules);
  if (!evolution) {
    core::logWarning("Chart paths: {}", evolution.error().message);
    return;
  }
  auto extra = ruleset::readSpeciesExtra(rules);
  if (!extra) {
    core::logWarning("Chart cells: {}", extra.error().message);
    extra = ruleset::SpeciesExtra{};
  }
  auto species = ruleset::loadPatchedDigimonTable(files, rules);
  const assets::DigimonTable* table = species ? &*species : nullptr;
  const assets::EvolutionChart cells = chartWithExtraCells(*chart, *extra, *evolution, table);
  publishPlayerChart(ui, cells, table);
  publishChartPaths(ui, cells, *evolution);
  // The Mega step's Megas (its mega list): the chart draws them in a Mega box of their own.
  std::string megas;
  if (extra->megaStep) {
    for (const std::string& mega : extra->megaStep->mega) {
      megas += (megas.empty() ? "" : ",") + mega;
    }
  }
  ui.setSetting("player.chart.mega", megas);
  core::logInfo("Chart: {} cells for ruleset {}", cells.entries.size(), rules.id());
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
  ui.setSetting("player.chart.mega", std::string{});
  loadPlayerChartDetails(ui, files, *chart);
}

void loadPlayerMedalsAndCards(ui::Ui& ui, const assets::VirtualFileSystem& files) {
  if (auto medals = assets::loadMedalTable(files); !medals) {
    core::logWarning("Medals: {}", medals.error().message);
  } else if (*medals) {
    for (const auto& medal : (*medals)->medals) {
      const auto base = std::format("player.medal.{}.", medal.index);
      ui.setSetting(base + "name", medal.name);
      for (std::size_t line = 0; line < medal.description.size(); ++line)
        ui.setSetting(std::format("{}line.{}", base, line), medal.description[line]);
    }
  }
  if (auto cards = assets::loadCardTable(files); !cards) {
    core::logWarning("Cards: {}", cards.error().message);
  } else if (*cards) {
    for (const auto& card : (*cards)->cards)
      ui.setSetting(std::format("player.card.{}.palette", card.index),
                    static_cast<double>(card.iconPalette.value_or(0)));
  }
}
} // namespace dwe::app
