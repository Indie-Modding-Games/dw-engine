#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include "extractor.hpp"

// The strings and the font of the executable and the overlays. The executable's extractor calls it,
// since one disc file has one extractor; the overlays are read for their strings only.
namespace dwe::dwx {

// Adds text/<language>/<source> for the executable ("main") and each overlay of the version's
// string manifest, and the font as font/main (PNG atlas) with its metadata. What cannot be read is
// reported in the extraction's unknowns; it fails only if an output cannot be made.
core::Result<void> addExecutableText(const Disc& disc, const formats::disc::IsoFile& executable,
                                     Extraction& extraction);

} // namespace dwe::dwx
