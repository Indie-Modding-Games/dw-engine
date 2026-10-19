#pragma once

#include <dwe/core/yaml.hpp>
#include <dwe/formats/map/map_file.hpp>

#include <span>
#include <string>

namespace dwe::formats::map {

// The string ids of the tables a map refers to, by index (exe::makeTableIds). Missing ids are written as null.
struct MapYamlIds {
    std::span<const std::string> maps;
    std::span<const std::string> digimon;
};

// Converts a map to the YAML document of content/schemas/map.schema.json: camera, lights, tile grid, TIM sections,
// sprite objects, warps, NPCs and collision. Raw numbers stay as they are in the file; where a number refers to
// another table, the id is written next to it. The collision grid is 100 rows of 100 characters, one per cell,
// with a legend: '.' = free, '#' = blocked and a letter or digit per trigger value.
core::YamlNode mapToYaml(const MapFile& map, const MapYamlIds& ids = {});

// Converts a collision grid, CollisionSize rows of CollisionSize cells, to the collision node of mapToYaml: one
// character per cell when the values fit the legend's symbols, otherwise two hex digits per cell. The tournament
// arenas store the same grid in their .ATR files.
core::YamlNode collisionToYaml(std::span<const std::uint8_t> cells);

} // namespace dwe::formats::map
