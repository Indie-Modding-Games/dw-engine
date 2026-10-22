#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/formats/image/png.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "editor_documents.hpp"
#include "map_documents.hpp"
#include "validate.hpp"
#include "yaml_edits.hpp"

// R4.7: a mods pack with exactly the kinds of file that the map editor and the animation studio
// write (docs/architecture/herramientas-externas.md), all synthetic, passes dwx validate on top of
// a base pack and wins over it by id.

using namespace dwe::dwx::testing;
using dwe::test_support::TempFolder;

namespace {

// The folders of the two packs inside the test's temporary folder.
std::filesystem::path baseFolder() {
  return std::filesystem::path{"content"} / BasePackId;
}

std::filesystem::path toolFolder() {
  return std::filesystem::path{"packs"} / ToolPackId;
}

// The base pack with the schema rules that dwx extract writes for the documents the tools write.
dwe::assets::PackManifest basePackManifest() {
  return {.id = std::string{BasePackId},
          .version = BasePackVersion,
          .layer = dwe::assets::Layer::Base,
          .priority = 0,
          .name = "Base",
          .description = {},
          .dependencies = {},
          .schemas = {{.schema = "maps", .ids = {std::string{MapTable}}},
                      {.schema = "map", .ids = {"map/*/data"}},
                      {.schema = "model_contract", .ids = {"digimon/*/contract"}},
                      {.schema = "script", .ids = {std::string{MapHeadScript}, "script/*/*"}}},
          .api = {}};
}

std::string textOf(std::span<const std::byte> bytes) {
  return bytes | std::views::transform([](std::byte value) { return static_cast<char>(value); }) |
         std::ranges::to<std::string>();
}

// A background as the game reads it (loadMapImages): an indexed PNG, whose palette the time of day
// picks.
std::string backgroundPng() {
  const std::vector<std::uint8_t> indices{0};
  const std::vector<dwe::formats::image::Rgba8> palette{
      {.r = 0x7F, .g = 0x7F, .b = 0x7F, .a = 0xFF}};
  const auto png = dwe::formats::image::encodePngIndexed(1, 1, indices, palette);
  REQUIRE_MESSAGE(png.has_value(), errorOf(png));
  return textOf(png.value_or(std::vector<std::byte>{}));
}

std::string baseMapTable() {
  return savedUnchanged(dwe::assets::testing::FixtureTable);
}

// The contract of the base model: studioModel's joints and its first clip.
std::string modelContract() {
  return std::format(
      "model: \"model.{}\"\nskeleton: \"{}\"\njoint_parents: {}\nanimations:\n- \"{}\"\n",
      GlbExtension, EditedDigimon, StudioModelJointParents, clipName(0));
}

// What each tool changes: a spawn moved, a toilet removed, the texts of two scripts rewritten and a
// clip added.
struct ToolEdits {
  std::string map = movedSpawn(extractedMap());
  std::string table = withoutFirstToilet(baseMapTable());
  std::string script = withFirstTextRewritten(extractedScript(), "Edited");
  std::string mapHead = withFirstTextRewritten(extractedScript(NewGameSection), "Edited");
  std::vector<std::byte> model = studioModel(2);
};

void writeBasePack(const TempFolder& folder) {
  folder.write(baseFolder() / "pack.yaml", manifestText(basePackManifest()));
  folder.write(packFile(baseFolder(), editedMapAsset("data"), YamlExtension), extractedMap());
  folder.write(packFile(baseFolder(), editedMapAsset("background_day"), PngExtension),
               backgroundPng());
  folder.write(packFile(baseFolder(), MapTable, YamlExtension), baseMapTable());
  folder.write(packFile(baseFolder(), EditedScript, YamlExtension), extractedScript());
  folder.write(packFile(baseFolder(), MapHeadScript, YamlExtension),
               extractedScript(NewGameSection));
  folder.write(packFile(baseFolder(), editedDigimonAsset("model"), GlbExtension),
               textOf(studioModel(1)));
  folder.write(packFile(baseFolder(), editedDigimonAsset("contract"), YamlExtension),
               modelContract());
}

void writeToolPack(const TempFolder& folder, const ToolEdits& edits) {
  folder.write(toolFolder() / "pack.yaml", manifestText(toolPackManifest()));
  folder.write(packFile(toolFolder(), editedMapAsset("data"), YamlExtension), edits.map);
  folder.write(packFile(toolFolder(), editedMapAsset("background_day"), PngExtension),
               backgroundPng());
  folder.write(packFile(toolFolder(), editedMapAsset("background_night"), PngExtension),
               backgroundPng());
  folder.write(packFile(toolFolder(), MapTable, YamlExtension), edits.table);
  folder.write(packFile(toolFolder(), EditedScript, YamlExtension), edits.script);
  folder.write(packFile(toolFolder(), MapHeadScript, YamlExtension), edits.mapHead);
  folder.write(packFile(toolFolder(), editedDigimonAsset("model"), GlbExtension),
               textOf(edits.model));
}

dwe::core::Result<dwe::dwx::ValidationReport> validateToolPack(const TempFolder& folder) {
  const std::array<std::filesystem::path, 1> with{folder.path() / baseFolder()};
  return dwe::dwx::validatePacks(dwe::assets::DiskFileSystem{}, folder.path() / toolFolder(),
                                 DWE_SCHEMA_DIR, with);
}

} // namespace

TEST_CASE(
    "a mods pack of the files the tools write passes dwx validate with the base pack and wins over it by id") {
  const TempFolder folder;
  const ToolEdits edits;
  writeBasePack(folder);
  writeToolPack(folder, edits);

  const auto report = validateToolPack(folder);
  REQUIRE_MESSAGE(report.has_value(), errorOf(report));
  CHECK_MESSAGE(report->problems.empty(), problemsOf(*report));
  CHECK(report->packs == 2);
  // The one contract of the stack, checked against the model that wins: the studio's.
  CHECK(report->contracts == 1);

  const dwe::assets::DiskFileSystem disk;
  // The base pack is the engine's own; the tools' pack is a mod.
  const std::array<dwe::assets::PackLocation, 2> locations{
      dwe::assets::PackLocation{.fileSystem = &disk,
                                .root = folder.path() / baseFolder(),
                                .fillsGaps = false,
                                .origin = dwe::assets::PackOrigin::Engine},
      dwe::assets::PackLocation{.fileSystem = &disk,
                                .root = folder.path() / toolFolder(),
                                .fillsGaps = false,
                                .origin = dwe::assets::PackOrigin::Mod}};
  const auto vfs = dwe::assets::VirtualFileSystem::mount(locations);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  for (const std::string& id :
       {editedMapAsset("data"), editedMapAsset("background_day"),
        editedMapAsset("background_night"), std::string{MapTable}, std::string{EditedScript},
        std::string{MapHeadScript}, editedDigimonAsset("model")}) {
    const auto assetId = dwe::assets::AssetId::parse(id);
    REQUIRE_MESSAGE(assetId.has_value(), id);
    const dwe::assets::AssetSource* source = vfs->resolve(*assetId);
    REQUIRE_MESSAGE(source != nullptr, id);
    CHECK_MESSAGE(source->packId == ToolPackId, id);
  }

  const auto map = dwe::assets::loadMapDocument(*vfs, EditedMapId);
  REQUIRE_MESSAGE(map.has_value(), errorOf(map));
  CHECK(map->warps[0].spawn.x == firstSpawnXOf(extractedMap()) + SpawnShift);
  CHECK(map->assets.background("night") != nullptr);
  const auto table = dwe::assets::loadMapTable(*vfs);
  REQUIRE_MESSAGE(table.has_value(), errorOf(table));
  CHECK_FALSE(table->maps[0].toilet.has_value());
  const auto modelId = dwe::assets::AssetId::parse(editedDigimonAsset("model"));
  REQUIRE(modelId.has_value());
  const auto modelBytes = vfs->read(*modelId);
  REQUIRE_MESSAGE(modelBytes.has_value(), errorOf(modelBytes));
  const auto model = dwe::assets::parseModel(*modelBytes);
  REQUIRE_MESSAGE(model.has_value(), errorOf(model));
  CHECK(model->findClip(clipName(1)) != nullptr);
}

TEST_CASE("a model from the animation studio without a clip of its contract fails dwx validate") {
  const TempFolder folder;
  ToolEdits edits;
  edits.model = studioModel(1, 1); // anim_01 instead of the contract's anim_00
  writeBasePack(folder);
  writeToolPack(folder, edits);
  const auto report = validateToolPack(folder);
  REQUIRE_MESSAGE(report.has_value(), errorOf(report));
  REQUIRE(report->problems.size() == 1);
  CHECK(report->problems[0].message.contains(clipName(0)));
}
