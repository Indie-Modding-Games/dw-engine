#include "upscale.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/image_palettes.hpp>
#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/image/png.hpp>

#include <algorithm>
#include <print>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "bundled_recipes.hpp"
#include "dwx.hpp"
#include "messages.hpp"

namespace dwe::dwx {

namespace {

constexpr std::string_view PngExtension = ".png";
constexpr std::string_view YamlExtension = ".yaml";
constexpr std::string_view ManifestFile = "pack.yaml";
// The key an upscaled image's metadata gains: how many of its pixels make one pixel of the original.
constexpr std::string_view ScaleKey = "scale";
// The folder written before it replaces the output, so a stopped run never leaves half a pack under the pack's name.
constexpr std::string_view PartialSuffix = ".partial";
constexpr std::string_view UpscalePackVersion = "0.1.0";

// A folder resolved, with no trailing separator: "out/" names the folder "out", so the output's .partial is written
// next to it, not into it, and the base pack's parent is the folder that holds it.
core::Result<std::filesystem::path> resolvedFolder(const std::filesystem::path& out) {
    std::error_code error;
    std::filesystem::path resolved = std::filesystem::weakly_canonical(out, error);
    if (error) {
        return core::fail(core::formatMessage(messages::UpscaleCannotResolve, assets::pathText(out), error.message()));
    }
    if (!resolved.has_filename()) {
        resolved = resolved.parent_path();
    }
    return resolved;
}

// The packs to read the images from: those under `packs`, but not the output, which may sit among them. The output
// may not hold `packs` nor hold or sit in any pack read (cook's guard, isWithin).
core::Result<std::vector<assets::FoundPack>>
sourcePacks(const assets::FileSystem& disk, const std::filesystem::path& packs, const std::filesystem::path& out) {
    auto found = assets::findPacks(disk, packs);
    if (!found) {
        return std::unexpected{found.error()};
    }
    std::error_code error;
    const std::filesystem::path resolvedPacks = std::filesystem::weakly_canonical(packs, error);
    if (error) {
        return core::fail(
            core::formatMessage(messages::UpscaleCannotResolve, assets::pathText(packs), error.message()));
    }
    if (isWithin(resolvedPacks, out)) {
        return core::fail(
            core::formatMessage(messages::UpscaleOutputOverlaps, assets::pathText(out), assets::pathText(packs)));
    }
    std::vector<assets::FoundPack> sources;
    for (const assets::FoundPack& pack : found->packs) {
        const std::filesystem::path resolvedPack = std::filesystem::weakly_canonical(pack.path, error);
        if (error) {
            return core::fail(
                core::formatMessage(messages::UpscaleCannotResolve, assets::pathText(pack.path), error.message()));
        }
        if (resolvedPack == out) {
            continue;
        }
        if (isWithin(out, resolvedPack) || isWithin(resolvedPack, out)) {
            return core::fail(core::formatMessage(messages::UpscaleOutputOverlaps, assets::pathText(out),
                                                  assets::pathText(pack.path)));
        }
        sources.push_back(pack);
    }
    if (sources.empty()) {
        return core::fail(core::formatMessage(messages::UpscaleNoPacks, assets::pathText(packs)));
    }
    return sources;
}

std::vector<formats::image::Rgba8> paletteOf(const assets::IndexedImage& image) {
    std::vector<formats::image::Rgba8> palette;
    palette.reserve(image.palette.size());
    for (const assets::PaletteColor& color : image.palette) {
        palette.push_back({.r = color.red, .g = color.green, .b = color.blue, .a = color.alpha});
    }
    return palette;
}

bool providesPng(const assets::VirtualFileSystem& files, const assets::AssetId& id) {
    const assets::AssetSource* source = files.resolve(id);
    return source != nullptr && assets::pathText(source->file.extension()) == PngExtension;
}

core::Result<std::vector<std::byte>> upscaledPng(const UpscaleRecipe& recipe, const assets::VirtualFileSystem& files,
                                                 const assets::AssetId& id) {
    if (!providesPng(files, id)) {
        return core::fail(core::formatMessage(messages::UpscaleImageMissing, id.str()));
    }
    auto bytes = files.read(id);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    auto image = assets::decodeIndexedPng(*bytes);
    if (!image) {
        return core::fail(core::formatMessage(messages::UpscaleNotIndexed, id.str(), image.error().message));
    }
    auto grown = indexScale(*image, recipe.scale, recipe.mode);
    if (!grown) {
        return std::unexpected{grown.error()};
    }
    return formats::image::encodePngIndexed(grown->width, grown->height, grown->indices, paletteOf(*grown));
}

// The image's metadata with `scale` added (or replaced), or nothing when the image has none.
core::Result<std::optional<std::string>>
upscaledMetadata(const UpscaleRecipe& recipe, const assets::VirtualFileSystem& files, const assets::AssetId& id) {
    auto metaId = assets::AssetId::parse(id.str() + std::string{assets::ImageMetaSuffix});
    if (!metaId || files.resolve(*metaId) == nullptr) {
        return std::optional<std::string>{};
    }
    auto bytes = files.read(*metaId);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    auto tree = core::parseYaml(std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
    if (!tree || !tree->isMapping()) {
        return core::fail(core::formatMessage(messages::UpscaleBadMetadata, metaId->str()));
    }
    core::YamlNode metadata = core::YamlNode::mapping();
    for (const core::YamlEntry& entry : tree->entries()) {
        if (entry.key != ScaleKey) {
            metadata.set(entry.key, entry.value);
        }
    }
    metadata.set(std::string{ScaleKey}, core::YamlNode::scalar(std::to_string(recipe.scale)));
    auto text = core::emitYaml(metadata);
    if (!text) {
        return std::unexpected{text.error()};
    }
    return std::optional<std::string>{std::move(*text)};
}

std::string manifestOf(const UpscaleRecipe& recipe) {
    core::YamlNode manifest = core::YamlNode::mapping();
    manifest.set("id", core::YamlNode::scalar(recipe.id));
    manifest.set("version", core::YamlNode::scalar(std::string{UpscalePackVersion}));
    manifest.set("layer", core::YamlNode::scalar(std::string{assets::layerName(assets::Layer::Remaster)}));
    manifest.set("description", core::YamlNode::scalar(
                                    core::formatMessage(messages::UpscalePackDescription, recipe.id, recipe.scale)));
    return core::emitYaml(manifest).value_or(std::string{});
}

core::Result<void> writeText(const std::filesystem::path& file, std::string_view text) {
    return writeFile(file, std::as_bytes(std::span{text}));
}

core::Result<UpscaleSummary> writeUpscaled(const UpscaleRecipe& recipe, const assets::VirtualFileSystem& files,
                                           const std::filesystem::path& folder, MissingImage missing) {
    // The manifest goes first: it marks the folder as this recipe's (isOwnOutput) from the start, so a run that stops
    // halfway leaves a .partial the next run knows it may remove.
    if (auto written = writeText(folder / ManifestFile, manifestOf(recipe)); !written) {
        return std::unexpected{written.error()};
    }
    UpscaleSummary summary;
    for (const assets::AssetId& id : recipe.images) {
        if (missing == MissingImage::Skip && !providesPng(files, id)) {
            ++summary.skipped;
            continue;
        }
        auto png = upscaledPng(recipe, files, id);
        if (!png) {
            return std::unexpected{png.error()};
        }
        const std::filesystem::path image = folder / (id.str() + std::string{PngExtension});
        if (auto written = writeFile(image, *png); !written) {
            return std::unexpected{written.error()};
        }
        ++summary.images;
        auto metadata = upscaledMetadata(recipe, files, id);
        if (!metadata) {
            return std::unexpected{metadata.error()};
        }
        if (*metadata) {
            const std::string metaFile = id.str() + std::string{assets::ImageMetaSuffix} + std::string{YamlExtension};
            if (auto written = writeText(folder / metaFile, **metadata); !written) {
                return std::unexpected{written.error()};
            }
            ++summary.metadata;
        }
    }
    return summary;
}

// Whether a folder is a pack this recipe wrote: a remaster pack with the recipe's id.
bool isOwnOutput(const std::filesystem::path& folder, const UpscaleRecipe& recipe) {
    const assets::DiskFileSystem disk;
    auto manifest = assets::readPackManifest(assets::PackLocation{.fileSystem = &disk, .root = folder});
    return manifest && manifest->id == recipe.id && manifest->layer == assets::Layer::Remaster;
}

core::Result<void> replaceOutput(const std::filesystem::path& partial, const std::filesystem::path& out,
                                 const UpscaleRecipe& recipe) {
    std::error_code error;
    if (std::filesystem::exists(out, error)) {
        if (!isOwnOutput(out, recipe)) {
            return core::fail(core::formatMessage(messages::UpscaleOutputTaken, assets::pathText(out), recipe.id));
        }
        std::filesystem::remove_all(out, error);
    }
    if (!error) {
        std::filesystem::rename(partial, out, error);
    }
    if (error) {
        return core::fail(core::formatMessage(messages::UpscaleCannotReplace, assets::pathText(out), error.message()));
    }
    return {};
}

} // namespace

core::Result<UpscaleSummary> upscalePacks(const UpscaleRecipe& recipe, const std::filesystem::path& packs,
                                          const std::filesystem::path& requestedOut, MissingImage missing) {
    auto resolved = resolvedFolder(requestedOut);
    if (!resolved) {
        return std::unexpected{resolved.error()};
    }
    const std::filesystem::path& out = *resolved;
    const assets::DiskFileSystem disk;
    auto found = sourcePacks(disk, packs, out);
    if (!found) {
        return std::unexpected{found.error()};
    }
    auto opened = assets::openPacks(disk, *found, assets::PackOrigin::Engine);
    if (!opened) {
        return std::unexpected{opened.error()};
    }
    auto files = assets::VirtualFileSystem::mount(opened->locations);
    if (!files) {
        return std::unexpected{files.error()};
    }
    std::filesystem::path partial = out;
    partial += PartialSuffix;
    std::error_code error;
    if (std::filesystem::exists(partial, error) && !isOwnOutput(partial, recipe) &&
        !std::filesystem::is_empty(partial, error)) {
        return core::fail(core::formatMessage(messages::UpscalePartialTaken, assets::pathText(partial)));
    }
    std::filesystem::remove_all(partial, error);
    auto summary = writeUpscaled(recipe, *files, partial, missing);
    if (!summary || summary->images == 0) {
        std::filesystem::remove_all(partial, error);
        return summary;
    }
    if (auto replaced = replaceOutput(partial, out, recipe); !replaced) {
        std::filesystem::remove_all(partial, error);
        return std::unexpected{replaced.error()};
    }
    return summary;
}

core::Result<void> runBundledRecipes(const std::filesystem::path& basePack, std::FILE* report) {
    auto base = resolvedFolder(basePack);
    if (!base) {
        return std::unexpected{base.error()};
    }
    for (const std::string_view text : BundledRecipes) {
        auto recipe = parseUpscaleRecipe(text);
        if (!recipe) {
            return core::fail(core::formatMessage(messages::BundledRecipeBroken, recipe.error().message));
        }
        const std::filesystem::path out = base->parent_path() / recipe->id;
        auto summary = upscalePacks(*recipe, *base, out, MissingImage::Skip);
        if (!summary) {
            return std::unexpected{summary.error()};
        }
        if (summary->images == 0) {
            std::println(report, "Upscaled pack {}: none of its images is in the base pack, not written", recipe->id);
            continue;
        }
        std::println(report, "Upscaled pack: {} ({} images grown {}x, {} not in the base pack)", assets::pathText(out),
                     summary->images, recipe->scale, summary->skipped);
    }
    return {};
}

int upscale(const std::filesystem::path& recipePath, const std::filesystem::path& packs,
            const std::filesystem::path& out) {
    const assets::DiskFileSystem disk;
    auto text = disk.readFile(recipePath);
    if (!text) {
        std::println(stderr, "dwx: {}", text.error().message);
        return ExitFailed;
    }
    auto recipe = parseUpscaleRecipe(std::string_view{reinterpret_cast<const char*>(text->data()), text->size()});
    if (!recipe) {
        std::println(stderr, "dwx: {}: {}", assets::pathText(recipePath), recipe.error().message);
        return ExitFailed;
    }
    auto summary = upscalePacks(*recipe, packs, out);
    if (!summary) {
        std::println(stderr, "dwx: {}", summary.error().message);
        return ExitFailed;
    }
    std::println("Upscaled pack: {}", assets::pathText(out));
    std::println("Images: {} grown {}x, {} with their metadata", summary->images, recipe->scale, summary->metadata);
    return ExitOk;
}

} // namespace dwe::dwx
