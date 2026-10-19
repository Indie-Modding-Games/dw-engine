#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <cstddef>
#include <span>
#include <string_view>

#include "extractor.hpp"

// The battle effects of ETCHI/EFEDAT.EFE: one slot per technique, each with its scripts, texture and model.
namespace dwe::dwx {

bool handlesEffectFile(std::string_view path);

// Writes, for every slot in use, effect/<move>/effect (the header and the technique), script (the decoded scripts
// and data area), texture (the TIM) and, when the slot has a TMD, model (textured as the slot's first VRAM cell
// holds it).
core::Result<Extraction> extractEffectFile(const Disc& disc, const formats::disc::IsoFile& file);

// The same, from the file's bytes.
core::Result<Extraction> extractEffects(std::span<const std::byte> bytes, const GameTables* tables,
                                        std::string_view source);

} // namespace dwe::dwx
