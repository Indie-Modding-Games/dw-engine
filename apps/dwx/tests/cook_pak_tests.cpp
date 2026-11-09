#include <dwe/assets/content_check.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/formats/image/png.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "cook.hpp"
#include "cook_pak.hpp"
#include "dwx.hpp"
#include "dwx_command.hpp"

using namespace dwe::dwx;
using namespace dwe::dwx::testing;
using dwe::test_support::TempFolder;

namespace {

std::string readText(const std::filesystem::path& file) {
  std::ifstream stream{file, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, {}};
}

// The names in a folder, sorted.
std::vector<std::string> namesIn(const std::filesystem::path& folder) {
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator{folder}) {
    names.push_back(entry.path().filename().string());
  }
  std::ranges::sort(names);
  return names;
}

std::string indexedPng() {
  const std::vector<std::uint8_t> indices(4, 1);
  const std::array<dwe::formats::image::Rgba8, 2> palette{{{}, {.r = 200, .a = 255}}};
  const auto png = dwe::formats::image::encodePngIndexed(2, 2, indices, palette);
  REQUIRE(png.has_value());
  return {reinterpret_cast<const char*>(png->data()), png->size()};
}

} // namespace

TEST_CASE("an install from scratch ends with base.pak alone, which the engine mounts and opens") {
  const TempFolder temp;
  const std::filesystem::path partial = temp.path() / "content.partial";
  const std::filesystem::path image = writeImage(temp.path(), unknownGameDisc());
  const std::array extract{std::string{"extract"}, std::string{"--progress"}, std::string{"--out"},
                           dwe::assets::pathText(partial / "base"), dwe::assets::pathText(image)};
  REQUIRE(runDwx(extract).exitCode == ExitOk);
  const std::string manifest = readText(partial / "base/pack.yaml");

  const std::array cookPak{std::string{"cook-pak"}, dwe::assets::pathText(partial / "base"),
                           std::string{"-o"}, dwe::assets::pathText(partial / "base.pak"),
                           std::string{"--progress"}};
  const CommandOutput output = runDwx(cookPak);
  REQUIRE(output.exitCode == ExitOk);
  CHECK(has(output, "progress 0 cook"));
  CHECK(has(output, "progress 80 pack"));
  CHECK(has(output, "progress 96 check"));
  CHECK(has(output, "progress 100 done"));
  // The source is left as it was; the cooked folder and the partial archive are gone.
  CHECK(readText(partial / "base/pack.yaml") == manifest);
  const std::vector<std::string> names = namesIn(partial);
  CHECK(std::ranges::none_of(names, [](const std::string& name) {
    return name.ends_with(".cook") || name.ends_with(".partial");
  }));
  CHECK(std::ranges::contains(names, "base.pak"));

  // The install drops the loose folder: the content holds the archive and the remastered UI.
  std::filesystem::remove_all(partial / "base");
  const dwe::assets::DiskFileSystem disk;
  const auto found = dwe::assets::findPacks(disk, partial);
  REQUIRE(found.has_value());
  REQUIRE_FALSE(found->packs.empty());
  CHECK(found->packs.front().path == partial / "base.pak");
  CHECK(std::ranges::none_of(found->packs, [](const dwe::assets::FoundPack& pack) {
    return pack.kind == dwe::assets::FoundPack::Kind::Folder && pack.path.filename() == "base";
  }));
  CHECK(found->packs.front().kind == dwe::assets::FoundPack::Kind::Archive);
  const auto opened = dwe::assets::openPacks(disk, found->packs, dwe::assets::PackOrigin::Engine);
  REQUIRE(opened.has_value());
  const auto files = dwe::assets::VirtualFileSystem::mount(opened->locations);
  REQUIRE_MESSAGE(files.has_value(), files.error().message);
  CHECK_FALSE(files->ids().empty());
  CHECK(dwe::assets::checkContent(*files).empty());
}

TEST_CASE("cook-pak cooks a loose pack into an archive the engine reads, keeping the pack") {
  const TempFolder temp;
  temp.write("content/base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  temp.write("content/base/texture/ui/plain.png", indexedPng());
  temp.write("content/base/map/start/background_day.png", indexedPng());
  const std::filesystem::path archive = temp.path() / "convert/base.pak";
  std::vector<std::string> steps;
  const auto summary = cookPakArchive(cookers(), temp.path() / "content/base", archive,
                                      [&](int /*percent*/, std::string_view step) {
                                        if (steps.empty() || steps.back() != step) {
                                          steps.emplace_back(step);
                                        }
                                      });
  REQUIRE_MESSAGE(summary.has_value(), summary.error().message);
  CHECK(steps == std::vector<std::string>{"cook", "pack", "check", "done"});
  CHECK(summary->entries == 2);
  CHECK(namesIn(temp.path() / "convert") == std::vector<std::string>{"base.pak"});
  CHECK(std::filesystem::is_regular_file(temp.path() / "content/base/texture/ui/plain.png"));
  // The texture became KTX2; the map background, which the game reads by its indices, stays PNG.
  const auto pak = dwe::assets::PakFileSystem::open(archive);
  REQUIRE(pak.has_value());
  std::vector<std::string> paths;
  std::ranges::transform((*pak)->entries(), std::back_inserter(paths),
                         &dwe::assets::PakEntry::path);
  CHECK(paths == std::vector<std::string>{"map/start/background_day.png", "texture/ui/plain.ktx2"});
}

TEST_CASE("cook-pak keeps the archive there when it fails, and refuses where it cannot write") {
  const TempFolder temp;
  temp.write("base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  temp.write("base/texture/broken.png", "not a png");
  temp.write("out/base.pak", "the archive of an earlier install");
  const auto failed = cookPakArchive(cookers(), temp.path() / "base", temp.path() / "out/base.pak");
  REQUIRE_FALSE(failed.has_value());
  CHECK(readText(temp.path() / "out/base.pak") == "the archive of an earlier install");
  CHECK_FALSE(std::filesystem::exists(partialArchiveOf(temp.path() / "out/base.pak")));

  CHECK_FALSE(
      cookPakArchive(cookers(), temp.path() / "base", temp.path() / "out/base.zip").has_value());
  CHECK_FALSE(
      cookPakArchive(cookers(), temp.path() / "base", temp.path() / "base/inner.pak").has_value());
  CHECK_FALSE(std::filesystem::exists(temp.path() / "base/inner.pak"));
}
