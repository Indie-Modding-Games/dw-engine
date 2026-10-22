#include <dwe/assets/file_system.hpp>
#include <dwe/assets/image_palettes.hpp>
#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/texture.hpp>
#include <dwe/core/message.hpp>
#include <dwe/ui/image_reference.hpp>

#include <charconv>
#include <cstdint>
#include <optional>
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

core::Result<assets::Image> decodeKtx2Image(std::span<const std::byte> bytes) {
    auto texture = assets::decodeKtx2(bytes, assets::TexelEncoding::Rgba8);
    if (!texture) {
        return std::unexpected{texture.error()};
    }
    return assets::Image{
        .width = texture->width, .height = texture->height, .pixels = std::move(texture->levels.front())};
}

// One palette of an indexed PNG, read from the image's metadata.
core::Result<assets::Image> applyMetaPalette(const assets::VirtualFileSystem& files, const ImageReference& reference,
                                             std::span<const std::byte> png) {
    auto indexed = assets::decodeIndexedPng(png);
    if (!indexed) {
        return std::unexpected{indexed.error()};
    }
    auto metaId = assets::AssetId::parse(reference.asset.str() + std::string{assets::ImageMetaSuffix});
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
        return core::fail(
            core::formatMessage(messages::PaletteOutOfRange, reference.asset.str(), index, palettes->size()));
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
    std::optional<std::size_t> palette;
    if (const std::size_t query = path.find(PaletteQuery); query != std::string_view::npos) {
        auto number = parsePalette(path.substr(query + PaletteQuery.size()));
        if (!number) {
            return std::unexpected{number.error()};
        }
        palette = *number;
        path = path.substr(0, query);
    }
    auto asset = assets::AssetId::parse(withoutExtension(path));
    if (!asset) {
        return std::unexpected{asset.error()};
    }
    return ImageReference{.asset = std::move(*asset), .palette = palette};
}

bool isPixelArt(const ImageReference& reference) {
    return reference.palette.has_value();
}

std::string imageReferenceText(const ImageReference& reference) {
    std::string text = "/" + reference.asset.str();
    if (reference.palette) {
        text += std::string{PaletteQuery} + std::to_string(*reference.palette);
    }
    return text;
}

namespace {

// An image's file: its bytes and its type, checked against what the reference asks of it.
struct ImageFile {
    std::vector<std::byte> bytes;
    std::string extension;
};

core::Result<ImageFile> readImageFile(const assets::VirtualFileSystem& files, const ImageReference& reference) {
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
        return core::fail(core::formatMessage(messages::PaletteNeedsPng, reference.asset.str(), extension));
    }
    auto size = files.size(reference.asset);
    if (!size) {
        return std::unexpected{size.error()};
    }
    if (*size > MaxUiImageFileBytes) {
        return core::fail(
            core::formatMessage(messages::ImageFileTooLarge, reference.asset.str(), *size, MaxUiImageFileBytes));
    }
    auto bytes = files.read(reference.asset);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    return ImageFile{.bytes = std::move(*bytes), .extension = std::move(extension)};
}

// The size of an image from its file's header, and the checks that keep a forged header from making the UI decode,
// allocate or inflate more than a UI image needs. A PNG within MaxUiImageSide fits MaxUiImageBytes already.
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
        return core::fail(core::formatMessage(messages::ImageTooLarge, reference.asset.str(), size.width, size.height,
                                              MaxUiImageSide));
    }
    if (ktx2) {
        auto usable = assets::checkPlain2dKtx2(*ktx2, MaxUiImageSide);
        if (usable) {
            usable = checkUiImageBudget(*ktx2);
        }
        if (!usable) {
            return core::fail(
                core::formatMessage(messages::ImageUnusable, reference.asset.str(), usable.error().message));
        }
    }
    return size;
}

} // namespace

core::Result<void> checkUiImageBudget(const assets::Ktx2Header& header) {
    // Every level is transcoded, even though the UI keeps only the first.
    const std::uint64_t bytes = rgbaBytesOverLevels(header.width, header.height, header.levels);
    if (bytes > MaxUiImageBytes) {
        return core::fail(core::formatMessage(messages::UiImageOverBudget, bytes, header.levels, MaxUiImageBytes));
    }
    return {};
}

core::Result<ImageSize> imageSize(const assets::VirtualFileSystem& files, const ImageReference& reference) {
    auto file = readImageFile(files, reference);
    if (!file) {
        return std::unexpected{file.error()};
    }
    return headerSize(reference, *file);
}

core::Result<assets::Image> loadImage(const assets::VirtualFileSystem& files, const ImageReference& reference) {
    auto file = readImageFile(files, reference);
    if (!file) {
        return std::unexpected{file.error()};
    }
    // The header first, so an image too large for the UI is refused before its pixels are decoded.
    if (auto size = headerSize(reference, *file); !size) {
        return std::unexpected{size.error()};
    }
    if (reference.palette) {
        return applyMetaPalette(files, reference, file->bytes);
    }
    if (file->extension == Ktx2Extension) {
        return decodeKtx2Image(file->bytes);
    }
    return assets::decodePng(file->bytes);
}

} // namespace dwe::ui
