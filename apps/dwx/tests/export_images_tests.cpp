#include <dwe/assets/image.hpp>
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

#include "export_images.hpp"

using dwe::dwx::exportImages;
using dwe::test_support::TempFolder;

namespace {

constexpr std::string_view MapTable = R"(maps:
- id: "mayo01"
  index: 0
  filename: "MAYO01"
  images_8bpp: 1
  images_4bpp: 0
  flags: 0
  sound_id: 0
  fixed_time_of_day: false
  has_npcs: false
  doors: null
  toilet: null
  loading_name: 0
- id: "betl02"
  index: 1
  filename: "BETL02"
  images_8bpp: 1
  images_4bpp: 0
  flags: 0
  sound_id: 0
  fixed_time_of_day: false
  has_npcs: false
  doors: null
  toilet: null
  loading_name: 1
doors: []
toilets: []
map_names:
- "  Native Forest  "
- "  Arena  "
light_updates: []
)";

std::string bytesText(const std::vector<std::byte>& bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::string indexedPng(std::uint32_t width, std::uint32_t height) {
  const std::vector<std::uint8_t> indices(static_cast<std::size_t>(width) * height, 1);
  const std::array<dwe::formats::image::Rgba8, 3> palette{
      {{}, {.r = 200, .a = 255}, {.b = 200, .a = 255}}};
  const auto png = dwe::formats::image::encodePngIndexed(width, height, indices, palette);
  REQUIRE(png.has_value());
  return bytesText(*png);
}

// An invented content folder: a base pack with one map background, an image with two palettes and
// one without metadata, and a translation pack that names one of the maps.
void writeContent(const TempFolder& folder) {
  folder.write("content/base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  folder.write("content/base/data/maps.yaml", MapTable);
  folder.write("content/base/map/mayo01/background_day.png", indexedPng(8, 4));
  folder.write("content/base/map/mayo01/texture8/000.png", indexedPng(4, 4));
  folder.write("content/base/texture/ui/sheet.png", indexedPng(2, 2));
  folder.write(
      "content/base/texture/ui/sheet_meta.yaml",
      "mode: indexed4\nwidth: 2\nheight: 2\npalettes:\n- \"0000 001f 7c00\"\n- \"0000 03e0 7c00\"\n");
  folder.write("content/base/texture/ui/plain.png", indexedPng(2, 2));
  folder.write("packs/es/pack.yaml", "id: es\nversion: 1.0.0\nlayer: base\n");
  folder.write("packs/es/text/es/main.yaml", "\"map_names.0000\": \"  Bosque Nativo  \"\n");
}

std::string readText(const std::filesystem::path& file) {
  std::ifstream stream{file, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, {}};
}

dwe::assets::Image decoded(const std::filesystem::path& file) {
  const std::string text = readText(file);
  auto image = dwe::assets::decodePng(std::as_bytes(std::span{text}));
  REQUIRE(image.has_value());
  return *image;
}

} // namespace

TEST_CASE("dwx export-images writes each image as a custom image pack names it") {
  const TempFolder folder;
  writeContent(folder);
  const std::filesystem::path out = folder.path() / "export";
  const std::array folders{folder.path() / "content", folder.path() / "packs"};
  auto summary = exportImages(folders, out, "es");
  REQUIRE(summary.has_value());
  CHECK(summary->backgrounds == 1);
  CHECK(summary->images == 3);
  CHECK(summary->maps == 2);

  CHECK(readText(out / "map/mayo01/background_day.png") == indexedPng(8, 4));
  CHECK_FALSE(std::filesystem::exists(out / "map/mayo01/texture8"));
  CHECK(readText(out / "texture/ui/plain_palette0.png") == indexedPng(2, 2));

  const dwe::assets::Image red = decoded(out / "texture/ui/sheet_palette0.png");
  const dwe::assets::Image green = decoded(out / "texture/ui/sheet_palette1.png");
  REQUIRE(red.width == 2);
  REQUIRE(red.height == 2);
  CHECK(red.pixels != green.pixels);
  CHECK_FALSE(std::filesystem::exists(out / "texture/ui/sheet_palette2.png"));

  SUBCASE("map names come in the language asked for, else as the table has them") {
    CHECK(readText(out / "maps.tsv") == "mayo01\tBosque Nativo\nbetl02\tArena\n");
  }
  SUBCASE("an export is replaced, any other folder is left alone") {
    CHECK(exportImages(folders, out, "en").has_value());
    CHECK(readText(out / "maps.tsv") == "mayo01\tNative Forest\nbetl02\tArena\n");
    folder.write("taken/notes.txt", "mine");
    CHECK_FALSE(exportImages(folders, folder.path() / "taken", "en").has_value());
    CHECK(readText(folder.path() / "taken/notes.txt") == "mine");
  }
}
