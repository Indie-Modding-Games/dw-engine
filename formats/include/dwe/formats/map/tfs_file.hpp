#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/map/map_file.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The .TFS file of a field map: the palettes and the 128 x 128 8-bit tiles of its pre-rendered background
// (map.c: readMapTFS, setupMap, updateTimeOfDay).
namespace dwe::formats::map {

inline constexpr std::size_t TileSize = 128;
inline constexpr std::size_t PaletteSize = 256;

// 256 colours in the console's 15-bit format: bits 0-4 red, 5-9 green, 10-14 blue, bit 15 the semi-transparency
// flag.
using Palette = std::array<std::uint16_t, PaletteSize>;

struct TfsTile {
    std::uint32_t unknown00 = 0;      // skipped by the game; in the US files, row * 128 << 16 | column * 64
    std::vector<std::uint8_t> pixels; // TileSize * TileSize palette indices, row by row

    bool operator==(const TfsTile&) const = default;
};

struct TfsFile {
    std::uint32_t unknown00 = 0; // skipped by the game; in the US files, height << 16 | width in pixels
    // Palette 0 is the day, 1 dusk and 2 night (updateTimeOfDay); maps with the fixed-time flag use only 0. What
    // palettes past 2 are for is not known: the game uploads them to rows 484 and on.
    std::vector<Palette> palettes;
    std::vector<TfsTile> tiles; // in the order of the grid's non-empty cells

    bool operator==(const TfsFile&) const = default;
};

// An 8-bit indexed image and its palette, ready for an image writer.
struct IndexedImage {
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<std::uint8_t> indices; // width * height, row by row
    Palette palette{};

    bool operator==(const IndexedImage&) const = default;
};

// Reads a .TFS file. The tile count follows from the file size, which must hold whole tiles.
core::Result<TfsFile> readTfsFile(std::span<const std::byte> file);

// Lays the tiles out on the map's grid, once per palette: width * 128 by height * 128 pixels. Tiles are taken in
// order for the cells that are not -1; empty cells are index 0 (the game clears them to black). Fails if the grid
// has more non-empty cells than the file has tiles.
core::Result<std::vector<IndexedImage>> composeBackgrounds(const TfsFile& tfs, const MapSetup& setup);

// The name of a palette for file names: day, dusk, night, then palette3, palette4...
std::string backgroundVariantName(std::size_t palette);

} // namespace dwe::formats::map
