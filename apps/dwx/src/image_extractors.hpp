#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/image/tim.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "extractor.hpp"

// The extractors of the original's images. Each image becomes a PNG and a YAML file with what the
// PNG cannot hold: where the original puts it in video memory, its mode and every palette as raw
// console colours.
namespace dwe::dwx {

// Where an image comes from in its disc file.
struct TimPlace {
  std::string_view source;          // the disc file
  std::optional<std::size_t> entry; // its number in a pack, a list of several or a map's images
  std::size_t offset = 0;           // where its TIM starts in the file
};

// Adds the PNG and the metadata (<id>_meta) of one TIM: indexed with palette 0 when it has a CLUT,
// RGBA when its colours are direct. The reader's warnings go to the extraction's unknowns and to
// the metadata's warnings; an image that cannot be encoded gets neither file, only an unknown.
core::Result<void> addTimImage(Extraction& extraction, const assets::AssetId& id,
                               const formats::image::Tim& tim, const TimPlace& place,
                               std::vector<std::string> warnings = {});

// The known fixed-stride packs (CHDAT/ALLTIM.TIM, CARD/CARD.ALL): one TIM at the start of each
// entry.
bool handlesTimPack(std::string_view path);
core::Result<Extraction> extractTimPack(const Disc& disc, const formats::disc::IsoFile& file);

// The stacked TIM files (ETCDAT/ETCTIM.BIN, STDDAT/STDTIM.BIN), walked as the original does.
bool handlesStackedTims(std::string_view path);
core::Result<Extraction> extractStackedTims(const Disc& disc, const formats::disc::IsoFile& file);

// Any other .TIM file: one TIM, or several one after another.
bool handlesTimFile(std::string_view path);
core::Result<Extraction> extractTimFile(const Disc& disc, const formats::disc::IsoFile& file);

} // namespace dwe::dwx
