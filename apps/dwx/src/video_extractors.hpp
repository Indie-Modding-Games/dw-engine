#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <string_view>

#include "extractor.hpp"

// The extractor of the original's movies (MOVIE/*.STR). Each movie becomes its decoded video (a YUV4MPEG2 file with
// the MDEC's planes as they are), its decoded audio (a 16-bit PCM WAVE file) and a YAML document with what the two
// cannot hold: the stream's sector counts and the entry the game plays it with (MOV_MOVIES).
namespace dwe::dwx {

// The stream files of the MOVIE folder.
bool handlesMovie(std::string_view path);

// Reads every sector of the file, puts its frames together and checks that each one decodes, decodes its audio and
// reads the movie's MOV_MOVIES entry when the version is known. Needs a raw image: the 2048-byte view of a disc
// loses the audio sectors and the sector types.
core::Result<Extraction> extractMovie(const Disc& disc, const formats::disc::IsoFile& file);

} // namespace dwe::dwx
