#include "image_extractors.hpp"

#include <dwe/core/yaml.hpp>
#include <dwe/formats/image/png.hpp>
#include <dwe/formats/image/tim.hpp>
#include <dwe/formats/image/tim_packs.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "asset_names.hpp"

namespace dwe::dwx {

namespace {

using core::YamlNode;
using formats::image::Tim;
using formats::image::TimPixelMode;

// The files the original reads with utils2.c: loadStackedTIMFile (main.c, vs_demo.c, vs_select.c).
constexpr std::array StackedTimFiles = {std::string_view{"ETCDAT/ETCTIM.BIN"}, std::string_view{"STDDAT/STDTIM.BIN"}};

std::string placeName(const TimPlace& place) {
    return place.entry ? std::format("image {} at 0x{:x}", *place.entry, place.offset)
                       : std::format("image at 0x{:x}", place.offset);
}

// A pack path in the format tables uses backslashes; the volume uses slashes.
std::string volumePathOf(std::string_view packPath) {
    std::string path{packPath};
    std::ranges::replace(path, '\\', '/');
    return path;
}

const formats::image::KnownPack* findPack(std::string_view path) {
    const auto packs = formats::image::knownPacks();
    const auto found =
        std::ranges::find_if(packs, [path](const auto& pack) { return volumePathOf(pack.path) == path; });
    return found == packs.end() ? nullptr : &*found;
}

std::string_view modeName(TimPixelMode mode) {
    switch (mode) {
    case TimPixelMode::Indexed4:
        return "indexed4";
    case TimPixelMode::Indexed8:
        return "indexed8";
    case TimPixelMode::Direct16:
        return "direct16";
    case TimPixelMode::Direct24:
        return "direct24";
    }
    return "unknown";
}

YamlNode number(std::size_t value) {
    return YamlNode::scalar(std::to_string(value));
}

YamlNode rectNode(const formats::image::VramRect& rect) {
    YamlNode node = YamlNode::mapping();
    node.set("x", number(rect.x));
    node.set("y", number(rect.y));
    node.set("width", number(rect.width));
    node.set("height", number(rect.height));
    return node;
}

// A palette as one line of 4-digit hex words, the raw console colours with their STP bit.
std::string paletteText(const std::vector<formats::image::Color15>& colors) {
    std::string text;
    for (const auto color : colors) {
        text += std::format("{}{:04x}", text.empty() ? "" : " ", color);
    }
    return text;
}

// The PNG of a TIM: indexed with palette 0 when it has a CLUT, so the indices and the palette survive; RGBA for
// direct colours. Fails when the pixels cannot be written, with the reason.
core::Result<std::pair<std::vector<std::byte>, std::string_view>> encodeTim(const Tim& tim) {
    if (tim.mode == TimPixelMode::Direct16 || tim.mode == TimPixelMode::Direct24) {
        auto image = formats::image::decodeTim(tim);
        if (!image) {
            return std::unexpected{image.error()};
        }
        auto png = formats::image::encodePngRgba(*image);
        if (!png) {
            return std::unexpected{png.error()};
        }
        return std::pair{std::move(*png), std::string_view{"rgba"}};
    }
    if (formats::image::timPaletteCount(tim) == 0) {
        return core::fail("an indexed image without a CLUT; its pixels are not written");
    }
    auto indices = formats::image::timIndices(tim);
    auto palette = formats::image::timPalette(tim, 0);
    if (!indices || !palette) {
        return std::unexpected{!indices ? indices.error() : palette.error()};
    }
    std::vector<formats::image::Rgba8> colors;
    std::ranges::transform(*palette, std::back_inserter(colors), formats::image::toRgba8);
    auto png = formats::image::encodePngIndexed(formats::image::timWidth(tim), tim.imageRect.height, *indices, colors);
    if (!png) {
        return std::unexpected{png.error()};
    }
    return std::pair{std::move(*png), std::string_view{"indexed"}};
}

} // namespace

core::Result<void> addTimImage(Extraction& extraction, const assets::AssetId& id, const Tim& tim, const TimPlace& place,
                               std::vector<std::string> warnings) {
    YamlNode meta = YamlNode::mapping();
    meta.set("source", YamlNode::scalar(std::string{place.source}, YamlNode::ScalarStyle::Quoted));
    if (place.entry) {
        meta.set("entry", number(*place.entry));
    }
    meta.set("offset", number(place.offset));
    meta.set("mode", YamlNode::scalar(std::string{modeName(tim.mode)}));
    meta.set("width", number(formats::image::timWidth(tim)));
    meta.set("height", number(tim.imageRect.height));
    meta.set("image_rect", rectNode(tim.imageRect));
    if (tim.hasClut) {
        meta.set("clut_rect", rectNode(tim.clutRect));
    }

    auto png = encodeTim(tim);
    if (png) {
        meta.set("png", YamlNode::scalar(std::string{png->second}));
        if (png->second == "indexed") {
            meta.set("png_palette", number(0));
        }
        extraction.outputs.push_back(OutputFile{.id = id, .extension = "png", .bytes = std::move(png->first)});
    } else {
        warnings.push_back(std::format("not written: {}", png.error().message));
    }

    const std::size_t paletteCount = formats::image::timPaletteCount(tim);
    if (paletteCount > 0) {
        YamlNode palettes = YamlNode::sequence();
        for (std::size_t i = 0; i < paletteCount; ++i) {
            auto palette = formats::image::timPalette(tim, i);
            if (!palette) {
                return std::unexpected{palette.error()};
            }
            palettes.append(YamlNode::scalar(paletteText(*palette), YamlNode::ScalarStyle::Quoted));
        }
        meta.set("palettes", std::move(palettes));
    }
    if (!warnings.empty()) {
        YamlNode list = YamlNode::sequence();
        for (const auto& warning : warnings) {
            list.append(YamlNode::scalar(warning, YamlNode::ScalarStyle::Quoted));
            extraction.unknowns.push_back(std::format("{}: {}", placeName(place), warning));
        }
        meta.set("warnings", std::move(list));
    }

    const auto metaId = names::metadataOf(id);
    if (!metaId) {
        return std::unexpected{metaId.error()};
    }
    auto text = core::emitYaml(meta);
    if (!text) {
        return std::unexpected{text.error()};
    }
    const auto bytes = std::as_bytes(std::span{*text});
    extraction.outputs.push_back(
        OutputFile{.id = *metaId, .extension = "yaml", .bytes = std::vector<std::byte>(bytes.begin(), bytes.end())});
    return {};
}

namespace {

// Reports the bytes from start to end that nothing explained, unless they are all zero (padding).
void reportLeftover(Extraction& extraction, std::span<const std::byte> bytes, std::size_t start, std::size_t end,
                    std::string_view what) {
    const auto rest = bytes.subspan(start, end - start);
    const auto nonZero = std::ranges::count_if(rest, [](std::byte value) { return value != std::byte{0}; });
    if (nonZero > 0) {
        extraction.unknowns.push_back(
            std::format("bytes 0x{:x}-0x{:x} {}: {} of them are not zero", start, end, what, nonZero));
    }
}

// Extracts a list of TIMs, with a warning for each one whose distance to the next is not what its block lengths
// say.
core::Result<Extraction> extractTimList(const formats::image::StackedTims& list, std::span<const std::byte> bytes,
                                        const formats::disc::IsoFile& file) {
    Extraction extraction;
    if (list.entries.empty()) {
        return core::fail("the file does not start with a TIM");
    }
    const bool several = list.entries.size() > 1;
    for (std::size_t i = 0; i < list.entries.size(); ++i) {
        const auto& [offset, tim] = list.entries[i];
        const std::size_t next = i + 1 < list.entries.size() ? list.entries[i + 1].offset : list.end;
        std::vector<std::string> warnings;
        if (next - offset != tim.byteSize) {
            warnings.push_back(std::format("the original moves {} bytes to the next image; the block lengths say {}",
                                           next - offset, tim.byteSize));
        }
        const auto id = names::discImage(file.path, several ? std::optional{i} : std::nullopt);
        if (!id) {
            return std::unexpected{id.error()};
        }
        const TimPlace place{.source = file.path, .entry = several ? std::optional{i} : std::nullopt, .offset = offset};
        if (auto added = addTimImage(extraction, *id, tim, place, std::move(warnings)); !added) {
            return std::unexpected{added.error()};
        }
    }
    reportLeftover(extraction, bytes, list.end, bytes.size(), "after the last image");
    return extraction;
}

} // namespace

bool handlesTimPack(std::string_view path) {
    return findPack(path) != nullptr;
}

core::Result<Extraction> extractTimPack(const Disc& disc, const formats::disc::IsoFile& file) {
    const formats::image::KnownPack* pack = findPack(file.path);
    if (pack == nullptr) {
        return core::fail("not a known pack");
    }
    const auto bytes = formats::disc::readIsoFile(disc.source(), file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    const auto entries = formats::image::splitKnownPack(*bytes, *pack);
    if (!entries) {
        return std::unexpected{entries.error()};
    }
    Extraction extraction;
    for (std::size_t i = 0; i < entries->size(); ++i) {
        const auto entry = (*entries)[i];
        const std::size_t offset = i * pack->strideBytes;
        const auto tim = formats::image::readTim(entry);
        if (!tim) {
            extraction.unknowns.push_back(std::format("entry {} at 0x{:x}: {}", i, offset, tim.error().message));
            continue;
        }
        const auto id = names::packEntry(disc.tables(), file.path, i);
        if (!id) {
            return std::unexpected{id.error()};
        }
        const TimPlace place{.source = file.path, .entry = i, .offset = offset};
        if (auto added = addTimImage(extraction, *id, *tim, place, {}); !added) {
            return std::unexpected{added.error()};
        }
        reportLeftover(extraction, *bytes, offset + tim->byteSize, offset + entry.size(),
                       std::format("after the image of entry {}", i));
    }
    return extraction;
}

bool handlesStackedTims(std::string_view path) {
    return std::ranges::contains(StackedTimFiles, path);
}

core::Result<Extraction> extractStackedTims(const Disc& disc, const formats::disc::IsoFile& file) {
    const auto bytes = formats::disc::readIsoFile(disc.source(), file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    const auto list = formats::image::readStackedTims(*bytes);
    if (!list) {
        return std::unexpected{list.error()};
    }
    return extractTimList(*list, *bytes, file);
}

bool handlesTimFile(std::string_view path) {
    return path.ends_with(".TIM");
}

core::Result<Extraction> extractTimFile(const Disc& disc, const formats::disc::IsoFile& file) {
    const auto bytes = formats::disc::readIsoFile(disc.source(), file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    const auto list = formats::image::readTimSequence(*bytes);
    if (!list) {
        return std::unexpected{list.error()};
    }
    return extractTimList(*list, *bytes, file);
}

} // namespace dwe::dwx
