#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/evolution_table.hpp>
#include <dwe/core/error.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace dwe::dwx {

// The language code of the guide when none is asked for.
inline constexpr std::string_view DefaultGuideLanguage = "en";

// The evolution requirements of every species in Markdown (docs/guides/evolutions.md, "Per-species
// table on your machine"): one section per level, one table per species that evolves, one row per
// target of its chart with the four scored groups (care mistakes, weight, stats, bonus) as the
// original's calculateRequirementScore reads them. Names come from the species table (its id when
// it has no name). The text is in `language`: "en" or "es" (anything else is a precondition
// failure; writeEvolutionGuide rejects it first).
std::string evolutionGuide(const assets::EvolutionTable& evolution,
                           const assets::DigimonTable& species,
                           std::string_view language = DefaultGuideLanguage);

// Reads the evolution and species tables from the packs in `content` (a content folder or one pack)
// and writes evolutionGuide to `out`. An existing `out` is never replaced; an unknown `language` is
// an error.
core::Result<void> writeEvolutionGuide(const std::filesystem::path& content,
                                       const std::filesystem::path& out,
                                       std::string_view language = DefaultGuideLanguage);

// dwx evolution-guide: writeEvolutionGuide, then prints where it wrote.
int evolutionGuideCommand(const std::filesystem::path& content, const std::filesystem::path& out,
                          std::string_view language);

} // namespace dwe::dwx
