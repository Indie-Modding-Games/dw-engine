#pragma once
#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/evolution_chart.hpp>
#include <dwe/ui/ui.hpp>

namespace dwe::app {
void publishPlayerChart(ui::Ui& ui, const assets::EvolutionChart& chart,
                        const assets::DigimonTable* species);
void publishChartDetailLayout(ui::Ui& ui, const assets::ChartDetailLayout& detail);
void loadPlayerChartDetails(ui::Ui& ui, const assets::VirtualFileSystem& files,
                            const assets::EvolutionChart& chart);
void loadPlayerChart(ui::Ui& ui, const assets::VirtualFileSystem& files);
// The medals chart's names and lines (player.medal.<n>.name, player.medal.<n>.line.<0-2>) and the
// card list's icon palettes (player.card.<n>.palette), from data/medals and data/cards.
void loadPlayerMedalsAndCards(ui::Ui& ui, const assets::VirtualFileSystem& files);
} // namespace dwe::app
