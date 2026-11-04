#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/yaml.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "minimap_enemies.hpp"

namespace dwe::app {
namespace {

// A map script's listing as dwx writes it (script/dg/<n>): section 5 fights at once, 6 only talks,
// 7 fights after a choice, 8 calls a routine that fights, 9 jumps past a battle it never reaches,
// and 10 fights only when a trigger is set.
constexpr std::string_view Listing = R"yaml(
code_start: 0x002c
sections:
- id: 0x0005
  at: l_0010
- id: 0x0006
  at: l_0020
- id: 0x0007
  at: l_0030
- id: 0x0008
  at: l_0040
- id: 0x0009
  at: l_0050
- id: 0x000a
  at: l_0060
instructions:
- at: 0x0010
  label: l_0010
  op: show_textbox
  text: "l_0099"
- at: 0x0012
  op: start_battle
  enemy: 0
- at: 0x0014
  op: end_section
- at: 0x0020
  label: l_0020
  op: show_textbox
  text: "Hello"
- at: 0x0022
  op: end_section
- at: 0x0024
  label: l_0024
  op: start_battle
  enemy: 0
- at: 0x0026
  op: end_section
- at: 0x0030
  label: l_0030
  op: set_selection
  options:
  - l_0034
  - l_0024
  text: "Yes\nNo"
- at: 0x0034
  label: l_0034
  op: end_section
- at: 0x0040
  label: l_0040
  op: jump_and_link
  target: l_0070
- at: 0x0042
  op: end_section
- at: 0x0050
  label: l_0050
  op: jump_to
  target: l_0034
- at: 0x0052
  op: start_battle
  enemy: 0
- at: 0x0060
  label: l_0060
  op: if
  conditions:
  - test: trigger
    trigger: 12
  - test: goto_if_false
    target: l_0034
- at: 0x0066
  op: jump_to
  target: l_0024
- at: 0x0070
  label: l_0070
  op: start_battle
  enemy: 0
- at: 0x0072
  op: jump_return
)yaml";

core::YamlNode listing() {
  auto parsed = core::parseYaml(Listing);
  REQUIRE_MESSAGE(parsed.has_value(), parsed.error().message);
  return *parsed;
}

assets::MapNpc npc(std::int32_t scriptId) {
  assets::MapNpc record;
  record.scriptId = scriptId;
  return record;
}

} // namespace

TEST_CASE("the sections that can reach a battle are the ones whose NPCs fight") {
  // Through a choice, a call and a branch the battle is reached; a section that only talks, or
  // jumps over its battle, does not fight.
  CHECK(fightingSections(listing()) == std::set<std::int32_t>{5, 7, 8, 10});
  CHECK(fightingSections(core::YamlNode::mapping()).empty());
}

TEST_CASE("the minimap marks the NPCs on the map that fight, where they stand") {
  const std::vector<assets::MapNpc> records{npc(5), npc(6), npc(7), npc(8), npc(5)};
  const std::set<std::int32_t> fighting = fightingSections(listing());
  // 5 and 6 stand on the map; 7 was beaten and is gone; 8 was never put there.
  const std::map<std::int32_t, glm::vec3> standing{{5, {300.0F, 0.0F, -200.0F}},
                                                   {6, {100.0F, 0.0F, 100.0F}}};
  const NpcPosition positionOf = [&](std::int32_t scriptId) -> std::optional<glm::vec3> {
    const auto found = standing.find(scriptId);
    return found != standing.end() ? std::optional{found->second} : std::nullopt;
  };
  const std::vector<ui::MinimapMarker> enemies =
      minimapEnemies(true, records, fighting, positionOf);
  // Only 5, once though two records share its script id; 6 is a friend that only talks.
  REQUIRE(enemies.size() == 1);
  CHECK(enemies.front().x == doctest::Approx(300.0F));
  CHECK(enemies.front().z == doctest::Approx(-200.0F));

  // It moves: the next frame reads where it stands now.
  const NpcPosition moved = [](std::int32_t scriptId) -> std::optional<glm::vec3> {
    return scriptId == 5 ? std::optional{glm::vec3{350.0F, 0.0F, -150.0F}} : std::nullopt;
  };
  const std::vector<ui::MinimapMarker> later = minimapEnemies(true, records, fighting, moved);
  REQUIRE(later.size() == 1);
  CHECK(later.front().x == doctest::Approx(350.0F));
  CHECK(later.front().z == doctest::Approx(-150.0F));

  // With the option off the minimap marks nobody, as before it existed.
  CHECK(minimapEnemies(false, records, fighting, positionOf).empty());
}

TEST_CASE("the finder reads the active script's listing once and none without a script") {
  assets::MemoryFileSystem memory;
  memory.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
  memory.addFile("base/script/dg/038.yaml", std::string{Listing});
  const std::array<std::filesystem::path, 1> roots{"base"};
  auto files = assets::VirtualFileSystem::mount(memory, roots, assets::PackOrigin::Engine);
  REQUIRE(files.has_value());
  MinimapEnemyFinder finder{*files};
  CHECK(finder.sectionsOf(38) == std::set<std::int32_t>{5, 7, 8, 10});
  CHECK(finder.sectionsOf(std::nullopt).empty());
  // A script without a listing marks nobody.
  CHECK(finder.sectionsOf(39).empty());
}

} // namespace dwe::app
