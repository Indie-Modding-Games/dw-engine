#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <string_view>

#include "extractor.hpp"

// The extractors of the original's images. Each image becomes a PNG and a YAML file with what the PNG cannot hold:
// where the original puts it in video memory, its mode and every palette as raw console colours.
namespace dwe::dwx {

// The known fixed-stride packs (CHDAT/ALLTIM.TIM, CARD/CARD.ALL): one TIM at the start of each entry.
bool handlesTimPack(std::string_view path);
core::Result<Extraction> extractTimPack(const Disc& disc, const formats::disc::IsoFile& file);

// The stacked TIM files (ETCDAT/ETCTIM.BIN, STDDAT/STDTIM.BIN), walked as the original does.
bool handlesStackedTims(std::string_view path);
core::Result<Extraction> extractStackedTims(const Disc& disc, const formats::disc::IsoFile& file);

// Any other .TIM file: one TIM, or several one after another.
bool handlesTimFile(std::string_view path);
core::Result<Extraction> extractTimFile(const Disc& disc, const formats::disc::IsoFile& file);

} // namespace dwe::dwx
