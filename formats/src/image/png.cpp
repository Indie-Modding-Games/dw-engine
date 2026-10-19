#include <dwe/formats/image/png.hpp>

#include <lodepng.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <span>

namespace dwe::formats::image {

namespace {

constexpr std::size_t MaxPaletteSize = 256;

// lodepng allocates its output with malloc unless it is built with its own allocators.
struct FreeBuffer {
    void operator()(unsigned char* buffer) const {
        // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): lodepng's buffer needs free.
        std::free(buffer);
    }
};

// Encodes the pixels with lodepng's C interface, which reads them in place and returns one buffer: copies of large
// images in vectors cost more than the compression in debug builds.
core::Result<std::vector<std::byte>> encode(std::span<const unsigned char> raw, std::uint32_t width,
                                            std::uint32_t height, lodepng::State& state) {
    // Write exactly the colour type asked for, never one lodepng finds smaller, so the output does not change with
    // the image's content.
    state.encoder.auto_convert = 0;
    unsigned char* buffer = nullptr;
    std::size_t size = 0;
    const unsigned error = lodepng_encode(&buffer, &size, raw.data(), width, height, &state);
    const std::unique_ptr<unsigned char, FreeBuffer> owner{buffer};
    if (error != 0) {
        return core::fail(std::format("cannot encode the PNG: {}", lodepng_error_text(error)));
    }
    std::vector<std::byte> png(size);
    std::memcpy(png.data(), buffer, size);
    return png;
}

core::Result<void> checkSize(std::uint32_t width, std::uint32_t height, std::size_t pixelCount) {
    if (width == 0 || height == 0) {
        return core::fail(std::format("cannot encode an empty {}x{} PNG", width, height));
    }
    if (const std::size_t expected = std::size_t{width} * height; pixelCount != expected) {
        return core::fail(
            std::format("a {}x{} PNG needs {} pixels, there are {}", width, height, expected, pixelCount));
    }
    return {};
}

} // namespace

core::Result<std::vector<std::byte>> encodePngRgba(const RgbaImage& image) {
    if (auto checked = checkSize(image.width, image.height, image.pixels.size()); !checked) {
        return std::unexpected{checked.error()};
    }
    std::vector<unsigned char> raw;
    raw.reserve(image.pixels.size() * 4);
    for (const Rgba8 pixel : image.pixels) {
        raw.insert(raw.end(), {pixel.r, pixel.g, pixel.b, pixel.a});
    }
    lodepng::State state;
    state.info_raw.colortype = LCT_RGBA;
    state.info_raw.bitdepth = 8;
    state.info_png.color.colortype = LCT_RGBA;
    state.info_png.color.bitdepth = 8;
    return encode(raw, image.width, image.height, state);
}

core::Result<std::vector<std::byte>> encodePngIndexed(std::uint32_t width, std::uint32_t height,
                                                      std::span<const std::uint8_t> indices,
                                                      std::span<const Rgba8> palette) {
    if (auto checked = checkSize(width, height, indices.size()); !checked) {
        return std::unexpected{checked.error()};
    }
    if (palette.empty() || palette.size() > MaxPaletteSize) {
        return core::fail(std::format("a PNG palette has 1 to 256 colours, not {}", palette.size()));
    }
    // Every 8-bit index is inside a full palette, so only smaller ones need the check.
    if (palette.size() < MaxPaletteSize) {
        if (const auto highest = std::ranges::max(indices); highest >= palette.size()) {
            return core::fail(std::format("colour index {} is outside the {}-colour palette", highest, palette.size()));
        }
    }
    lodepng::State state;
    for (LodePNGColorMode* mode : {&state.info_raw, &state.info_png.color}) {
        mode->colortype = LCT_PALETTE;
        mode->bitdepth = 8;
        for (const Rgba8 color : palette) {
            if (const unsigned error = lodepng_palette_add(mode, color.r, color.g, color.b, color.a); error != 0) {
                return core::fail(std::format("cannot build the PNG palette: {}", lodepng_error_text(error)));
            }
        }
    }
    return encode(indices, width, height, state);
}

} // namespace dwe::formats::image
