#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/image/tim.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <string_view>

namespace dwe::formats::image {

namespace {

constexpr std::size_t HeaderSize = 8;       // magic and flags
constexpr std::size_t BlockHeaderSize = 12; // length, x, y, width, height
constexpr std::uint32_t ModeMask = 0x7;
constexpr std::uint32_t ClutFlag = 0x8;
constexpr std::uint16_t VramWidth = 1024; // in 16-bit units
constexpr std::uint16_t VramHeight = 512;

struct Block {
    std::size_t length = 0;
    VramRect rect;
    std::span<const std::byte> data; // rect.width × 2 × rect.height bytes
};

// Reads a CLUT or image block: {u32 length; u16 x, y, width, height; data}. The length counts the block header.
core::Result<Block> readBlock(std::span<const std::byte> bytes, std::size_t offset, std::string_view name) {
    const ByteReader reader{bytes};
    const auto length = reader.u32(offset);
    const auto x = reader.u16(offset + 4);
    const auto y = reader.u16(offset + 6);
    const auto width = reader.u16(offset + 8);
    const auto height = reader.u16(offset + 10);
    if (!length || !x || !y || !width || !height) {
        return core::fail(std::format("TIM {} block header at 0x{:x} runs past the end", name, offset));
    }
    const VramRect rect{.x = *x, .y = *y, .width = *width, .height = *height};
    if (rect.width == 0 || rect.height == 0 || rect.width > VramWidth || rect.height > VramHeight) {
        return core::fail(std::format("TIM {} block has an invalid size {}x{}", name, rect.width, rect.height));
    }
    const std::size_t dataSize = std::size_t{rect.width} * 2 * rect.height;
    if (*length < BlockHeaderSize + dataSize) {
        return core::fail(
            std::format("TIM {} block length {} is too small for {}x{}", name, *length, rect.width, rect.height));
    }
    if (!reader.contains(offset, *length)) {
        return core::fail(std::format("TIM {} block length {} runs past the end", name, *length));
    }
    return Block{.length = *length, .rect = rect, .data = bytes.subspan(offset + BlockHeaderSize, dataSize)};
}

bool isIndexed(TimPixelMode mode) {
    return mode == TimPixelMode::Indexed4 || mode == TimPixelMode::Indexed8;
}

std::size_t paletteSize(const Tim& tim) {
    const std::size_t full = tim.mode == TimPixelMode::Indexed4 ? 16 : 256;
    return std::min<std::size_t>(full, tim.clutRect.width);
}

std::uint8_t widen5(unsigned value) {
    return static_cast<std::uint8_t>((value << 3U) | (value >> 2U));
}

} // namespace

core::Result<Tim> readTim(std::span<const std::byte> bytes) {
    const ByteReader reader{bytes};
    const auto magic = reader.u32(0);
    const auto flagsWord = reader.u32(4);
    if (!magic || !flagsWord) {
        return core::fail(std::format("TIM header needs {} bytes, there are {}", HeaderSize, bytes.size()));
    }
    if (*magic != TimMagic) {
        return core::fail(std::format("not a TIM: the first word is 0x{:x}", *magic));
    }
    const std::uint32_t flags = *flagsWord;
    if ((flags & ModeMask) > static_cast<std::uint32_t>(TimPixelMode::Direct24)) {
        return core::fail(std::format("TIM pixel mode {} is not supported", flags & ModeMask));
    }

    Tim tim;
    tim.mode = static_cast<TimPixelMode>(flags & ModeMask);
    tim.hasClut = (flags & ClutFlag) != 0;
    std::size_t offset = HeaderSize;
    if (tim.hasClut) {
        auto clut = readBlock(bytes, offset, "CLUT");
        if (!clut) {
            return std::unexpected{clut.error()};
        }
        tim.clutRect = clut->rect;
        tim.clut.reserve(clut->data.size() / 2);
        const ByteReader colors{clut->data};
        for (std::size_t i = 0; i < clut->data.size(); i += 2) {
            tim.clut.push_back(colors.u16(i).value_or(0));
        }
        offset += clut->length;
    }
    auto image = readBlock(bytes, offset, "image");
    if (!image) {
        return std::unexpected{image.error()};
    }
    tim.imageRect = image->rect;
    tim.data.assign(image->data.begin(), image->data.end());
    tim.byteSize = offset + image->length;
    return tim;
}

std::uint32_t timWidth(const Tim& tim) {
    const std::uint32_t units = tim.imageRect.width;
    switch (tim.mode) {
    case TimPixelMode::Indexed4:
        return units * 4;
    case TimPixelMode::Indexed8:
        return units * 2;
    case TimPixelMode::Direct16:
        return units;
    case TimPixelMode::Direct24:
        return units * 2 / 3; // any remainder of a row is padding
    }
    return 0;
}

std::size_t timPaletteCount(const Tim& tim) {
    if (!isIndexed(tim.mode) || !tim.hasClut) {
        return 0;
    }
    return std::size_t{tim.clutRect.width} / paletteSize(tim) * tim.clutRect.height;
}

core::Result<std::vector<Color15>> timPalette(const Tim& tim, std::size_t paletteIndex) {
    const std::size_t count = timPaletteCount(tim);
    if (paletteIndex >= count) {
        return core::fail(std::format("TIM palette {} does not exist; the TIM has {}", paletteIndex, count));
    }
    const std::size_t size = paletteSize(tim);
    const std::size_t perRow = tim.clutRect.width / size;
    const std::size_t start = ((paletteIndex / perRow) * tim.clutRect.width) + ((paletteIndex % perRow) * size);
    return std::vector<Color15>(tim.clut.begin() + static_cast<std::ptrdiff_t>(start),
                                tim.clut.begin() + static_cast<std::ptrdiff_t>(start + size));
}

core::Result<std::vector<std::uint8_t>> timIndices(const Tim& tim) {
    if (!isIndexed(tim.mode)) {
        return core::fail("only 4- and 8-bit TIMs have colour indices");
    }
    // Each byte is one index of an 8-bit image; copied whole, which matters for large images in debug builds.
    if (tim.mode == TimPixelMode::Indexed8) {
        std::vector<std::uint8_t> indices(tim.data.size());
        std::memcpy(indices.data(), tim.data.data(), tim.data.size());
        return indices;
    }
    std::vector<std::uint8_t> indices(tim.data.size() * 2);
    for (std::size_t i = 0; i < tim.data.size(); ++i) {
        // The first pixel of each byte is in the low nibble.
        const auto value = std::to_integer<std::uint8_t>(tim.data[i]);
        indices[2 * i] = value & 0x0FU;
        indices[(2 * i) + 1] = value >> 4U;
    }
    return indices;
}

Rgba8 toRgba8(Color15 color) {
    return Rgba8{.r = widen5(color & 0x1FU),
                 .g = widen5((color >> 5U) & 0x1FU),
                 .b = widen5((color >> 10U) & 0x1FU),
                 .a = static_cast<std::uint8_t>(color == 0 ? 0 : 255)};
}

core::Result<RgbaImage> decodeTim(const Tim& tim, std::size_t paletteIndex) {
    RgbaImage image{.width = timWidth(tim), .height = tim.imageRect.height, .pixels = {}};
    image.pixels.reserve(std::size_t{image.width} * image.height);

    switch (tim.mode) {
    case TimPixelMode::Indexed4:
    case TimPixelMode::Indexed8: {
        auto palette = timPalette(tim, paletteIndex);
        if (!palette) {
            return std::unexpected{palette.error()};
        }
        auto indices = timIndices(tim);
        if (!indices) {
            return std::unexpected{indices.error()};
        }
        for (const std::uint8_t index : *indices) {
            if (index >= palette->size()) {
                return core::fail(
                    std::format("TIM colour index {} is outside its {}-colour palette", index, palette->size()));
            }
            image.pixels.push_back(toRgba8((*palette)[index]));
        }
        break;
    }
    case TimPixelMode::Direct16: {
        const ByteReader pixels{tim.data};
        for (std::size_t i = 0; i < tim.data.size(); i += 2) {
            image.pixels.push_back(toRgba8(pixels.u16(i).value_or(0)));
        }
        break;
    }
    case TimPixelMode::Direct24: {
        const std::size_t rowBytes = std::size_t{tim.imageRect.width} * 2;
        for (std::size_t row = 0; row < image.height; ++row) {
            for (std::size_t column = 0; column < image.width; ++column) {
                const std::size_t at = (row * rowBytes) + (column * 3);
                image.pixels.push_back(Rgba8{.r = std::to_integer<std::uint8_t>(tim.data[at]),
                                             .g = std::to_integer<std::uint8_t>(tim.data[at + 1]),
                                             .b = std::to_integer<std::uint8_t>(tim.data[at + 2]),
                                             .a = 255});
            }
        }
        break;
    }
    }
    return image;
}

} // namespace dwe::formats::image
