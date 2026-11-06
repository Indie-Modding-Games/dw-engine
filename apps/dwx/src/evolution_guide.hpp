#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/evolution_table.hpp>
#include <dwe/core/error.hpp>

#include <filesystem>
#include <string>

namespace dwe::dwx {

// The evolution requirements of every species in Markdown (docs/guides/evoluciones.md, «Tabla por
// especie en tu equipo»): one section per level, one table per species that evolves, one row per
// target of its chart with the four scored groups (care mistakes, weight, stats, extra) as the
// original's calculateRequirementScore reads them. Names come from the species table (its id when
// it has no name). The text is in Spanish, like the guide it goes with.
std::string evolutionGuide(const assets::EvolutionTable& evolution,
                           const assets::DigimonTable& species);

// Reads the evolution and species tables from the packs in `content` (a content folder or one pack)
// and writes evolutionGuide to `out`. An existing `out` is never replaced.
core::Result<void> writeEvolutionGuide(const std::filesystem::path& content,
                                       const std::filesystem::path& out);

// dwx evolution-guide: writeEvolutionGuide, then prints where it wrote.
int evolutionGuideCommand(const std::filesystem::path& content, const std::filesystem::path& out);

} // namespace dwe::dwx
