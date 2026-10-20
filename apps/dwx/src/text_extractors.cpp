#include "text_extractors.hpp"

#include <dwe/assets/text_table.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/exe/memory_image.hpp>
#include <dwe/formats/image/png.hpp>
#include <dwe/formats/image/tim.hpp>
#include <dwe/formats/text/font.hpp>
#include <dwe/formats/text/game_text.hpp>
#include <dwe/formats/text/string_manifest.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "asset_names.hpp"

namespace dwe::dwx {

namespace {

using core::YamlNode;
using formats::exe::MemoryImage;
using formats::text::StringBlock;
using formats::text::StringManifest;

constexpr std::string_view MainSource = "main";
constexpr std::string_view OverlaySuffix = "_REL.BIN";
// The columns of the font atlas: 16 cells of 12 pixels make it 192 pixels wide.
constexpr std::uint32_t AtlasColumns = 16;

core::Result<OutputFile> encodedOutput(const core::Result<assets::AssetId>& id, std::string extension,
                                       std::span<const std::byte> bytes) {
    if (!id) {
        return std::unexpected{id.error()};
    }
    return OutputFile{
        .id = *id, .extension = std::move(extension), .bytes = std::vector<std::byte>(bytes.begin(), bytes.end())};
}

core::Result<OutputFile> yamlOutput(const core::Result<assets::AssetId>& id, const YamlNode& node) {
    const auto text = core::emitYaml(node);
    if (!text) {
        return std::unexpected{text.error()};
    }
    return encodedOutput(id, "yaml", std::as_bytes(std::span{*text}));
}

// The id segment of a source: "main", or the overlay's name without _REL.BIN ("btl").
std::string sourceName(std::string_view file) {
    if (file.empty()) {
        return std::string{MainSource};
    }
    return names::segment(file.ends_with(OverlaySuffix) ? file.substr(0, file.size() - OverlaySuffix.size()) : file);
}

// Reads the strings of the blocks of one source into a flat table {"<block>.<slot>": string}
// (docs/formats/textos.md). An empty slot has no key. A block that does not read is an unknown and is left out.
YamlNode readSource(std::string_view source, const StringManifest& manifest, const MemoryImage& image,
                    const MemoryImage& executable, Extraction& extraction, std::size_t& strings) {
    YamlNode table = YamlNode::mapping();
    for (const StringBlock& block : manifest.blocks) {
        if (block.source != source) {
            continue;
        }
        const auto read = formats::text::readStringBlock(block, image, executable);
        if (!read) {
            extraction.unknowns.push_back(std::format("text {}: {}", sourceName(source), read.error().message));
            continue;
        }
        for (std::size_t slot = 0; slot < read->size(); ++slot) {
            const auto& raw = (*read)[slot];
            if (!raw) {
                continue;
            }
            table.set(assets::gameTextEntryKey(block.name, slot),
                      YamlNode::scalar(formats::text::decodeByteString(std::as_bytes(std::span{*raw})),
                                       YamlNode::ScalarStyle::Quoted));
            ++strings;
        }
    }
    return table;
}

core::Result<void> addTextTable(Extraction& extraction, const StringManifest& manifest, std::string_view source,
                                const MemoryImage& image, const MemoryImage& executable, std::size_t& strings) {
    const YamlNode root = readSource(source, manifest, image, executable, extraction, strings);
    auto output = yamlOutput(names::textTable(manifest.language, sourceName(source)), root);
    if (!output) {
        return std::unexpected{output.error()};
    }
    extraction.outputs.push_back(std::move(*output));
    return {};
}

core::Result<void> addFont(const Disc& disc, const MemoryImage& executable, Extraction& extraction) {
    const auto location = formats::text::findFontLocation(disc.info().version);
    if (!location) {
        // Only the versions the tools know have a located font; the others are not this extractor's scope yet.
        extraction.notes.push_back(std::format("the font of version '{}' is not located", disc.info().version));
        return {};
    }
    const auto codes = executable.bytesAt(location->charToGlyphTable, std::size_t{location->glyphCount} * 2);
    const auto glyphs = executable.bytesAt(location->glyphData, std::size_t{location->glyphCount} * 24);
    const auto clut = executable.bytesAt(location->clut, formats::text::FontClutSize * 2);
    if (!codes || !glyphs || !clut) {
        extraction.unknowns.emplace_back("the font's tables are outside the executable");
        return {};
    }
    const auto font = formats::text::readFont(*codes, *glyphs, *clut);
    if (!font) {
        extraction.unknowns.push_back(std::format("font: {}", font.error().message));
        return {};
    }
    const auto atlas = formats::text::buildFontAtlas(*font, AtlasColumns);
    std::vector<formats::image::Rgba8> palette;
    std::ranges::transform(font->clut, std::back_inserter(palette), formats::image::toRgba8);
    const auto png = formats::image::encodePngIndexed(atlas.width, atlas.height, atlas.indices, palette);
    if (!png) {
        return std::unexpected{png.error()};
    }
    const auto id = names::fontImage();
    auto image = encodedOutput(id, "png", *png);
    if (!image) {
        return std::unexpected{image.error()};
    }
    auto metadata = yamlOutput(names::metadataOf(*id), formats::text::fontToYaml(*font, atlas));
    if (!metadata) {
        return std::unexpected{metadata.error()};
    }
    extraction.outputs.push_back(std::move(*image));
    extraction.outputs.push_back(std::move(*metadata));
    extraction.notes.push_back(std::format("font: {} glyphs of {}x{} pixels", font->glyphs.size(),
                                           formats::text::GlyphColumns, formats::text::GlyphRows));
    return {};
}

} // namespace

core::Result<void> addExecutableText(const Disc& disc, const formats::disc::IsoFile& executable,
                                     Extraction& extraction) {
    const auto bytes = formats::disc::readIsoFile(disc.source(), executable);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    const auto image = MemoryImage::fromExecutable(*bytes);
    if (!image) {
        return std::unexpected{image.error()};
    }
    if (auto font = addFont(disc, *image, extraction); !font) {
        return font;
    }
    const auto manifest = formats::text::findStringManifest(disc.info().version);
    if (!manifest) {
        extraction.notes.push_back(
            std::format("the strings of version '{}' are not located; no text table is made", disc.info().version));
        return {};
    }
    std::size_t strings = 0;
    if (auto table = addTextTable(extraction, *manifest, "", *image, *image, strings); !table) {
        return table;
    }
    for (const auto& overlay : manifest->overlays) {
        const formats::disc::IsoFile* file = disc.findFile(overlay.file);
        if (file == nullptr) {
            extraction.unknowns.push_back(std::format("text: {} is not on the disc", overlay.file));
            continue;
        }
        const auto overlayBytes = formats::disc::readIsoFile(disc.source(), *file);
        if (!overlayBytes) {
            return std::unexpected{overlayBytes.error()};
        }
        const auto overlayImage = MemoryImage::fromOverlay(*overlayBytes, overlay.loadAddress);
        if (auto table = addTextTable(extraction, *manifest, overlay.file, overlayImage, *image, strings); !table) {
            return table;
        }
    }
    extraction.notes.push_back(
        std::format("text: {} strings of the executable and {} overlays", strings, manifest->overlays.size()));
    for (const std::string_view missing : manifest->missing) {
        extraction.notes.push_back(std::format("text: the decomp names strings without a known address: {}", missing));
    }
    return {};
}

} // namespace dwe::dwx
