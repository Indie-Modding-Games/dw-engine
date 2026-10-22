#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "extractor.hpp"

namespace dwe::dwx {

// A run of bytes that are not zero: [begin, end).
struct ByteRange {
  std::size_t begin = 0;
  std::size_t end = 0;
};

// The runs of bytes that are not zero, in order.
std::vector<ByteRange> nonZeroRanges(std::span<const std::byte> bytes);

// Describes byte ranges for a report: "0x10-0x20, 0x40-0x41", and how many more after the first
// few.
std::string describeRanges(std::span<const ByteRange> ranges);

// Files that only fill the disc: MMM.DAT is 27 MB of zeros that no code opens; only the CD file
// table of the main executable lists it (main/file_table_us.c). It gives no output; a byte that is
// not zero is reported.
bool handlesPadding(std::string_view path);
core::Result<Extraction> extractPadding(const Disc& disc, const formats::disc::IsoFile& file);

} // namespace dwe::dwx
