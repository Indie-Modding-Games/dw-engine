#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <string_view>

#include "extractor.hpp"

namespace dwe::dwx {

// The overlays (*_REL.BIN at the root): code and data a scene loads. The code is not an asset; the
// files some of them carry are the loose files of the disc again; the gameplay tables of five of
// them (fishing, tournament, versus, training, battle) become YAML documents. Every part of the
// file is accounted for in the notes, or reported as unknown.
bool handlesOverlay(std::string_view path);
core::Result<Extraction> extractOverlay(const Disc& disc, const formats::disc::IsoFile& file);

} // namespace dwe::dwx
