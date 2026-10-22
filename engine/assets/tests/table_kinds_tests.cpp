#include <dwe/assets/content_check.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/table_kinds.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using dwe::assets::MemoryFileSystem;
using dwe::assets::VirtualFileSystem;

namespace {

VirtualFileSystem mounted(const MemoryFileSystem& memory,
                          const std::vector<std::filesystem::path>& roots) {
  auto vfs = VirtualFileSystem::mount(memory, roots);
  REQUIRE_MESSAGE(vfs.has_value(), vfs.error().message);
  return std::move(*vfs);
}

// The role of the kind of a table of the packs, or nothing when it is no content table.
std::string_view roleOf(const VirtualFileSystem& vfs, std::string_view id) {
  const auto tables = dwe::assets::composedTablesOf(vfs);
  const auto found =
      std::ranges::find(tables, id, [](const auto& table) { return table.first.str(); });
  return found == tables.end() ? std::string_view{} : found->second->role;
}

} // namespace

TEST_CASE(
    "the content tables of the packs are each kind's table and the ones a ruleset names by the kind's role") {
  MemoryFileSystem memory;
  memory.addFile("packs/base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  memory.addFile("packs/base/data/raise.yaml", "raise: []\n");
  memory.addFile("packs/base/data/hard_raise.yaml", "raise: []\n");
  memory.addFile("packs/base/data/shops.yaml", "shop_maps: []\n");
  memory.addFile(
      "packs/base/rules/hard/ruleset.yaml",
      "id: hard\ntables:\n  raise: data/hard_raise\n  shops: data/shops\n  moves: data/missing\n");
  const VirtualFileSystem vfs = mounted(memory, {"packs/base"});

  CHECK(roleOf(vfs, "data/raise") == "raise");
  CHECK(roleOf(vfs, "data/hard_raise") == "raise");
  CHECK(roleOf(vfs, "data/shops").empty()); // no content table: replaced whole
  CHECK(roleOf(vfs, "data/missing").empty());
  CHECK(dwe::assets::composedTablesOf(vfs).size() == 2);
}

TEST_CASE(
    "the content check composes a table a ruleset names and reads it with its kind's reader") {
  MemoryFileSystem memory;
  memory.addFile("packs/base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  memory.addFile("packs/base/data/hard_raise.yaml", "raise: []\n");
  memory.addFile("packs/base/rules/hard/ruleset.yaml",
                 "id: hard\ntables:\n  raise: data/hard_raise\n");
  memory.addFile("packs/my_mod/pack.yaml", "id: my_mod\nversion: 1.0.0\nlayer: mods\n");
  // A raise row with a key the reader does not know: only the reader of the kind refuses it.
  memory.addFile("packs/my_mod/data/hard_raise.yaml",
                 "raise:\n- digimon: my_mod:pebble\n  colour: grey\n");
  const VirtualFileSystem vfs = mounted(memory, {"packs/base", "packs/my_mod"});

  const auto problems = dwe::assets::checkContent(vfs);
  REQUIRE(problems.size() == 1);
  CHECK(problems.front().id.str() == "data/hard_raise");
  CHECK(problems.front().message.contains("colour"));
}

TEST_CASE("the content check refuses a Digimon a pack adds without its model contract") {
  MemoryFileSystem memory;
  memory.addFile("packs/base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  memory.addFile("packs/base/data/digimon.yaml", "digimon: []\n");
  memory.addFile("packs/my_mod/pack.yaml", "id: my_mod\nversion: 1.0.0\nlayer: mods\n");
  memory.addFile("packs/my_mod/data/digimon.yaml", R"(digimon:
- id: my_mod:pebble
  name: "Pebble"
  file_name: null
  bone_count: 1
  radius: 100
  height: 200
  type: 1
  level: 3
  special: [0, null, null]
  drop_item: null
  drop_chance: 0
  moves: [null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null]
)");
  const auto problems = dwe::assets::checkContent(mounted(memory, {"packs/base", "packs/my_mod"}));
  REQUIRE(problems.size() == 1);
  CHECK(problems.front().message.contains("digimon/my_mod/pebble/contract"));

  memory.addFile("packs/my_mod/digimon/my_mod/pebble/contract.yaml", "model: model.glb\n");
  CHECK(dwe::assets::checkContent(mounted(memory, {"packs/base", "packs/my_mod"})).empty());
}
