#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dwe::assets {

// One logical Ogg bitstream, as readOggStream reassembles it from its pages.
struct OggStream {
  std::uint32_t serial = 0;
  std::vector<std::vector<std::byte>> packets;
  // The granule position of the last page: for Opus, the 48 kHz sample count including the
  // pre-skip.
  std::int64_t lastGranule = 0;
};

// Reads an Ogg file that holds one logical bitstream (RFC 3533): every page must have the capture
// pattern, version 0, a matching CRC, the stream's serial number and the next sequence number; the
// first page starts the stream, the last one ends it, nothing follows it, and packets split across
// pages must continue on the next one.
core::Result<OggStream> readOggStream(std::span<const std::byte> bytes);

} // namespace dwe::assets
