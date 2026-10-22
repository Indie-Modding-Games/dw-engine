#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/exe/executable_layout.hpp>
#include <dwe/formats/exe/tables.hpp>
#include <dwe/formats/exe/tables_yaml.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace dwe::dwx {

// The tables of the main executable and the string ids made from them. Extractors name assets after
// these ids, so the names are the same whichever file an asset comes from.
struct GameTables {
  formats::exe::ExecutableTables tables;
  formats::exe::TableIds ids;
};

// Reads every table of an executable with the layout of its version and makes their ids.
core::Result<GameTables> readGameTables(std::span<const std::byte> executable,
                                        const formats::exe::ExecutableLayout& layout);

// The volume path of a map's file, as the original builds it from the map's id and MAP_ENTRIES file
// name (map.c: buildMapPath): MAP/MAP<id / 15 + 1>/<name><extension>.
std::string mapVolumePath(std::size_t mapId, std::string_view filename, std::string_view extension);

} // namespace dwe::dwx
