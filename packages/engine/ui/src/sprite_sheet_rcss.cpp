#include <dwe/core/message.hpp>
#include <dwe/ui/image_reference.hpp>
#include <dwe/ui/sprite_sheet_rcss.hpp>

#include <cstdint>
#include <format>

#include "messages.hpp"

namespace dwe::ui {
namespace {

// How many image pixels an original pixel covers along one side; nothing when the image is not a
// whole multiple.
std::optional<std::uint32_t> upscaleOf(std::uint32_t image, std::optional<std::uint32_t> original) {
  if (!original) {
    return 1;
  }
  if (image < *original || image % *original != 0) {
    return std::nullopt;
  }
  return image / *original;
}

} // namespace

core::Result<std::string> spriteSheetRcss(const SkinSpriteSheet& sheet, std::uint32_t imageWidth,
                                          std::uint32_t imageHeight) {
  const std::optional<std::uint32_t> across = upscaleOf(imageWidth, sheet.originalWidth);
  const std::optional<std::uint32_t> down = upscaleOf(imageHeight, sheet.originalHeight);
  if (!across || !down || *across != *down) {
    return core::fail(core::formatMessage(messages::SpriteSheetSizeMismatch, sheet.name, imageWidth,
                                          imageHeight));
  }
  const std::uint32_t scale = *across;
  // RmlUi draws a sprite at its size in image pixels divided by the resolution, in dp.
  const float resolution = static_cast<float>(scale) / sheet.dpPerPixel;
  std::string rcss = std::format("@spritesheet {} {{\n\tsrc: {};\n\tresolution: {}x;\n", sheet.name,
                                 imageReferenceText(sheet.image), resolution);
  // In 64 bits: a sprite's edge and its scaled rectangle can pass 32 bits even when the image is
  // small.
  const std::uint64_t wide = scale;
  for (const SkinSprite& sprite : sheet.sprites) {
    const std::uint64_t right = std::uint64_t{sprite.x} + sprite.width;
    const std::uint64_t bottom = std::uint64_t{sprite.y} + sprite.height;
    if (right > imageWidth / scale || bottom > imageHeight / scale) {
      return core::fail(core::formatMessage(messages::SpriteOutsideImage, sprite.name, sheet.name));
    }
    rcss += std::format("\t{}: {}px {}px {}px {}px;\n", sprite.name, sprite.x * wide,
                        sprite.y * wide, sprite.width * wide, sprite.height * wide);
  }
  rcss += "}\n";
  return rcss;
}

} // namespace dwe::ui
