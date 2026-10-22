#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/core/yaml.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "editor_documents.hpp"
#include "map_documents.hpp"
#include "script_listing_check.hpp"
#include "yaml_edits.hpp"

// R4.7: the map editor (dw1fe) opens the engine's documents, edits them and writes them back with
// the rules of emitYaml, with no conversion of its own
// (docs/architecture/herramientas-externas.md). These tests make the edits it makes on synthetic
// documents of each kind it writes.

using namespace dwe::dwx::testing;

TEST_CASE("a map the editor opens and saves keeps every byte, and an edit changes only its value") {
  const std::string original = extractedMap();
  CHECK(savedUnchanged(original) == original);
  REQUIRE_MESSAGE(dwe::assets::parseMapDocument(original).has_value(),
                  errorOf(dwe::assets::parseMapDocument(original)));

  const std::string moved = movedSpawn(original);
  CHECK(moved != original);
  const auto map = dwe::assets::parseMapDocument(moved);
  REQUIRE_MESSAGE(map.has_value(), errorOf(map));
  CHECK(map->warps[0].spawn.x == firstSpawnXOf(original) + SpawnShift);
  CHECK(emitted(withScalar(parsed(moved), firstSpawnX(),
                           std::to_string(firstSpawnXOf(original)))) == original);
}

TEST_CASE(
    "the map table the editor writes whole keeps every byte and its edit, and the engine refuses a wrong key") {
  // The fixture is hand-written; what the editor reads is the engine's own text.
  const std::string table = savedUnchanged(dwe::assets::testing::FixtureTable);
  CHECK(savedUnchanged(table) == table);
  REQUIRE_MESSAGE(dwe::assets::parseMapTable(table).has_value(),
                  errorOf(dwe::assets::parseMapTable(table)));

  const auto edited = dwe::assets::parseMapTable(withoutFirstToilet(table));
  REQUIRE_MESSAGE(edited.has_value(), errorOf(edited));
  CHECK_FALSE(edited->maps[0].toilet.has_value());
  checkMapTableRefusesWrongKeys(table);
}

TEST_CASE(
    "a script listing the editor opens and saves keeps every byte, and an edit changes only its value") {
  const std::string original = extractedScript();
  CHECK(savedUnchanged(original) == original);
  CHECK(listingProblems(parsed(original)).empty());

  const YamlPath text = firstText(parsed(original)).value_or(YamlPath{});
  REQUIRE_FALSE(text.empty());
  const std::string before = nodeAt(parsed(original), text).text();
  const std::string edited = withFirstTextRewritten(original, "Hi, it's me!\nB");
  CHECK(edited.contains("text: \"Hi, it's me!\\nB\""));
  CHECK(listingProblems(parsed(edited)).empty());
  CHECK(emitted(withScalar(parsed(edited), text, before)) == original);

  // The jump of the synthetic script, at its third instruction, goes to l_0018; l_0016 is inside
  // the textbox.
  const std::string jumpless =
      emitted(withScalar(parsed(original), {"instructions", "2", "target"}, "l_0016"));
  CHECK(listingProblems(parsed(jumpless)) ==
        std::vector<std::string>{"a jump to l_0016, which no instruction has",
                                 "the label l_0018 that nothing jumps to"});
}
