#include <dwe/assets/file_system.hpp>
#include <dwe/assets/image_palettes.hpp>
#include <dwe/assets/image_replacement.hpp>
#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/texture.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/ui/image_reference.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::ui {
namespace {

std::string_view textOf(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// The path without the extension of its last part.
std::string_view withoutExtension(std::string_view path) {
  const std::size_t slash = path.rfind('/');
  const std::size_t dot = path.rfind('.');
  if (dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash)) {
    return path.substr(0, dot);
  }
  return path;
}

core::Result<std::size_t> parsePalette(std::string_view text) {
  std::size_t palette = 0;
  const auto [last, error] = std::from_chars(text.data(), text.data() + text.size(), palette);
  if (text.empty() || error != std::errc{} || last != text.data() + text.size()) {
    return core::fail(core::formatMessage(messages::BadPaletteNumber, text));
  }
  return palette;
}

// What the options after an image's path ask for.
struct ImageOptions {
  std::optional<std::size_t> palette;
  ImageTone tone = ImageTone::Natural;
  bool smooth = false;
  std::uint32_t mirrorLeft = 0;
  std::uint32_t mirrorRight = 0;
};

// A count of mirrored columns: a whole number from 0 to MaxMirroredColumns.
core::Result<std::uint32_t> parseMirroredColumns(std::string_view text) {
  std::uint32_t columns = 0;
  const auto [last, error] = std::from_chars(text.data(), text.data() + text.size(), columns);
  if (text.empty() || error != std::errc{} || last != text.data() + text.size() ||
      columns > MaxMirroredColumns) {
    return core::fail(core::formatMessage(messages::BadMirroredColumns, text, MaxMirroredColumns));
  }
  return columns;
}

// Reads the options after an image's path:
// `palette=3&tone=grey&upscale=smooth&mirror_left=100&mirror_right=26`.
core::Result<ImageOptions> parseImageOptions(std::string_view options) {
  ImageOptions parsed;
  while (true) {
    const std::size_t end = options.find('&');
    const std::string_view option = options.substr(0, end);
    const std::size_t equals = option.find('=');
    const std::string_view name = option.substr(0, equals);
    const std::string_view value =
        equals == std::string_view::npos ? std::string_view{} : option.substr(equals + 1);
    if (equals != std::string_view::npos && name == PaletteOption) {
      auto number = parsePalette(value);
      if (!number) {
        return std::unexpected{number.error()};
      }
      parsed.palette = *number;
    } else if (equals != std::string_view::npos && name == ToneOption && value == GreyToneName) {
      parsed.tone = ImageTone::Grey;
    } else if (equals != std::string_view::npos && name == UpscaleOption &&
               value == SmoothUpscaleName) {
      parsed.smooth = true;
    } else if (equals != std::string_view::npos &&
               (name == MirrorLeftOption || name == MirrorRightOption)) {
      auto columns = parseMirroredColumns(value);
      if (!columns) {
        return std::unexpected{columns.error()};
      }
      (name == MirrorLeftOption ? parsed.mirrorLeft : parsed.mirrorRight) = *columns;
    } else {
      return core::fail(core::formatMessage(messages::BadImageOption, option));
    }
    if (end == std::string_view::npos) {
      return parsed;
    }
    options.remove_prefix(end + 1);
  }
}

core::Result<assets::Image> decodeKtx2Image(std::span<const std::byte> bytes) {
  auto texture = assets::decodeKtx2(bytes, assets::TexelEncoding::Rgba8);
  if (!texture) {
    return std::unexpected{texture.error()};
  }
  return assets::Image{.width = texture->width,
                       .height = texture->height,
                       .pixels = std::move(texture->levels.front())};
}

// One palette of an indexed PNG, read from the image's metadata.
core::Result<assets::Image> applyMetaPalette(const assets::VirtualFileSystem& files,
                                             const ImageReference& reference,
                                             std::span<const std::byte> png) {
  auto indexed = assets::decodeIndexedPng(png);
  if (!indexed) {
    return std::unexpected{indexed.error()};
  }
  auto metaId =
      assets::AssetId::parse(reference.asset.str() + std::string{assets::ImageMetaSuffix});
  if (!metaId) {
    return std::unexpected{metaId.error()};
  }
  auto meta = files.read(*metaId);
  if (!meta) {
    return core::fail(core::formatMessage(messages::NoImageMeta, reference.asset.str()));
  }
  auto palettes = assets::parseImagePalettes(textOf(*meta));
  if (!palettes) {
    return std::unexpected{palettes.error()};
  }
  const std::size_t index = reference.palette.value_or(0);
  if (index >= palettes->size()) {
    return core::fail(core::formatMessage(messages::PaletteOutOfRange, reference.asset.str(), index,
                                          palettes->size()));
  }
  std::vector<assets::PaletteColor> colors;
  colors.reserve(palettes->at(index).size());
  for (const std::uint16_t color : palettes->at(index)) {
    colors.push_back(assets::imageColorFromOriginal(color));
  }
  return assets::applyPalette(*indexed, colors);
}

} // namespace

core::Result<ImageReference> parseImageReference(std::string_view path) {
  while (path.starts_with('/')) {
    path.remove_prefix(1);
  }
  ImageOptions options;
  if (const std::size_t query = path.find('?'); query != std::string_view::npos) {
    auto parsed = parseImageOptions(path.substr(query + 1));
    if (!parsed) {
      return std::unexpected{parsed.error()};
    }
    options = *parsed;
    path = path.substr(0, query);
  }
  auto asset = assets::AssetId::parse(withoutExtension(path));
  if (!asset) {
    return std::unexpected{asset.error()};
  }
  return ImageReference{.asset = std::move(*asset),
                        .palette = options.palette,
                        .tone = options.tone,
                        .smooth = options.smooth,
                        .mirrorLeft = options.mirrorLeft,
                        .mirrorRight = options.mirrorRight};
}

bool isPixelArt(const ImageReference& reference) {
  return reference.palette.has_value() && !reference.smooth;
}

std::uint32_t smoothDoublings(std::uint32_t width) {
  std::uint32_t doublings = 0;
  while (doublings < SmoothUpscaleMaxDoublings && width > 0 &&
         (width << doublings) < SmoothUpscaleWidth) {
    ++doublings;
  }
  return doublings;
}

std::string imageReferenceText(const ImageReference& reference) {
  std::string text = "/" + reference.asset.str();
  if (reference.palette) {
    text += std::string{PaletteQuery} + std::to_string(*reference.palette);
  }
  if (reference.tone == ImageTone::Grey) {
    text += reference.palette ? "&" : "?";
    text += std::string{ToneOption} + "=" + std::string{GreyToneName};
  }
  if (reference.smooth) {
    text += text.contains('?') ? "&" : "?";
    text += std::string{UpscaleOption} + "=" + std::string{SmoothUpscaleName};
  }
  const auto addMirror = [&text](std::string_view option, std::uint32_t columns) {
    if (columns > 0) {
      text += text.contains('?') ? "&" : "?";
      text += std::string{option} + "=" + std::to_string(columns);
    }
  };
  addMirror(MirrorLeftOption, reference.mirrorLeft);
  addMirror(MirrorRightOption, reference.mirrorRight);
  return text;
}

assets::Image withMirroredSides(const assets::Image& image, std::uint32_t left,
                                std::uint32_t right) {
  const std::size_t row = std::size_t{image.width} * BytesPerRgbaPixel;
  if (image.width == 0 || image.height == 0 || image.pixels.size() != row * image.height ||
      (left == 0 && right == 0)) {
    return image;
  }
  left = std::min(left, image.width);
  right = std::min(right, image.width);
  assets::Image wide{.width = image.width + left + right, .height = image.height, .pixels = {}};
  const std::size_t wideRow = std::size_t{wide.width} * BytesPerRgbaPixel;
  wide.pixels.resize(wideRow * wide.height);
  const auto copyPixel = [&](std::size_t from, std::size_t to) {
    std::copy_n(image.pixels.begin() + static_cast<std::ptrdiff_t>(from), BytesPerRgbaPixel,
                wide.pixels.begin() + static_cast<std::ptrdiff_t>(to));
  };
  for (std::uint32_t y = 0; y < image.height; ++y) {
    const std::size_t source = y * row;
    const std::size_t target = y * wideRow;
    std::copy_n(image.pixels.begin() + static_cast<std::ptrdiff_t>(source), row,
                wide.pixels.begin() +
                    static_cast<std::ptrdiff_t>(target + (left * BytesPerRgbaPixel)));
    for (std::uint32_t column = 0; column < left; ++column) {
      copyPixel(source + (std::size_t{column} * BytesPerRgbaPixel),
                target + (std::size_t{left - 1 - column} * BytesPerRgbaPixel));
    }
    for (std::uint32_t column = 0; column < right; ++column) {
      copyPixel(source + (std::size_t{image.width - 1 - column} * BytesPerRgbaPixel),
                target + (std::size_t{left + image.width + column} * BytesPerRgbaPixel));
    }
  }
  return wide;
}

namespace {

// An image's file: its bytes and its type, checked against what the reference asks of it.
struct ImageFile {
  std::vector<std::byte> bytes;
  std::string extension;
};

core::Result<ImageFile> readImageFile(const assets::VirtualFileSystem& files,
                                      const ImageReference& reference) {
  const assets::AssetSource* source = files.resolve(reference.asset);
  if (source == nullptr) {
    return core::fail(core::formatMessage(messages::NoImage, reference.asset.str()));
  }
  std::string extension = assets::pathText(source->file.extension());
  if (extension != PngExtension && extension != Ktx2Extension) {
    return core::fail(core::formatMessage(messages::NotAnImage, reference.asset.str(), extension));
  }
  // A remaster's KTX2 that overrides an indexed PNG has no palettes to pick from.
  if (reference.palette && extension != PngExtension) {
    return core::fail(
        core::formatMessage(messages::PaletteNeedsPng, reference.asset.str(), extension));
  }
  auto size = files.size(reference.asset);
  if (!size) {
    return std::unexpected{size.error()};
  }
  if (*size > MaxUiImageFileBytes) {
    return core::fail(core::formatMessage(messages::ImageFileTooLarge, reference.asset.str(), *size,
                                          MaxUiImageFileBytes));
  }
  auto bytes = files.read(reference.asset);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  return ImageFile{.bytes = std::move(*bytes), .extension = std::move(extension)};
}

// The size of an image from its file's header, and the checks that keep a forged header from making
// the UI decode, allocate or inflate more than a UI image needs. A PNG within MaxUiImageSide fits
// MaxUiImageBytes already.
core::Result<ImageSize> headerSize(const ImageReference& reference, const ImageFile& file) {
  ImageSize size;
  std::optional<assets::Ktx2Header> ktx2;
  if (file.extension == PngExtension) {
    auto header = assets::inspectPng(file.bytes);
    if (!header) {
      return std::unexpected{header.error()};
    }
    size = {.width = header->width, .height = header->height};
  } else {
    auto header = assets::inspectKtx2(file.bytes);
    if (!header) {
      return std::unexpected{header.error()};
    }
    size = {.width = header->width, .height = header->height};
    ktx2 = std::move(*header);
  }
  if (size.width > MaxUiImageSide || size.height > MaxUiImageSide) {
    return core::fail(core::formatMessage(messages::ImageTooLarge, reference.asset.str(),
                                          size.width, size.height, MaxUiImageSide));
  }
  if (ktx2) {
    auto usable = assets::checkPlain2dKtx2(*ktx2, MaxUiImageSide);
    if (usable) {
      usable = checkUiImageBudget(*ktx2);
    }
    if (!usable) {
      return core::fail(core::formatMessage(messages::ImageUnusable, reference.asset.str(),
                                            usable.error().message));
    }
  }
  return size;
}

// A player's image of an image, ready to decode: its reference, its file and the size it is drawn
// at, the original's.
struct Replacement {
  ImageReference reference;
  ImageFile file;
  ImageSize size;
};

// The size of the game's own image: the bottom of the stack, the pack that first gave it.
core::Result<ImageSize> originalSize(const assets::VirtualFileSystem& files,
                                     const assets::AssetId& image) {
  const std::span<const assets::AssetSource> sources = files.sources(image);
  if (sources.empty()) {
    return core::fail(core::formatMessage(messages::NoImage, image.str()));
  }
  auto bytes = files.read(sources.front());
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  if (auto png = assets::inspectPng(*bytes)) {
    return ImageSize{.width = png->width, .height = png->height};
  }
  auto ktx2 = assets::inspectKtx2(*bytes);
  if (!ktx2) {
    return std::unexpected{ktx2.error()};
  }
  return ImageSize{.width = ktx2->width, .height = ktx2->height};
}

std::optional<Replacement> findReplacement(const assets::VirtualFileSystem& files,
                                           const ImageReference& reference) {
  auto id = assets::paletteImageId(reference.asset, reference.palette.value_or(0));
  const assets::AssetSource* source = id ? files.resolve(*id) : nullptr;
  if (source == nullptr) {
    return std::nullopt;
  }
  ImageReference replacement{.asset = std::move(*id),
                             .palette = std::nullopt,
                             .tone = reference.tone,
                             .smooth = reference.smooth};
  const auto unusable = [&](const std::string& reason) {
    core::logWarning("{}",
                     core::formatMessage(messages::ReplacementUnusable, replacement.asset.str(),
                                         source->packId, reference.asset.str(), reason));
    return std::nullopt;
  };
  auto original = originalSize(files, reference.asset);
  if (!original) {
    return unusable(original.error().message);
  }
  auto file = readImageFile(files, replacement);
  auto size = file ? headerSize(replacement, *file)
                   : core::Result<ImageSize>{std::unexpected{file.error()}};
  if (!size) {
    return unusable(size.error().message);
  }
  if (!assets::replacementScale(original->width, original->height, size->width, size->height)) {
    core::logWarning("{}", core::formatMessage(messages::ReplacementWrongSize,
                                               replacement.asset.str(), source->packId, size->width,
                                               size->height, original->width, original->height,
                                               assets::MaxReplacementScale, reference.asset.str()));
    return std::nullopt;
  }
  return Replacement{
      .reference = std::move(replacement), .file = std::move(*file), .size = *original};
}

// How many of an image's pixels one pixel of the game's own image covers across (1 when the game's
// is the one drawn, or when it cannot be read).
std::uint32_t pixelsPerOriginalPixel(const assets::VirtualFileSystem& files,
                                     const assets::AssetId& image, std::uint32_t width) {
  auto original = originalSize(files, image);
  if (!original || original->width == 0 || width < original->width) {
    return 1;
  }
  return width / original->width;
}

// The size of an image as a reference draws it: wider by its mirrored sides.
ImageSize withMirroredWidth(const assets::VirtualFileSystem& files, const ImageReference& reference,
                            ImageSize size) {
  if (reference.mirrorLeft == 0 && reference.mirrorRight == 0) {
    return size;
  }
  const std::uint32_t scale = pixelsPerOriginalPixel(files, reference.asset, size.width);
  size.width += std::min(reference.mirrorLeft * scale, size.width) +
                std::min(reference.mirrorRight * scale, size.width);
  return size;
}

} // namespace

core::Result<void> checkUiImageBudget(const assets::Ktx2Header& header) {
  // Every level is transcoded, even though the UI keeps only the first.
  const std::uint64_t bytes = rgbaBytesOverLevels(header.width, header.height, header.levels);
  if (bytes > MaxUiImageBytes) {
    return core::fail(
        core::formatMessage(messages::UiImageOverBudget, bytes, header.levels, MaxUiImageBytes));
  }
  return {};
}

core::Result<ImageSize> imageSize(const assets::VirtualFileSystem& files,
                                  const ImageReference& reference) {
  if (std::optional<Replacement> replacement = findReplacement(files, reference)) {
    return withMirroredWidth(files, reference, replacement->size);
  }
  auto file = readImageFile(files, reference);
  if (!file) {
    return std::unexpected{file.error()};
  }
  auto size = headerSize(reference, *file);
  if (!size) {
    return std::unexpected{size.error()};
  }
  return withMirroredWidth(files, reference, *size);
}

void applyImageTone(assets::Image& image, ImageTone tone) {
  if (tone == ImageTone::Natural) {
    return;
  }
  for (std::size_t pixel = 0; pixel + 3 < image.pixels.size(); pixel += BytesPerRgbaPixel) {
    const auto grey = std::byte{greyToneOf(std::to_integer<std::uint8_t>(image.pixels[pixel]),
                                           std::to_integer<std::uint8_t>(image.pixels[pixel + 1]),
                                           std::to_integer<std::uint8_t>(image.pixels[pixel + 2]))};
    image.pixels[pixel] = grey;
    image.pixels[pixel + 1] = grey;
    image.pixels[pixel + 2] = grey;
  }
}

namespace {

core::Result<assets::Image> decodeImage(const assets::VirtualFileSystem& files,
                                        const ImageReference& reference, const ImageFile& file) {
  if (reference.palette) {
    return applyMetaPalette(files, reference, file.bytes);
  }
  if (file.extension == Ktx2Extension) {
    return decodeKtx2Image(file.bytes);
  }
  return assets::decodePng(file.bytes);
}

// The pixels and their drawn size as a reference draws them: wider by the mirrored sides it asks
// for, as many of the image's pixels as the game's own image's pixels it names, and its drawn size
// as many more of its own.
DrawnImage withMirroredSidesIfAsked(const assets::VirtualFileSystem& files,
                                    const ImageReference& reference, assets::Image pixels,
                                    ImageSize size) {
  if (reference.mirrorLeft == 0 && reference.mirrorRight == 0) {
    return DrawnImage{.pixels = std::move(pixels), .size = size};
  }
  const std::uint32_t drawnScale = pixelsPerOriginalPixel(files, reference.asset, size.width);
  const std::uint32_t texels = size.width > 0 ? std::max(1U, pixels.width / size.width) : 1U;
  const std::uint32_t left = std::min(reference.mirrorLeft * drawnScale, size.width);
  const std::uint32_t right = std::min(reference.mirrorRight * drawnScale, size.width);
  return DrawnImage{.pixels = withMirroredSides(pixels, left * texels, right * texels),
                    .size = {.width = size.width + left + right, .height = size.height}};
}

} // namespace

core::Result<DrawnImage> loadDrawnImage(const assets::VirtualFileSystem& files,
                                        const ImageReference& reference) {
  if (std::optional<Replacement> replacement = findReplacement(files, reference)) {
    auto image = decodeImage(files, replacement->reference, replacement->file);
    if (image) {
      applyImageTone(*image, reference.tone);
      return withMirroredSidesIfAsked(files, reference, std::move(*image), replacement->size);
    }
    core::logWarning("{}", core::formatMessage(messages::ReplacementUnusable,
                                               replacement->reference.asset.str(),
                                               files.resolve(replacement->reference.asset)->packId,
                                               reference.asset.str(), image.error().message));
  }
  auto file = readImageFile(files, reference);
  if (!file) {
    return std::unexpected{file.error()};
  }
  // The header first, so an image too large for the UI is refused before its pixels are decoded.
  if (auto size = headerSize(reference, *file); !size) {
    return std::unexpected{size.error()};
  }
  auto image = decodeImage(files, reference, *file);
  if (!image) {
    return std::unexpected{image.error()};
  }
  applyImageTone(*image, reference.tone);
  const ImageSize size{.width = image->width, .height = image->height};
  return withMirroredSidesIfAsked(files, reference, std::move(*image), size);
}

core::Result<assets::Image> loadImage(const assets::VirtualFileSystem& files,
                                      const ImageReference& reference) {
  auto drawn = loadDrawnImage(files, reference);
  if (!drawn) {
    return std::unexpected{drawn.error()};
  }
  return std::move(drawn->pixels);
}

} // namespace dwe::ui
