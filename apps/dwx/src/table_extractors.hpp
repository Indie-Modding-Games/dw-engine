#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <string_view>

#include "extractor.hpp"

namespace dwe::dwx {

// The boot executable (SLUS_010.32 and the like): the documents of its tables. Its code is not an asset.
bool handlesExecutable(std::string_view path);
core::Result<Extraction> extractExecutable(const Disc& disc, const formats::disc::IsoFile& file);

// SYSTEM.CNF: the boot settings, one "KEY = VALUE" per line.
bool handlesBootConfig(std::string_view path);
core::Result<Extraction> extractBootConfig(const Disc& disc, const formats::disc::IsoFile& file);

} // namespace dwe::dwx
