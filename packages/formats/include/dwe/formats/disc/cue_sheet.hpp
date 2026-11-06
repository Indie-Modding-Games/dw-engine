#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/sector.hpp>

#include <string>
#include <string_view>

namespace dwe::formats::disc {

// What a CUE sheet says about a single-track data disc.
struct CueSheet {
  std::string
      dataFile; // as written in the sheet; relative paths are relative to the sheet's folder
  SectorLayout layout;
};

// Parses a CUE sheet with one BINARY file holding one data track (MODE2/2352, MODE1/2352 or
// MODE1/2048) that starts at 00:00:00. Anything else (several files or tracks, audio, pregaps) is
// rejected with an error saying what.
core::Result<CueSheet> parseCueSheet(std::string_view text);

} // namespace dwe::formats::disc
