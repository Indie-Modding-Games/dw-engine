#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/image.hpp>
#include <dwe/assets/texture.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::ui {

// What follows an image's path to pick one palette of an indexed image:
// `texture/stddat/time.png?palette=3`.
inline constexpr std::string_view PaletteQuery = "?palette=";

// The file types an image may come from: an extracted or edited PNG, or a texture `dwx cook` or a
// remaster made.
inline constexpr std::string_view PngExtension = ".png";
inline constexpr std::string_view Ktx2Extension = ".ktx2";

// The longest side of an image the UI reads (assets::MaxImageSide is the GPU's limit): a UI image
// is a sprite sheet or a frame, and every switch of skin reads them again.
inline constexpr std::uint32_t MaxUiImageSide = 4096;
// What a pixel takes once decoded: 8-bit RGBA.
inline constexpr std::uint64_t BytesPerRgbaPixel = 4;
// What decoding an image of this size to RGBA takes, summed over its mip levels. Every level past
// the chain's 1x1 one also measures 1x1, so those are counted at once: the loop runs at most 32
// times, whatever the level count.
constexpr std::uint64_t rgbaBytesOverLevels(std::uint32_t width, std::uint32_t height,
                                            std::uint32_t levels) {
  std::uint64_t total = 0;
  for (std::uint32_t level = 0; level < levels; ++level) {
    const bool past = level >= std::numeric_limits<std::uint32_t>::digits;
    const std::uint32_t levelWidth = past ? 1U : std::max(width >> level, 1U);
    const std::uint32_t levelHeight = past ? 1U : std::max(height >> level, 1U);
    if (levelWidth == 1 && levelHeight == 1) {
      return total + (std::uint64_t{levels - level} * BytesPerRgbaPixel);
    }
    total += std::uint64_t{levelWidth} * levelHeight * BytesPerRgbaPixel;
  }
  return total;
}

// The most a UI image may take once decoded to RGBA, summed over its mip levels: a MaxUiImageSide
// square with its full mip chain (13 levels, about 85.3 MiB), as dwx cook writes it.
inline constexpr std::uint64_t MaxUiImageBytes =
    rgbaBytesOverLevels(MaxUiImageSide, MaxUiImageSide, std::bit_width(MaxUiImageSide));
// The largest file a UI image may come from, checked before the file is read.
inline constexpr std::uint64_t MaxUiImageFileBytes =
    std::uint64_t{MaxUiImageSide} * MaxUiImageSide * BytesPerRgbaPixel;

// An image a document or a style sheet names. The extension in its path is ignored: the image is
// the asset, and whichever file provides it is read, so a remaster's `.ktx2` that overrides a
// `.png` keeps every `.png` reference working. A palette other than the one the file holds picks a
// row of the image's metadata (`<id>_meta.yaml`).
struct ImageReference {
  assets::AssetId asset;
  std::optional<std::size_t> palette;
};

// Reads a reference as RmlUi passes it, already joined to the document's folder: `ui/x/frame.png`,
// `/texture/stddat/time?palette=3`.
core::Result<ImageReference> parseImageReference(std::string_view path);

// Whether the UI draws an image as the disc's pixel art, with sharp texel edges at any size
// (rml.slang: sharpUv): an indexed image drawn with one of its palettes, as the original look's
// sprites are. Any other image is filtered.
bool isPixelArt(const ImageReference& reference);

// The text of a reference, as a style sheet writes it: `/texture/stddat/time?palette=3`.
std::string imageReferenceText(const ImageReference& reference);

// The size of an image in pixels, from the header of its file only.
struct ImageSize {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

// Checks that decoding a KTX2 texture to RGBA, every level of it, takes no more than
// MaxUiImageBytes. Fails with the reason, without the image's name.
core::Result<void> checkUiImageBudget(const assets::Ktx2Header& header);

// Reads an image's size from its file's header, without decoding its pixels. Fails for a file
// larger than MaxUiImageFileBytes, an image larger than MaxUiImageSide on a side or than
// MaxUiImageBytes decoded, a KTX2 that is not a plain 2D Basis Universal texture
// (assets::checkPlain2dKtx2), and a palette asked of a file that is not a PNG. loadImage runs the
// same checks before it decodes anything.
core::Result<ImageSize> imageSize(const assets::VirtualFileSystem& files,
                                  const ImageReference& reference);

// Loads the pixels of an image: a PNG of any kind or a KTX2 texture. A palette needs an indexed PNG
// and its metadata with at least that many palettes.
core::Result<assets::Image> loadImage(const assets::VirtualFileSystem& files,
                                      const ImageReference& reference);

} // namespace dwe::ui
