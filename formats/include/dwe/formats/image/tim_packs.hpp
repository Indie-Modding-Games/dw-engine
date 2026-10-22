#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/image/tim.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::formats::image {

// A TIM of a stacked TIM file and the offset where it starts.
struct StackedTim {
  std::size_t offset = 0;
  Tim tim;
};

// The TIMs of a stacked TIM file, in file order.
struct StackedTims {
  std::vector<StackedTim> entries;
  std::size_t end = 0;               // where the list stops: the first word that is not a TIM magic
  std::vector<std::string> warnings; // places where the file disagrees with itself
};

// Reads a file of TIMs stored one after another, as the original does (utils2.c:
// loadStackedTIMFile): the list stops at the first word that is not 0x10, and each TIM takes the
// size its width × height give, not its block lengths. When they disagree the original's size is
// used and the result carries a warning.
core::Result<StackedTims> readStackedTims(std::span<const std::byte> bytes);

// Reads a file of TIMs stored one after another, each starting where the block lengths of the one
// before end. A plain .TIM file is the case of one TIM; the arena textures hold several
// (std_setup.c: STD_loadArenaTIMToVRAM). The list stops at the first word that is not 0x10.
core::Result<StackedTims> readTimSequence(std::span<const std::byte> bytes);

// What each entry of a pack holds.
enum class PackEntryFormat : std::uint8_t { Tim };

// A pack of the original: entries at a fixed stride with no header, whose stride and count are in
// the code that reads it, not in the file.
struct KnownPack {
  std::string_view path; // on the disc
  std::size_t strideBytes = 0;
  std::size_t entryCount = 0;
  PackEntryFormat entryFormat = PackEntryFormat::Tim;
  std::string_view indexedBy; // what the original uses as the entry number
  std::string_view source;    // the decomp code that reads it
};

// Returns the packs the tools know.
std::span<const KnownPack> knownPacks();

// Splits a pack into its entries of `strideBytes` bytes. Fails if the size is not a whole number of
// entries.
core::Result<std::vector<std::span<const std::byte>>> splitPack(std::span<const std::byte> bytes,
                                                                std::size_t strideBytes);

// Splits a known pack and checks that it has the entry count the original expects.
core::Result<std::vector<std::span<const std::byte>>>
splitKnownPack(std::span<const std::byte> bytes, const KnownPack& pack);

} // namespace dwe::formats::image
