#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/map/map_file.hpp>

#include <cstddef>
#include <string_view>

#include "extractor.hpp"

// The extractors of the field maps and the arenas. Both files of a map need its MAP_ENTRIES record,
// so they need the executable's tables; the map's slug is its id there.
namespace dwe::dwx {

// MAP/MAP<n>/<name>.MAP: map/<slug>/data.yaml and the map's TIMs as PNG and metadata.
bool handlesMapFile(std::string_view path);
core::Result<Extraction> extractMapFile(const Disc& disc, const formats::disc::IsoFile& file);

// The .MAP file of the map with this MAP_ENTRIES id, read with its record, for extractors that need
// a map's data.
core::Result<formats::map::MapFile> readMapOf(const Disc& disc, std::size_t mapId);

// MAP/MAP<n>/<name>.TFS: the pre-rendered background, one indexed PNG per palette
// (map/<slug>/background_day...).
bool handlesMapBackground(std::string_view path);
core::Result<Extraction> extractMapBackground(const Disc& disc, const formats::disc::IsoFile& file);

// STDDAT/<name>.ATR: the collision grid of a tournament arena, the same 100 x 100 grid as a map's
// (std_setup.c: STD_loadArenaAssets, vs_scene.c: VS_loadArenaAssets), as
// arena/<name>/collision.yaml.
bool handlesArenaCollision(std::string_view path);
core::Result<Extraction> extractArenaCollision(const Disc& disc,
                                               const formats::disc::IsoFile& file);

} // namespace dwe::dwx
