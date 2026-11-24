#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/game/randomizer/randomizer.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::randomizer {

// The species a pick may give, by their level in the Digimon table (DigimonRow::level), sorted.
using StagePools = std::map<std::int32_t, std::vector<std::string>>;

// Groups a roster of species ids by their level; ids the table lacks or without a model are left
// out.
[[nodiscard]] StagePools stagePools(const assets::DigimonTable& species,
                                    std::span<const std::string> roster);

// Gives the map's wild enemies (Randomizer::enemy) a species of their original's level and
// techniques of the new species in their used slots. Their stats, places, scripts and type ids stay
// the record's, so the fight is as hard as the original's and the story scripts find them.
void randomizeMapEnemies(const Randomizer& randomizer, std::string_view map,
                         std::span<assets::MapNpc> npcs, const assets::DigimonTable& species,
                         const StagePools& pools);

} // namespace dwe::game::randomizer
