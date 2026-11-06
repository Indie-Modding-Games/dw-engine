#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace dwe::assets {

// What the game reads of an image that it decodes from its PNG file instead of loading it as a
// texture.
enum class PngReading : std::uint8_t {
  // Its palette indices and its palette (decodeIndexedPng): the game chooses the palette while it
  // runs.
  Indexed,
  // Its exact pixels (decodePng): a lossy texture would change them.
  ExactPixels,
};

// A kind of image the game reads from its PNG, by a pattern of asset ids (matchesIdPattern).
struct PngOnlyImage {
  std::string_view pattern;
  PngReading reading;
};

// The ids of bitmap font atlases.
inline constexpr std::string_view FontAtlasPattern = "font/*";

// Every kind of image the game reads from its PNG. A cooked pack keeps them as PNG (dwx cook passes
// them through) and the content check refuses any other file for them, so a pack the game cannot
// read fails before the game runs. Both ask pngReadingOf, the one rule; a new kind is one more
// entry here.
inline constexpr std::array PngOnlyImages = {
    // loadMapImages: the backgrounds, whose palette the time of day picks, and the texture pages,
    // whose sprites each
    // pick a CLUT row; also those of a mod's maps (map/<pack>/<name>/..., ContentId::assetPath).
    PngOnlyImage{.pattern = "map/*/background_*", .reading = PngReading::Indexed},
    PngOnlyImage{.pattern = "map/*/*/background_*", .reading = PngReading::Indexed},
    PngOnlyImage{.pattern = "map/*/texture8/*", .reading = PngReading::Indexed},
    PngOnlyImage{.pattern = "map/*/texture4/*", .reading = PngReading::Indexed},
    PngOnlyImage{.pattern = "map/*/*/texture8/*", .reading = PngReading::Indexed},
    PngOnlyImage{.pattern = "map/*/*/texture4/*", .reading = PngReading::Indexed},
    // ui::BitmapFont: the atlas of a bitmap font, whose alpha says which pixels are ink. The
    // original look's font
    // must name an atlas here (ui::parseOriginalLook and ui_text_font.schema.json refuse any
    // other).
    PngOnlyImage{.pattern = FontAtlasPattern, .reading = PngReading::ExactPixels},
};

// How the game reads the image of this id from its PNG, or nullopt when the game loads it as any
// texture: the rule that dwx cook, dwx validate and dwe-game --check-content share. `meta` is the
// image's `<id>_meta` document, or null when it has none: what the metadata declares may exempt an
// image that the id names (today nothing does), never add one. The metadata itself is never such an
// image.
std::optional<PngReading> pngReadingOf(const AssetId& id, const core::YamlNode* meta);

// Opens the file of an image the game reads from its PNG as the game reads it. Fails when the file
// is not a PNG, or is not one the reading takes (an indexed one, for Indexed).
core::Result<void> openPngOnlyImage(PngReading reading, std::string_view extension,
                                    std::span<const std::byte> bytes);

} // namespace dwe::assets
