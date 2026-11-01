#pragma once
#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/evolution_chart.hpp>
#include <dwe/assets/evolution_table.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/species_extra.hpp>
#include <dwe/ui/ui.hpp>

namespace dwe::app {
void publishPlayerChart(ui::Ui& ui, const assets::EvolutionChart& chart,
                        const assets::DigimonTable* species);
void publishChartDetailLayout(ui::Ui& ui, const assets::ChartDetailLayout& detail);
void loadPlayerChartDetails(ui::Ui& ui, const assets::VirtualFileSystem& files,
                            const assets::EvolutionChart& chart);
void loadPlayerChart(ui::Ui& ui, const assets::VirtualFileSystem& files);
// The arrows of each cell (player.chart.<n>.from.<slot>, .to.<slot>) from an evolution table.
void publishChartPaths(ui::Ui& ui, const assets::EvolutionChart& chart,
                       const assets::EvolutionTable& evolution);
// The chart's cells with the species a ruleset's patches add (species_extra: chart), each where its
// patch puts it, drawn with the icon of another species in another palette; only a species some
// path leads to.
assets::EvolutionChart chartWithExtraCells(const assets::EvolutionChart& chart,
                                           const ruleset::SpeciesExtra& extra,
                                           const assets::EvolutionTable& evolution,
                                           const assets::DigimonTable* species);
// The chart of a game's ruleset: its cells and arrows from the ruleset's patched tables, published
// again each time a game starts (player.chart.mega: the Mega step's Megas, comma-separated ids).
void publishRulesetChart(ui::Ui& ui, const assets::VirtualFileSystem& files,
                         const ruleset::Ruleset& rules);
// The medals chart's names and lines (player.medal.<n>.name, player.medal.<n>.line.<0-2>) and the
// card list's icon palettes (player.card.<n>.palette), from data/medals and data/cards.
void loadPlayerMedalsAndCards(ui::Ui& ui, const assets::VirtualFileSystem& files);
} // namespace dwe::app
