#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <string_view>

#include "extractor.hpp"

// The extractors of the field maps. Both files of a map need its MAP_ENTRIES record, so they need the executable's
// tables; the map's slug is its id there.
namespace dwe::dwx {

// MAP/MAP<n>/<name>.MAP: map/<slug>/data.yaml and the map's TIMs as PNG and metadata.
bool handlesMapFile(std::string_view path);
core::Result<Extraction> extractMapFile(const Disc& disc, const formats::disc::IsoFile& file);

// MAP/MAP<n>/<name>.TFS: the pre-rendered background, one indexed PNG per palette (map/<slug>/background_day...).
bool handlesMapBackground(std::string_view path);
core::Result<Extraction> extractMapBackground(const Disc& disc, const formats::disc::IsoFile& file);

} // namespace dwe::dwx
