// The map editor's output read by the engine: each case of tests/data/written_maps (which dwe-map's
// tests/written.rs writes) is a patch the editor wrote for an edit of base.yaml, and the whole map
// it expects (the editor's own reader applied the patch). The engine applies the same patch to the
// same base and must get the same map, field by field; it must also read the editor's whole map as
// it is.

#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_patch.hpp>

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#ifndef DWE_MAPPER_WRITTEN
#error "DWE_MAPPER_WRITTEN must name the folder of the editor's written maps"
#endif

using dwe::assets::MapDocument;
using dwe::assets::MapPatchText;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

std::string readText(const std::filesystem::path& path) {
  std::ifstream file{path, std::ios::binary};
  REQUIRE_MESSAGE(file.good(), path.string());
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

std::string vectorText(const glm::ivec3& value) {
  return std::format("{} {} {}", value.x, value.y, value.z);
}

template <typename Range>
std::string listText(const Range& values) {
  std::string text;
  for (const auto value : values) {
    text += std::format("{} ", value);
  }
  return text;
}

// Every field the editor may change or must keep, one per line, to compare two maps.
std::string dump(const MapDocument& map) {
  std::string text;
  const auto line = [&text](std::string_view key, const std::string& value) {
    text += std::format("{}: {}\n", key, value);
  };
  line("camera", std::format("{} | {} | {}", vectorText(map.camera.viewpoint),
                             vectorText(map.camera.reference), map.camera.projection));
  for (const auto& light : map.lights) {
    line("light", std::format("{} | {} {} {}", vectorText(light.direction), light.color.r,
                              light.color.g, light.color.b));
  }
  line("ambient", std::format("{} {} {}", map.ambient.r, map.ambient.g, map.ambient.b));
  line("regions", listText(map.regions.a) + "| " + listText(map.regions.b));
  line("tiles",
       std::format("{} x {}: {}", map.tiles.width, map.tiles.height, listText(map.tiles.cells)));
  for (const auto& image : map.images) {
    line("image", std::format("{} {} {}", image.bitsPerPixel, image.offset, image.size));
  }
  line("objects", map.objects ? std::format("{} sprites, {} instances", map.objects->sprites.size(),
                                            map.objects->instances.size())
                              : "none");
  for (const auto& warp : map.warps) {
    line("warp",
         std::format("{} | {} | {} | {} {} | {}", warp.exit, vectorText(warp.spawn), warp.rotation,
                     warp.targetMap, warp.targetMapId.value_or("-"), warp.targetExit));
  }
  line("npc count", std::to_string(map.digimonCount));
  for (const auto& npc : map.npcs) {
    line("npc", std::format("{} {} | {} | {} | {} {} {}", npc.typeId, npc.followMode,
                            vectorText(npc.position), vectorText(npc.rotation), npc.trackingRange,
                            npc.unknown12, npc.scriptId));
    line("npc stats", std::format("{} {} {} {} {} {} {} {} {} {} {}", npc.hp, npc.mp, npc.currentHp,
                                  npc.currentMp, npc.off, npc.def, npc.speed, npc.brain, npc.bits,
                                  npc.chargeMode, npc.unknown2A));
    line("npc moves",
         listText(npc.moves) + "| " + listText(npc.movesPriority) + "| " + vectorText(npc.flee));
    std::string path = listText(npc.aiSections) + "|";
    for (const auto& point : npc.waypoints) {
      path += " " + vectorText(point) + ",";
    }
    line("npc path", path);
  }
  line("collision", listText(map.collision.values()));
  return text;
}

void checkCase(std::string_view name) {
  const std::filesystem::path folder{DWE_MAPPER_WRITTEN};
  const std::string base = readText(folder / "base.yaml");
  const std::array patches{MapPatchText{.pack = "written",
                                        .baseRights = false,
                                        .file = std::format("{}/patch.yaml", name),
                                        .text = readText(folder / name / "patch.yaml")}};
  const auto patched = dwe::assets::patchMapDocument(base, patches);
  REQUIRE_MESSAGE(patched.has_value(), errorOf(patched));
  const auto expected = dwe::assets::parseMapDocument(readText(folder / name / "expected.yaml"));
  REQUIRE_MESSAGE(expected.has_value(), errorOf(expected));
  CHECK(dump(*patched) == dump(*expected));
  CHECK(patched->patches == std::vector<std::string>{"written"});
}

} // namespace

TEST_CASE("the engine reads the editor's base map as the editor does") {
  const auto base = dwe::assets::parseMapDocument(
      readText(std::filesystem::path{DWE_MAPPER_WRITTEN} / "base.yaml"));
  REQUIRE_MESSAGE(base.has_value(), errorOf(base));
  CHECK(base->npcs.size() == 2);
  CHECK(base->warps.size() == 10);
}

TEST_CASE("a patch of cell rectangles, exits and NPC fields patches as the editor expects") {
  checkCase("cells");
}

TEST_CASE("a patch with a whole grid patches as the editor expects") {
  checkCase("grid");
}
