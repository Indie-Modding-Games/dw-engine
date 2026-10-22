#pragma once

#include <dwe/assets/map_table.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/map/map_file.hpp>
#include <dwe/formats/map/map_yaml.hpp>
#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/script_yaml.hpp>

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
#include <utility>
#include <vector>

#include "glb_builder.hpp"
#include "map_fixtures.hpp"
#include "validate.hpp"
#include "yaml_edits.hpp"

// Synthetic documents of each kind that the org's tools read from the base pack and write into a
// mods pack (docs/architecture/herramientas-externas.md), made the way dwx extract makes them.
namespace dwe::dwx::testing {

// A map as dwx extract writes map/<id>/data.yaml: a synthetic .MAP converted with mapToYaml.
inline std::string extractedMap() {
  constexpr formats::map::MapReadOptions Options{
      .imageCount8 = 1, .imageCount4 = 1, .hasNpcs = true};
  const auto map = formats::map::readMapFile(formats::test::makeMap(), Options);
  REQUIRE_MESSAGE(map.has_value(), errorOf(map));
  return emitted(formats::map::mapToYaml(map.value_or(formats::map::MapFile{})));
}

// The section a DG script starts at when its map loads, and the MAPHEAD section of a new game.
inline constexpr std::uint16_t MapEntrySection = 0x00FE;
inline constexpr std::uint16_t NewGameSection = 0x00CC;

// A script with one section at 8: set_pstat, a textbox, a jump to the end and end_script.
inline std::vector<std::byte> syntheticScript(std::uint16_t section) {
  const auto sectionLow = static_cast<unsigned char>(section & 0xFFU);
  const auto sectionHigh = static_cast<unsigned char>(section >> 8U);
  const std::array<unsigned char, 26> bytes{
      0x08, 0x00, sectionLow, sectionHigh, 0x08, 0x00, 0xFF, 0xFF, // header: the section at 8
      0x1E, 0x00, 0x05,       0x07,                                // 8 set_pstat 5 = 7
      0x1A, 0x00, 0x82,       0x60,        0x0D, 0x00, 0x00, 0x00, // 12 show_textbox "A"
      0x16, 0x00, 0x18,       0x00,                                // 20 jump_to 24
      0xFF, 0x00,                                                  // 24 end_script
  };
  return bytes |
         std::views::transform([](unsigned char value) { return static_cast<std::byte>(value); }) |
         std::ranges::to<std::vector>();
}

// The synthetic script as dwx extract writes a listing: script/dg/NNN.yaml with MapEntrySection,
// and script/maphead.yaml, which has the same layout, with a MAPHEAD section such as
// NewGameSection.
inline std::string extractedScript(std::uint16_t section = MapEntrySection) {
  const auto disassembly = formats::script::disassembleScript(syntheticScript(section));
  REQUIRE_MESSAGE(disassembly.has_value(), errorOf(disassembly));
  REQUIRE(disassembly->unknowns.empty());
  return emitted(
      formats::script::scriptToYaml(disassembly.value_or(formats::script::Disassembly{})));
}

// The parents of the joints of studioModel, as a model contract lists them.
inline constexpr std::string_view StudioModelJointParents = "[-1, 0]";

// The name of the clip of an animation id: "anim_05".
inline std::string clipName(int id) {
  return std::format("anim_{:02}", id);
}

// A model as the animation studio writes digimon/<slug>/model.glb: joints bone_00 and bone_01 in
// one skin, a triangle weighted to bone_01 and clipCount clips from anim_<firstClip>, each with its
// root motion on bone 0 and the extras of a dwx clip at 20 ticks per second.
inline std::vector<std::byte> studioModel(int clipCount, int firstClip = 0) {
  assets::testing::GlbBuilder builder;
  const int positions = builder.addFloats({0, 10, 0, 1, 10, 0, 0, 11, 0}, "VEC3", 3,
                                          R"(,"min":[0,10,0],"max":[1,11,0])");
  const std::vector<std::uint8_t> joints{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  const int jointAccessor = builder.addAccessor(
      std::span<const std::uint8_t>{joints}, assets::testing::GlbBuilder::UnsignedByte, "VEC4", 3);
  const int weights = builder.addFloats({1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}, "VEC4", 4);
  const int inverseBind = builder.addFloats({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0,   0, 1, //
                                             1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -10, 0, 1},
                                            "MAT4", 16);
  const int times = builder.addFloats({0.0F, 0.05F}, "SCALAR", 1, R"(,"min":[0],"max":[0.05])");
  const int rootMotion = builder.addFloats({0, 0, 0, 0, 0, 10}, "VEC3", 3);
  std::string clips;
  for (int clip = firstClip; clip < firstClip + clipCount; ++clip) {
    clips += std::format(
        R"({}{{"name":"{}","extras":{{"id":{},"frameCount":2,"hasScale":false,"ticksPerSecond":20,)"
        R"("tickCount":2,"endsInLoop":false,"rootMotionNode":0,"loops":[],"textureMoves":[],"sounds":[]}},)"
        R"("samplers":[{{"input":{},"output":{}}}],"channels":[{{"sampler":0,"target":{{"node":0,"path":"translation"}}}}]}})",
        clip == firstClip ? "" : ",", clipName(clip), clip, times, rootMotion);
  }
  return builder.build(std::format(
      R"({{"scene":0,"scenes":[{{"nodes":[0,2]}}],)"
      R"("nodes":[{{"name":"bone_00","children":[1]}},{{"name":"bone_01","translation":[0,10,0]}},)"
      R"({{"name":"model","mesh":0,"skin":0}}],)"
      R"("skins":[{{"joints":[0,1],"inverseBindMatrices":{}}}],)"
      R"("meshes":[{{"primitives":[{{"attributes":{{"POSITION":{},"JOINTS_0":{},"WEIGHTS_0":{}}}}}]}}],)"
      R"("animations":[{}]}})",
      inverseBind, positions, jointAccessor, weights, clips));
}

// The mods pack that the tests write the way the tools do, and what it edits.
inline constexpr std::string_view BasePackId = "base";
inline constexpr assets::Version BasePackVersion{.major = 0, .minor = 1, .patch = 0};
inline constexpr std::string_view ToolPackId = "tool_edits";
inline constexpr std::string_view ToolPackName = "Tool edits";
inline constexpr std::string_view EditedMapId = "twna01";
inline constexpr std::string_view EditedDigimon = "species_a";
inline constexpr std::string_view EditedScript = "script/dg/001";
inline constexpr std::string_view MapHeadScript = "script/maphead";
inline constexpr std::string_view MapTable = "data/maps";
inline constexpr std::string_view YamlExtension = "yaml";
inline constexpr std::string_view PngExtension = "png";
inline constexpr std::string_view GlbExtension = "glb";
inline constexpr std::string_view YamlNull = "null";
// How far the editor moves the first spawn of the map, in the map's units.
inline constexpr int SpawnShift = 100;

// The manifest the tools write: the mods layer and a dependency on the base pack.
inline assets::PackManifest toolPackManifest() {
  return {.id = std::string{ToolPackId},
          .version = {.major = 1, .minor = 0, .patch = 0},
          .layer = assets::Layer::Mods,
          .priority = 0,
          .name = std::string{ToolPackName},
          .description = {},
          .dependencies = {{.id = std::string{BasePackId}, .minVersion = BasePackVersion}},
          .schemas = {},
          .api = {}};
}

// pack.yaml for a manifest, laid out as dwx extract and the tools write it.
inline std::string manifestText(const assets::PackManifest& manifest) {
  core::YamlNode root = core::YamlNode::mapping();
  root.set("id", core::YamlNode::scalar(manifest.id));
  root.set("version", core::YamlNode::scalar(assets::formatVersion(manifest.version)));
  root.set("layer", core::YamlNode::scalar(std::string{assets::layerName(manifest.layer)}));
  root.set("name", core::YamlNode::scalar(manifest.name, core::YamlNode::ScalarStyle::Quoted));
  core::YamlNode dependencies = core::YamlNode::sequence();
  for (const assets::PackDependency& dependency : manifest.dependencies) {
    core::YamlNode entry = core::YamlNode::mapping();
    entry.set("id", core::YamlNode::scalar(dependency.id));
    entry.set("min_version", core::YamlNode::scalar(assets::formatVersion(dependency.minVersion)));
    dependencies.append(std::move(entry));
  }
  if (!manifest.dependencies.empty()) {
    root.set("dependencies", std::move(dependencies));
  }
  core::YamlNode schemas = core::YamlNode::mapping();
  for (const assets::SchemaRule& rule : manifest.schemas) {
    core::YamlNode ids = core::YamlNode::sequence();
    for (const std::string& id : rule.ids) {
      ids.append(core::YamlNode::scalar(id, core::YamlNode::ScalarStyle::Quoted));
    }
    schemas.set(rule.schema, std::move(ids));
  }
  if (!manifest.schemas.empty()) {
    root.set("schemas", std::move(schemas));
  }
  return emitted(root);
}

// The id of a file of the edited map: "map/twna01/<leaf>".
inline std::string editedMapAsset(std::string_view leaf) {
  return std::format("map/{}/{}", EditedMapId, leaf);
}

// The id of a file of the edited Digimon: "digimon/species_a/<leaf>".
inline std::string editedDigimonAsset(std::string_view leaf) {
  return std::format("digimon/{}/{}", EditedDigimon, leaf);
}

// The file of an asset in a pack: <pack>/<id>.<extension>.
inline std::filesystem::path packFile(const std::filesystem::path& pack, std::string_view id,
                                      std::string_view extension) {
  return pack / std::format("{}.{}", id, extension);
}

// The problems of a dwx validate report, one "file: message" line each.
inline std::string problemsOf(const ValidationReport& report) {
  std::string text;
  for (const ValidationProblem& problem : report.problems) {
    text += std::format("{}: {}\n", problem.file, problem.message);
  }
  return text;
}

// The path to the x of the first warp's spawn in a map document.
inline YamlPath firstSpawnX() {
  return {"warps", "0", "spawn", "x"};
}

// The x of the first warp's spawn of a map document.
inline int firstSpawnXOf(std::string_view map) {
  return std::stoi(nodeAt(parsed(map), firstSpawnX()).text());
}

// A map document with its first spawn moved SpawnShift along x, as the editor saves it.
inline std::string movedSpawn(std::string_view map) {
  return emitted(
      withScalar(parsed(map), firstSpawnX(), std::to_string(firstSpawnXOf(map) + SpawnShift)));
}

// The path to the toilet of the first map of the map table.
inline YamlPath firstToilet() {
  return {"maps", "0", "toilet"};
}

// A map table whose first map has no toilet, as the editor saves it.
inline std::string withoutFirstToilet(std::string_view table) {
  return emitted(withScalar(parsed(table), firstToilet(), std::string{YamlNull}));
}

// A key that no map of the map table has, and one that every map must have.
inline constexpr std::string_view UnknownMapKey = "weather";
inline constexpr std::string_view RequiredMapKey = "doors";

// Checks that the engine refuses a map table whose first map has a key too many or one too few, and
// says which.
inline void checkMapTableRefusesWrongKeys(std::string_view table) {
  const YamlPath firstMap{"maps", "0"};
  const auto extra = assets::parseMapTable(emitted(withKey(
      parsed(table), firstMap, std::string{UnknownMapKey}, core::YamlNode::scalar("rain"))));
  REQUIRE_FALSE(extra.has_value());
  CHECK(errorOf(extra).contains(std::format("'{}'", UnknownMapKey)));
  const auto missing =
      assets::parseMapTable(emitted(withoutKey(parsed(table), firstMap, RequiredMapKey)));
  REQUIRE_FALSE(missing.has_value());
  CHECK(errorOf(missing).contains(std::format("'{}'", RequiredMapKey)));
}

// The path to the text of the first textbox of a listing, if it has one.
inline std::optional<YamlPath> firstText(const core::YamlNode& listing) {
  const core::YamlNode* instructions = listing.find("instructions");
  REQUIRE(instructions != nullptr);
  REQUIRE(instructions->isSequence());
  const auto items = instructions->items();
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (items[index].find("text") != nullptr) {
      return YamlPath{"instructions", std::to_string(index), "text"};
    }
  }
  return std::nullopt;
}

// A listing with the text of its first textbox rewritten, as the editor saves it.
inline std::string withFirstTextRewritten(std::string_view listing, const std::string& text) {
  const auto path = firstText(parsed(listing));
  REQUIRE(path.has_value());
  return emitted(withScalar(parsed(listing), path.value_or(YamlPath{}), text));
}

} // namespace dwe::dwx::testing
