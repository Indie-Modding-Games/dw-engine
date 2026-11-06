#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/exe/memory_image.hpp>
#include <dwe/formats/video/xa_adpcm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What the game knows about its movies, and the metadata document the extractor writes next to each
// one.
namespace dwe::formats::video {

// The overlay that plays the movies (mov.c), and where the game loads it (utils2.c:
// loadDynamicLibrary).
constexpr std::string_view MovieOverlayPath = "MOV_REL.BIN";
constexpr std::uint32_t MovieOverlayLoadAddress = 0x80010000;
// MOV_MOVIES has four entries of 0x28 bytes.
constexpr std::size_t MovieCount = 4;

// An entry of MOV_MOVIES (mov.c): the file the game opens, the frame it starts at and how many
// frames it shows before it stops (MOV_readFrame).
struct MovieEntry {
  std::string path; // as the game writes it: "\\MOVIE\\OP1.STR;1"
  std::int32_t startFrame;
  std::int32_t frameCount;
};

// Returns the address of MOV_MOVIES in a version's overlay (config/<version>/symbols_mov.txt of the
// decomp), or nullopt for an unknown version. Only the US address has been checked against a disc.
std::optional<std::uint32_t> movieTableAddress(std::string_view version);

// Reads MOV_MOVIES at an address of the mapped overlay. Each path must end inside its 0x20 bytes.
core::Result<std::vector<MovieEntry>> readMovieTable(const exe::MemoryImage& overlay,
                                                     std::uint32_t address);

// Turns a path as the game writes it into a volume path: "\\MOVIE\\OP1.STR;1" is "MOVIE/OP1.STR".
std::string movieVolumePath(std::string_view gamePath);

// What the extractor found in a movie file.
struct MovieInfo {
  std::string source; // volume path
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t frames = 0;     // decoded frames, all of them in the file
  std::uint32_t firstFrame = 0; // the number of the first one
  std::uint32_t fpsNumerator = 0;
  std::uint32_t fpsDenominator = 1;
  std::uint16_t bitstreamVersion = 0;
  std::optional<XaCoding> audio;
  std::uint64_t audioSampleFrames = 0;
  std::size_t sectors = 0;
  std::size_t videoSectors = 0;
  std::size_t audioSectors = 0;
  std::size_t paddingSectors = 0;
  std::optional<std::size_t>
      tableIndex; // the movie's entry in MOV_MOVIES, when the version is known
  std::optional<MovieEntry> tableEntry;
};

// Converts the info to the document of content/schemas/movie.schema.json: the source file, the
// video stream (size, frames, rate, how video.y4m stores it), the audio stream (or null), the
// sector counts and the MOV_MOVIES entry (or null).
core::YamlNode movieToYaml(const MovieInfo& info);

} // namespace dwe::formats::video
