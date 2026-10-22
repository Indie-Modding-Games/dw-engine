#include <dwe/assets/file_system.hpp>
#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/image/png.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "upscale.hpp"
#include "upscale_recipe.hpp"

using dwe::dwx::IndexScaleMode;
using dwe::dwx::parseUpscaleRecipe;
using dwe::dwx::upscalePacks;
using dwe::test_support::TempFolder;

namespace {

constexpr std::string_view Recipe = R"(recipe: 1
id: ui_test
asset_type: ui
select:
  include:
    - texture/ui/sheet
scale: 2
steps:
  - id: upscale
    builtin: index_scale
    scale: 2
    params:
      mode: edge
)";

// An invented 2x2 checkerboard sprite with a palette of three colours, and its metadata with two
// palettes.
void writeSyntheticPack(const TempFolder& folder) {
  folder.write("content/base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  const std::array<std::uint8_t, 4> indices{1, 2, 2, 1};
  const std::array<dwe::formats::image::Rgba8, 3> palette{
      {{}, {.r = 200, .a = 255}, {.b = 200, .a = 255}}};
  const auto png = dwe::formats::image::encodePngIndexed(2, 2, indices, palette);
  REQUIRE(png.has_value());
  folder.write("content/base/texture/ui/sheet.png",
               std::string_view{reinterpret_cast<const char*>(png->data()), png->size()});
  folder.write(
      "content/base/texture/ui/sheet_meta.yaml",
      "mode: indexed4\nwidth: 2\nheight: 2\npalettes:\n- \"0000 001f 7c00\"\n- \"0000 03e0 7c00\"\n");
}

std::vector<std::byte> readBytes(const std::filesystem::path& file) {
  std::ifstream stream{file, std::ios::binary};
  std::vector<char> text{std::istreambuf_iterator<char>{stream}, {}};
  std::vector<std::byte> bytes(text.size());
  std::ranges::transform(text, bytes.begin(),
                         [](char letter) { return static_cast<std::byte>(letter); });
  return bytes;
}

} // namespace

TEST_CASE("an upscale recipe names its pack, its images, its scale and the index_scale mode") {
  const auto recipe = parseUpscaleRecipe(Recipe);
  REQUIRE(recipe.has_value());
  CHECK(recipe->id == "ui_test");
  REQUIRE(recipe->images.size() == 1);
  CHECK(recipe->images.front().str() == "texture/ui/sheet");
  CHECK(recipe->scale == 2);
  CHECK(recipe->mode == IndexScaleMode::Edge);
}

TEST_CASE(
    "an upscale recipe with a provider, an unknown mode or a factor its mode lacks is refused") {
  const std::string recipe{Recipe};
  const auto replaced = [&](std::string_view from, std::string_view to) {
    std::string text = recipe;
    text.replace(text.find(from), from.size(), to);
    return text;
  };
  CHECK_FALSE(
      parseUpscaleRecipe(replaced("builtin: index_scale", "provider: local_upscaler")).has_value());
  CHECK_FALSE(parseUpscaleRecipe(replaced("mode: edge", "mode: smooth")).has_value());
  CHECK_FALSE(parseUpscaleRecipe(replaced("scale: 2\nsteps", "scale: 5\nsteps")).has_value());
  CHECK_FALSE(parseUpscaleRecipe(replaced("    scale: 2\n", "    scale: 4\n")).has_value());
  CHECK_FALSE(parseUpscaleRecipe(replaced("asset_type: ui", "asset_type: background")).has_value());
  CHECK_FALSE(parseUpscaleRecipe(replaced("id: ui_test", "id: UI-test")).has_value());
  CHECK_FALSE(parseUpscaleRecipe(replaced("recipe: 1", "recipe: 2")).has_value());
}

TEST_CASE("dwx upscale writes a remaster pack with the grown images under their own ids") {
  const TempFolder folder;
  writeSyntheticPack(folder);
  const auto recipe = parseUpscaleRecipe(Recipe);
  REQUIRE(recipe.has_value());
  const std::filesystem::path out = folder.path() / "content/ui_test";

  const auto summary = upscalePacks(*recipe, folder.path() / "content", out);
  REQUIRE(summary.has_value());
  CHECK(summary->images == 1);
  CHECK(summary->metadata == 1);

  const dwe::assets::DiskFileSystem disk;
  const auto manifest =
      dwe::assets::readPackManifest(dwe::assets::PackLocation{.fileSystem = &disk, .root = out});
  REQUIRE(manifest.has_value());
  CHECK(manifest->layer == dwe::assets::Layer::Remaster);

  const auto grown = dwe::assets::decodeIndexedPng(readBytes(out / "texture/ui/sheet.png"));
  REQUIRE(grown.has_value());
  CHECK(grown->width == 4);
  CHECK(grown->indices ==
        std::vector<std::uint8_t>{1, 1, 2, 2, 1, 2, 1, 2, 2, 1, 2, 1, 2, 2, 1, 1});

  const auto metadata = readBytes(out / "texture/ui/sheet_meta.yaml");
  const auto tree = dwe::core::parseYaml(
      std::string_view{reinterpret_cast<const char*>(metadata.data()), metadata.size()});
  REQUIRE(tree.has_value());
  REQUIRE(tree->find("scale") != nullptr);
  CHECK(tree->find("scale")->text() == "2");
  CHECK(tree->find("palettes")->items().size() == 2);

  // The content folder now holds the base and the upscaled pack: the upscaled image wins, and a
  // second run reads the base again, not its own output, and replaces it with the same bytes, file
  // by file.
  const std::array roots{folder.path() / "content/base", out};
  const auto stack =
      dwe::assets::VirtualFileSystem::mount(disk, roots, dwe::assets::PackOrigin::Engine);
  REQUIRE(stack.has_value());
  CHECK(stack->resolve(*dwe::assets::AssetId::parse("texture/ui/sheet"))->packId == "ui_test");
  const std::array files{std::filesystem::path{"pack.yaml"},
                         std::filesystem::path{"texture/ui/sheet.png"},
                         std::filesystem::path{"texture/ui/sheet_meta.yaml"}};
  std::vector<std::vector<std::byte>> firstRun;
  firstRun.reserve(files.size());
  for (const std::filesystem::path& file : files) {
    firstRun.push_back(readBytes(out / file));
  }
  REQUIRE(upscalePacks(*recipe, folder.path() / "content", out).has_value());
  for (std::size_t index = 0; index < files.size(); ++index) {
    CAPTURE(files[index]);
    CHECK_FALSE(firstRun[index].empty());
    CHECK(readBytes(out / files[index]) == firstRun[index]);
  }
}

TEST_CASE("dwx upscale reads an output folder named with a trailing slash as that folder") {
  const TempFolder folder;
  writeSyntheticPack(folder);
  const auto recipe = parseUpscaleRecipe(Recipe);
  REQUIRE(recipe.has_value());
  const std::filesystem::path out = folder.path() / "content/ui_test/";

  REQUIRE(upscalePacks(*recipe, folder.path() / "content", out).has_value());
  CHECK(std::filesystem::exists(folder.path() / "content/ui_test/texture/ui/sheet.png"));
  CHECK_FALSE(std::filesystem::exists(folder.path() / "content/ui_test/.partial"));
  CHECK_FALSE(std::filesystem::exists(folder.path() / "content/ui_test.partial"));
  // Run again the same way: the folder is its own output and is replaced, not read as a source
  // pack.
  REQUIRE(upscalePacks(*recipe, folder.path() / "content", out).has_value());
  CHECK(std::filesystem::exists(folder.path() / "content/ui_test/texture/ui/sheet.png"));
}

TEST_CASE("dwx upscale refuses an output folder that holds or sits in a pack it reads") {
  const TempFolder folder;
  writeSyntheticPack(folder);
  const auto recipe = parseUpscaleRecipe(Recipe);
  REQUIRE(recipe.has_value());
  const std::filesystem::path packs = folder.path() / "content";

  CHECK_FALSE(upscalePacks(*recipe, packs, packs / "base/ui_test").has_value());
  CHECK_FALSE(upscalePacks(*recipe, packs, packs).has_value());
  CHECK_FALSE(upscalePacks(*recipe, packs / "base", folder.path()).has_value());
  CHECK(std::filesystem::exists(packs / "base/texture/ui/sheet.png"));
  CHECK_FALSE(std::filesystem::exists(packs / "base/ui_test"));
}

TEST_CASE("dwx upscale removes only a half-written folder of its own next to the output") {
  const TempFolder folder;
  writeSyntheticPack(folder);
  const auto recipe = parseUpscaleRecipe(Recipe);
  REQUIRE(recipe.has_value());
  const std::filesystem::path packs = folder.path() / "content";

  folder.write("content/ui_test.partial/notes.txt", "someone else's");
  CHECK_FALSE(upscalePacks(*recipe, packs, packs / "ui_test").has_value());
  CHECK(std::filesystem::exists(packs / "ui_test.partial/notes.txt"));

  std::filesystem::remove_all(packs / "ui_test.partial");
  folder.write("content/ui_test.partial/pack.yaml",
               "id: ui_test\nversion: 0.1.0\nlayer: remaster\n");
  REQUIRE(upscalePacks(*recipe, packs, packs / "ui_test").has_value());
  CHECK_FALSE(std::filesystem::exists(packs / "ui_test.partial"));
}

TEST_CASE("dwx upscale never replaces a folder it did not write") {
  const TempFolder folder;
  writeSyntheticPack(folder);
  folder.write("elsewhere/pack.yaml", "id: other\nversion: 1.0.0\nlayer: mods\n");
  const auto recipe = parseUpscaleRecipe(Recipe);
  REQUIRE(recipe.has_value());
  CHECK_FALSE(
      upscalePacks(*recipe, folder.path() / "content", folder.path() / "elsewhere").has_value());
  CHECK(std::filesystem::exists(folder.path() / "elsewhere/pack.yaml"));
  CHECK_FALSE(std::filesystem::exists(folder.path() / "elsewhere.partial"));
}
