#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace dwe::rhi {

// Pixel formats of images and render targets. Only the ones the engine uses; each maps to one Vulkan format.
enum class Format : std::uint8_t {
    Undefined,
    // 8-bit RGBA holding sRGB-encoded values, sampled and blended as they are (as the debug UI and the swapchain do).
    Rgba8Unorm,
    // The swapchain's format.
    Bgra8Unorm,
    Depth16Unorm,
    Depth24UnormX8,
    Depth32Float,
};

// Whether format holds depth.
constexpr bool isDepthFormat(Format format) {
    return format == Format::Depth16Unorm || format == Format::Depth24UnormX8 || format == Format::Depth32Float;
}

// Bytes per pixel of a color format, 0 for the others.
constexpr std::uint32_t bytesPerPixel(Format format) {
    return format == Format::Rgba8Unorm || format == Format::Bgra8Unorm ? 4 : 0;
}

// A size in pixels.
struct Extent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    friend bool operator==(const Extent&, const Extent&) = default;
};

// A rectangle of an image or of the screen, in pixels from the top left.
struct Rect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    friend bool operator==(const Rect&, const Rect&) = default;
};

// The part of rect inside a target of the given extent (empty when none is): Vulkan rejects scissors that start
// off the target, and UI libraries hand out clip rectangles that do.
constexpr Rect clampToExtent(const Rect& rect, Extent extent) {
    const auto clamp = [](std::int64_t value, std::uint32_t limit) {
        return static_cast<std::uint32_t>(std::clamp<std::int64_t>(value, 0, limit));
    };
    const std::uint32_t left = clamp(rect.x, extent.width);
    const std::uint32_t top = clamp(rect.y, extent.height);
    const std::uint32_t right = clamp(std::int64_t{rect.x} + rect.width, extent.width);
    const std::uint32_t bottom = clamp(std::int64_t{rect.y} + rect.height, extent.height);
    return {.x = static_cast<std::int32_t>(left),
            .y = static_cast<std::int32_t>(top),
            .width = right - left,
            .height = bottom - top};
}

// A viewport in pixels from the top left, with its depth range.
struct Viewport {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    float minDepth = 0.0F;
    float maxDepth = 1.0F;
};

enum class IndexType : std::uint8_t {
    Uint16,
    Uint32,
};

// Flag enums of this module opt in by specializing IsFlagEnum; they combine with | and are tested with hasAll.
template <typename T>
inline constexpr bool IsFlagEnum = false;

template <typename T>
concept FlagEnum = std::is_enum_v<T> && IsFlagEnum<T>;

template <FlagEnum T>
constexpr T operator|(T left, T right) {
    using Bits = std::underlying_type_t<T>;
    return static_cast<T>(static_cast<Bits>(static_cast<Bits>(left) | static_cast<Bits>(right)));
}

// Whether flags holds every bit of wanted.
template <FlagEnum T>
constexpr bool hasAll(T flags, T wanted) {
    using Bits = std::underlying_type_t<T>;
    return (static_cast<Bits>(flags) & static_cast<Bits>(wanted)) == static_cast<Bits>(wanted);
}

} // namespace dwe::rhi
