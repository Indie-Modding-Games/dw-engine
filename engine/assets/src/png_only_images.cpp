#include <dwe/assets/image.hpp>
#include <dwe/assets/image_palettes.hpp>
#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/png_only_images.hpp>
#include <dwe/core/message.hpp>

#include <algorithm>

#include "messages.hpp"

namespace dwe::assets {

namespace {

constexpr std::string_view PngExtension = "png";

std::string_view readingName(PngReading reading) {
    return reading == PngReading::Indexed ? messages::IndexedReadingName : messages::ExactPixelsReadingName;
}

} // namespace

std::optional<PngReading> pngReadingOf(const AssetId& id, const core::YamlNode* /*meta*/) {
    if (id.str().ends_with(ImageMetaSuffix)) {
        return std::nullopt;
    }
    const auto kind = std::ranges::find_if(
        PngOnlyImages, [&id](const PngOnlyImage& image) { return matchesIdPattern(image.pattern, id); });
    return kind == PngOnlyImages.end() ? std::nullopt : std::optional{kind->reading};
}

core::Result<void> openPngOnlyImage(PngReading reading, std::string_view extension, std::span<const std::byte> bytes) {
    if (extension != PngExtension) {
        return core::fail(core::formatMessage(messages::PngOnlyImageNotPng, readingName(reading), extension));
    }
    if (reading == PngReading::Indexed) {
        if (auto image = decodeIndexedPng(bytes); !image) {
            return core::fail(core::formatMessage(messages::PngOnlyImageNotIndexed, image.error().message));
        }
        return {};
    }
    if (auto image = decodePng(bytes); !image) {
        return core::fail(core::formatMessage(messages::PngOnlyImageUnreadable, image.error().message));
    }
    return {};
}

} // namespace dwe::assets
