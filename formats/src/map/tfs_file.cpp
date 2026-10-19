#include <dwe/formats/map/tfs_file.hpp>

#include <bytes/byte_cursor.hpp>

#include <algorithm>
#include <array>
#include <format>

namespace dwe::formats::map {

namespace {

using bytes::ByteCursor;

constexpr std::size_t TilePixels = TileSize * TileSize;
constexpr std::size_t TileRecordSize = 4 + TilePixels;
// CLUT i goes to VRAM row 481 + i, and VRAM has 512 rows.
constexpr std::uint32_t MaxPalettes = 31;

} // namespace

core::Result<TfsFile> readTfsFile(std::span<const std::byte> file) {
    ByteCursor c{file};
    TfsFile tfs;
    tfs.unknown00 = c.u32();
    const std::uint32_t paletteCount = c.u32();
    if (c.overrun()) {
        return core::fail("the file is shorter than its header");
    }
    if (paletteCount > MaxPalettes) {
        return core::fail(std::format("{} palettes; VRAM has room for {}", paletteCount, MaxPalettes));
    }
    for (std::uint32_t i = 0; i < paletteCount; ++i) {
        Palette& palette = tfs.palettes.emplace_back();
        std::ranges::generate(palette, [&] { return c.u16(); });
    }
    if (c.overrun()) {
        return core::fail(std::format("the file is shorter than its {} palettes", paletteCount));
    }
    if (c.remaining() % TileRecordSize != 0) {
        return core::fail(
            std::format("{} bytes after the palettes are not whole tiles of {} bytes", c.remaining(), TileRecordSize));
    }
    while (c.remaining() > 0) {
        TfsTile& tile = tfs.tiles.emplace_back();
        tile.unknown00 = c.u32();
        const auto pixels = c.take(TilePixels);
        tile.pixels.resize(TilePixels);
        std::ranges::transform(pixels, tile.pixels.begin(),
                               [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
    }
    return tfs;
}

core::Result<std::vector<IndexedImage>> composeBackgrounds(const TfsFile& tfs, const MapSetup& setup) {
    if (setup.width < 0 || setup.height < 0 ||
        setup.tiles.size() != static_cast<std::size_t>(setup.width) * static_cast<std::size_t>(setup.height)) {
        return core::fail("the tile grid does not match its width and height");
    }
    const auto columns = static_cast<std::size_t>(setup.width);
    const auto used =
        static_cast<std::size_t>(std::ranges::count_if(setup.tiles, [](std::int32_t id) { return id != -1; }));
    if (used > tfs.tiles.size()) {
        return core::fail(std::format("the grid uses {} tiles but the file has {}", used, tfs.tiles.size()));
    }

    IndexedImage base;
    base.width = columns * TileSize;
    base.height = static_cast<std::size_t>(setup.height) * TileSize;
    base.indices.assign(base.width * base.height, 0);
    std::size_t next = 0;
    for (std::size_t cell = 0; cell < setup.tiles.size(); ++cell) {
        if (setup.tiles[cell] == -1) {
            continue;
        }
        const TfsTile& tile = tfs.tiles[next++];
        const std::size_t left = (cell % columns) * TileSize;
        const std::size_t top = (cell / columns) * TileSize;
        for (std::size_t row = 0; row < TileSize; ++row) {
            const auto source = std::span{tile.pixels}.subspan(row * TileSize, TileSize);
            std::ranges::copy(source,
                              base.indices.begin() + static_cast<std::ptrdiff_t>(((top + row) * base.width) + left));
        }
    }

    std::vector<IndexedImage> variants;
    variants.reserve(tfs.palettes.size());
    for (const Palette& palette : tfs.palettes) {
        IndexedImage& variant = variants.emplace_back(base);
        variant.palette = palette;
    }
    return variants;
}

std::string backgroundVariantName(std::size_t palette) {
    constexpr std::array<std::string_view, 3> Names = {"day", "dusk", "night"};
    return palette < Names.size() ? std::string{Names[palette]} : std::format("palette{}", palette);
}

} // namespace dwe::formats::map
