#pragma once
#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/evolution_chart.hpp>
#include <dwe/ui/ui.hpp>

namespace dwe::app {
void publishPlayerChart(ui::Ui& ui, const assets::EvolutionChart& chart,
                        const assets::DigimonTable* species);
void loadPlayerChart(ui::Ui& ui, const assets::VirtualFileSystem& files);
} // namespace dwe::app
