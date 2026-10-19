#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/formats/image/png.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "cook.hpp"
#include "temp_folder.hpp"

using namespace dwe::dwx;
using dwe::assets::DiskFileSystem;
using dwe::assets::testing::TempFolder;

namespace {

std::uint32_t u32At(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
    }
    return value;
}

std::string pngText(std::uint8_t red) {
    dwe::formats::image::RgbaImage image{.width = 2, .height = 2, .pixels = {}};
    image.pixels.assign(4, {.r = red, .g = 20, .b = 30, .a = 255});
    const auto png = dwe::formats::image::encodePngRgba(image);
    REQUIRE(png.has_value());
    return {reinterpret_cast<const char*>(png->data()), png->size()};
}

std::string indexedPngText() {
    const std::vector<std::uint8_t> indices{0, 1, 2, 1};
    const std::vector<dwe::formats::image::Rgba8> palette{
        {.r = 0, .g = 0, .b = 0, .a = 0}, {.r = 255, .g = 0, .b = 0, .a = 255}, {.r = 0, .g = 0, .b = 255, .a = 255}};
    const auto png = dwe::formats::image::encodePngIndexed(2, 2, indices, palette);
    REQUIRE(png.has_value());
    return {reinterpret_cast<const char*>(png->data()), png->size()};
}

// A binary glTF with only its header, which is all the cooker checks.
std::string glbText() {
    return std::string{"glTF\x02\x00\x00\x00\x0C\x00\x00\x00", 12};
}

void writeSourcePack(const TempFolder& folder) {
    folder.write("src/pack.yaml", "id: mod\nversion: 1.0.0\nlayer: mods\ndependencies:\n  - id: base\n"
                                  "    min_version: 1.0.0\n");
    folder.write("src/map/start/background.png", pngText(10));
    folder.write("src/map/start/collision.yaml", "cells: []\n");
    folder.write("src/digimon/x/model.glb", glbText());
}

std::vector<std::byte> readOrEmpty(const std::filesystem::path& file) {
    return DiskFileSystem{}.readFile(file).value_or(std::vector<std::byte>{});
}

CookSummary cooked(const TempFolder& folder, std::string_view out = "out") {
    const auto summary = cookPack(cookers(), folder.path() / "src", folder.path() / out);
    REQUIRE(summary.has_value());
    return *summary;
}

std::string cookError(const TempFolder& folder, std::string_view out = "out") {
    const auto summary = cookPack(cookers(), folder.path() / "src", folder.path() / out);
    REQUIRE_FALSE(summary.has_value());
    return summary.error().message;
}

} // namespace

TEST_CASE("cooking a pack keeps its ids and runs each file through its cooker") {
    const TempFolder folder;
    writeSourcePack(folder);
    const CookSummary summary = cooked(folder);
    CHECK(summary.assets == 3);
    CHECK(summary.cooked == 2);
    CHECK(summary.copied == 1);
    CHECK(summary.kept == 0);

    const std::filesystem::path out = folder.path() / "out";
    CHECK(readOrEmpty(out / "pack.yaml") == readOrEmpty(folder.path() / "src/pack.yaml"));
    CHECK(readOrEmpty(out / "map/start/collision.yaml") == readOrEmpty(folder.path() / "src/map/start/collision.yaml"));
    CHECK(readOrEmpty(out / "digimon/x/model.glb") == readOrEmpty(folder.path() / "src/digimon/x/model.glb"));
    const auto texture = readOrEmpty(out / "map/start/background.ktx2");
    REQUIRE(texture.size() > 80);
    CHECK(u32At(texture, 12) == 43);
    CHECK(u32At(texture, 40) == 2);
    CHECK_FALSE(std::filesystem::exists(out / "map/start/background.png"));

    const auto files = dwe::assets::listPackFiles(DiskFileSystem{}, out);
    REQUIRE(files.has_value());
    CHECK(files->size() == 3);

    // The same pack cooks to the same bytes in another folder.
    cooked(folder, "again");
    for (const auto& file : *files) {
        CHECK_MESSAGE(readOrEmpty(out / file.path) == readOrEmpty(folder.path() / "again" / file.path), file.path);
    }
}

TEST_CASE("cooking again only cooks what changed") {
    const TempFolder folder;
    writeSourcePack(folder);
    cooked(folder);
    const std::filesystem::path out = folder.path() / "out";
    const auto first = readOrEmpty(out / "map/start/background.ktx2");

    CookSummary summary = cooked(folder);
    CHECK(summary.kept == 3);
    CHECK(summary.cooked == 0);

    // A changed source cooks again; an output edited by hand is cooked again too.
    folder.write("src/map/start/background.png", pngText(99));
    folder.write("out/digimon/x/model.glb", "edited");
    summary = cooked(folder);
    CHECK(summary.kept == 1);
    CHECK(summary.cooked == 2);
    CHECK(readOrEmpty(out / "map/start/background.ktx2") != first);
    CHECK(readOrEmpty(out / "digimon/x/model.glb") == readOrEmpty(folder.path() / "src/digimon/x/model.glb"));

    // A second asset with the same bytes is copied from the first; an asset that is gone loses its output.
    folder.write("src/map/zone/background.png", pngText(99));
    std::filesystem::remove(folder.path() / "src/map/start/collision.yaml");
    summary = cooked(folder);
    CHECK(summary.shared == 1);
    CHECK(summary.removed == 1);
    CHECK(readOrEmpty(out / "map/zone/background.ktx2") == readOrEmpty(out / "map/start/background.ktx2"));
    CHECK_FALSE(std::filesystem::exists(out / "map/start/collision.yaml"));

    // A cache that does not parse only makes everything cook again.
    folder.write("out/.dwx-cook", "outputs: [");
    summary = cooked(folder);
    CHECK(summary.kept == 0);
    CHECK(summary.cooked == 2);
    CHECK(summary.shared == 1);
}

TEST_CASE("cooking refuses folders it does not own and files that do not cook") {
    const TempFolder folder;
    writeSourcePack(folder);
    folder.write("mine/notes.txt", "keep");
    CHECK(cookError(folder, "mine").ends_with("is not empty and dwx cook did not write it; choose another folder"));
    CHECK(cookError(folder, "src/out").contains("must not hold each other"));

    folder.write("src/digimon/x/model.glb", "not a glb at all");
    CHECK(cookError(folder).ends_with("model.glb: not a binary glTF file"));
    folder.write("src/digimon/x/model.glb", glbText());
    folder.write("src/map/start/background.png", "not a png");
    CHECK(cookError(folder).contains("background.png: not a PNG"));
}

TEST_CASE("lookup images pass through the cook as PNG and sidecars set the colour space") {
    const TempFolder folder;
    writeSourcePack(folder);
    // Indexed, with no sidecar, like a map background drawn with one of its palettes.
    folder.write("src/map/start/background_palette3.png", indexedPngText());
    // RGBA, but its sidecar says it has a CLUT.
    folder.write("src/card/001.png", pngText(40));
    folder.write("src/card/001_meta.yaml", "mode: direct16\nclut_rect: {x: 0, y: 480, width: 16, height: 1}\n"
                                           "png: rgba\n");
    folder.write("src/ui/mask.png", pngText(50));
    folder.write("src/ui/mask_meta.yaml", "color_space: linear\n");

    CookSummary summary = cooked(folder);
    CHECK(summary.assets == 8);
    CHECK(summary.cooked == 3);
    CHECK(summary.passed == 2);
    CHECK(summary.copied == 3);
    const std::filesystem::path out = folder.path() / "out";
    CHECK(readOrEmpty(out / "map/start/background_palette3.png") ==
          readOrEmpty(folder.path() / "src/map/start/background_palette3.png"));
    CHECK(readOrEmpty(out / "card/001.png") == readOrEmpty(folder.path() / "src/card/001.png"));
    CHECK_FALSE(std::filesystem::exists(out / "card/001.ktx2"));
    const auto mask = readOrEmpty(out / "ui/mask.ktx2");
    REQUIRE(mask.size() > 16);
    CHECK(u32At(mask, 12) == 37);
    CHECK(u32At(readOrEmpty(out / "map/start/background.ktx2"), 12) == 43);

    // A sidecar that changes how its image cooks makes it cook again, though the image is the same.
    folder.write("src/card/001_meta.yaml", "mode: direct16\npng: rgba\n");
    folder.write("src/ui/mask_meta.yaml", "color_space: srgb\n");
    summary = cooked(folder);
    CHECK(summary.cooked == 2);
    CHECK(summary.removed == 1);
    CHECK_FALSE(std::filesystem::exists(out / "card/001.png"));
    CHECK(u32At(readOrEmpty(out / "card/001.ktx2"), 12) == 43);
    CHECK(u32At(readOrEmpty(out / "ui/mask.ktx2"), 12) == 43);

    folder.write("src/ui/mask_meta.yaml", "color_space: [");
    CHECK(cookError(folder).contains("mask_meta.yaml"));
    folder.write("src/ui/mask_meta.yaml", "color_space: hdr\n");
    CHECK(cookError(folder).contains("mask.png: its sidecar's color_space is not srgb or linear"));
}
