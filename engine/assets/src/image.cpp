#include <dwe/assets/image.hpp>

#include <lodepng.h>

#include <algorithm>
#include <format>

namespace dwe::assets {

core::Result<Image> decodePng(std::span<const std::byte> png) {
    const auto* data = reinterpret_cast<const unsigned char*>(png.data());
    unsigned width = 0;
    unsigned height = 0;
    lodepng::State state;
    if (const unsigned error = lodepng_inspect(&width, &height, &state, data, png.size()); error != 0) {
        return core::fail(std::format("not a PNG: {}", lodepng_error_text(error)));
    }
    if (width > MaxImageSide || height > MaxImageSide) {
        return core::fail(std::format("the PNG is {}x{}, larger than {} on a side", width, height, MaxImageSide));
    }
    std::vector<unsigned char> rgba;
    if (const unsigned error = lodepng::decode(rgba, width, height, data, png.size(), LCT_RGBA, 8); error != 0) {
        return core::fail(std::format("cannot decode the PNG: {}", lodepng_error_text(error)));
    }
    Image image{.width = width, .height = height, .pixels = std::vector<std::byte>(rgba.size())};
    std::ranges::transform(rgba, image.pixels.begin(), [](unsigned char value) { return std::byte{value}; });
    return image;
}

} // namespace dwe::assets
