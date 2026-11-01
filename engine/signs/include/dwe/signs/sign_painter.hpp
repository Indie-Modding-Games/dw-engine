#pragma once

#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/signs/sign_font.hpp>
#include <dwe/signs/sign_overlay.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Paints signs into a map's images: the old text cleared, the new one drawn with a font and its
// effects, and every pixel written back as an index of the image's palette, so the background's
// times of day still shade it. Only palette indices change: a sign never brings colours of its own
// into the texture (docs/modulos/engine/signs.md).
namespace dwe::signs {

// The pixels a sign changed in one image of the map: applying the patches of a map in order gives
// the painted images again without drawing anything.
struct SignPatch {
  SignTarget::Kind kind = SignTarget::Kind::Background;
  std::int32_t bitsPerPixel = 8;
  std::int32_t page = 0;
  PixelRect rect;
  std::vector<std::uint8_t> indices;
};

// What painting needs from the game: the text of a localization key and a font by name (nullptr
// when there is none).
struct SignResources {
  std::function<std::string(std::string_view key)> text;
  std::function<const SignFont*(std::string_view name)> font;
};

struct PaintedSigns {
  std::vector<SignPatch> patches;
  // Signs left out, and why (a page the map lacks, a missing font...).
  std::vector<std::string> warnings;
};

// Paints one sign with its text into an indexed image whose colours are `palette`; returns the
// rectangle it changed (empty when nothing). `original`, the image before any sign was painted
// (the same size), says which colours SignColors::Region allows; `image` itself when null. Used
// by paintSigns and the tests.
PixelRect paintSign(assets::IndexedImage& image, std::span<const assets::PaletteColor> palette,
                    const Sign& sign, std::string_view text, const SignFont& font,
                    const assets::IndexedImage* original = nullptr);

// Paints the signs into copies of the map's images and returns what changed, sign by sign. With
// the map's `objects`, a background sign is also painted into the sprites cut from the background
// over it (a counter in front of a banner, an awning over a sign), so the sprite drawn on top
// shows the new text too.
PaintedSigns paintSigns(const assets::MapImages& images, std::span<const Sign> signs,
                        const SignResources& resources,
                        const assets::MapObjects* objects = nullptr);

// Writes patches into the images and, if a texture page changed, builds the sprites' textures
// again from the pages (`sprites`: the map's MapSprite list).
void applySignPatches(assets::MapImages& images, std::span<const SignPatch> patches,
                      std::span<const assets::MapSprite> sprites);

// The page a sign target names, or nullptr.
assets::MapTexturePage* findSignPage(std::span<assets::MapTexturePage> pages,
                                     std::int32_t bitsPerPixel, std::int32_t page);
const assets::MapTexturePage* findSignPage(std::span<const assets::MapTexturePage> pages,
                                           std::int32_t bitsPerPixel, std::int32_t page);

} // namespace dwe::signs
