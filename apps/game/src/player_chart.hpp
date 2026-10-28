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
} // namespace dwe::app
