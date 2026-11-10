#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/evolution_table.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/raise_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

// Made-up rows in the shape dwx extracts.
constexpr std::string_view MoveDocument = R"(moves:
- id: "spark"
  index: 0
  name: "Spark"
  distance: 9000
  power: 50
  mp_cost: 10
  iframes: 20
  range: 1
  special: 0
  status: 0
  accuracy: 90
  status_chance: 0
  unknown_0d: 0
  unknown_0e: 0
  unknown_0f: 0
- id: "basic"
  index: 1
  name: null
  distance: 0
  power: 0
  mp_cost: 0
  iframes: 0
  range: 0
  special: 0
  status: 0
  accuracy: 0
  status_chance: 0
  unknown_0d: 0
  unknown_0e: 0
  unknown_0f: 0
)";

constexpr std::string_view EvolutionDocument = R"(requirements:
- target: "beta"
  index: 1
  digimon: "alpha"
  hp: 1
  mp: -1
  offense: 1
  defense: -1
  speed: -1
  brain: -1
  care: 3
  weight: 15
  discipline: -1
  happiness: -1
  battles: -2
  techs: 0
  flags: 16
stats_gains:
- target: "beta"
  index: 1
  hp: 500
  mp: 400
  offense: 50
  defense: 40
  speed: 30
  brains: 10
  target_digimon: "beta"
paths:
- digimon: "alpha"
  index: 0
  from: [null, null, null, null, null]
  to: [null, "beta", "-2", null, null, null]
item_targets:
- item: "beta_charm"
  index: 71
  target: "beta"
)";

} // namespace

TEST_CASE("a move listed twice or a row without a key is an error with its line") {
  const std::string_view rows = MoveDocument.substr(MoveDocument.find('\n') + 1);
  CHECK(errorOf(dwe::assets::parseMoveTable(std::string{MoveDocument} + std::string{rows}))
            .contains("'spark' is listed twice"));
  CHECK(errorOf(dwe::assets::parseMoveTable("moves:\n- id: \"spark\"\n  index: 0\n"))
            .contains("line"));
}

TEST_CASE("the move table reads power and the optional name") {
  const auto table = dwe::assets::parseMoveTable(MoveDocument);
  REQUIRE_MESSAGE(table.has_value(), errorOf(table));
  REQUIRE(table->find("spark") != nullptr);
  CHECK(table->find("spark")->power == 50);
  CHECK(table->find("spark")->name == "Spark");
  CHECK_FALSE(table->findByOriginalIndex(1)->name);
}

TEST_CASE(
    "the evolution table reads requirements, stat rows, charts with raw references and item targets") {
  const auto table = dwe::assets::parseEvolutionTable(EvolutionDocument);
  REQUIRE_MESSAGE(table.has_value(), errorOf(table));
  REQUIRE(table->requirements.size() == 1);
  CHECK(table->requirements.front().digimon == "alpha");
  CHECK(table->requirements.front().battles == -2);
  CHECK(table->requirements.front().flags == 16);
  REQUIRE(table->statsGains.size() == 1);
  CHECK(table->statsGains.front().brains == 10);
  CHECK(table->statsGains.front().targetDigimon == "beta");
  REQUIRE(table->paths.size() == 1);
  CHECK(table->paths.front().to.at(1) == "beta");
  CHECK(table->paths.front().to.at(2) == "-2");
  REQUIRE(table->itemTargets.size() == 1);
  CHECK(table->itemTargets.front().item == "beta_charm");

  // The item targets are optional: dwx does not extract them yet.
  const std::string withoutItems{
      EvolutionDocument.substr(0, EvolutionDocument.find("item_targets:"))};
  const auto noItems = dwe::assets::parseEvolutionTable(withoutItems);
  REQUIRE_MESSAGE(noItems.has_value(), errorOf(noItems));
  CHECK(noItems->itemTargets.empty());
}

TEST_CASE("a species without rows borrows those of its namesake, then of its level and type") {
  using dwe::assets::DigimonRow;
  dwe::assets::DigimonTable table;
  const auto row = [](std::string id, std::string name, std::int32_t type, std::int32_t level) {
    DigimonRow made;
    made.id = std::move(id);
    made.name = std::move(name);
    made.type = type;
    made.level = level;
    return made;
  };
  table.digimon = {row("alpha", "Alpha", 1, 3),  row("beta", "Beta", 2, 4),
                   row("gamma", "Gamma", 1, 4),  row("enemy_beta", "Beta", 1, 4),
                   row("other", "Other", 2, 4),  row("lonely", "Lonely", 3, 5),
                   row("person", "Person", 0, 0)};
  const auto hasRows = [](const DigimonRow& candidate) {
    return candidate.id == "alpha" || candidate.id == "beta" || candidate.id == "gamma";
  };
  const auto standIn = [&](std::string_view id) {
    const DigimonRow* found = dwe::assets::standInRow(table, *table.find(id), hasRows);
    return found != nullptr ? found->id : std::string{"-"};
  };
  CHECK(standIn("beta") == "beta");
  // An enemy's copy of a partner form takes the form's rows, whatever its type.
  CHECK(standIn("enemy_beta") == "beta");
  // Then the first of the same level and type, then of the same level.
  CHECK(standIn("other") == "beta");
  table.digimon.at(4).type = 1;
  CHECK(standIn("other") == "gamma");
  // A level with no rows at all, or none (a character), takes a rookie's.
  CHECK(standIn("lonely") == "alpha");
  CHECK(standIn("person") == "alpha");
  CHECK(dwe::assets::standInRow(table, *table.find("person"),
                                [](const DigimonRow&) { return false; }) == nullptr);
}

// Runs only when DWE_CONTENT names a content folder (scripts/dev.sh mounts it).
TEST_CASE("the user's disc, extracted: the Digimon, raise, move and evolution tables read") {
  const std::optional<std::filesystem::path> mountedContent = dwe::test_support::userContent();
  if (!mountedContent) {
    MESSAGE("DWE_CONTENT is not set: skipping the tables on extracted content");
    return;
  }
  const std::filesystem::path& content = *mountedContent;
  const dwe::assets::DiskFileSystem disk;
  const auto found = dwe::assets::findPacks(disk, content);
  REQUIRE_MESSAGE(found.has_value(), errorOf(found));
  const auto opened = dwe::assets::openPacks(disk, found->packs, dwe::assets::PackOrigin::Engine);
  REQUIRE_MESSAGE(opened.has_value(), errorOf(opened));
  const auto vfs = dwe::assets::VirtualFileSystem::mount(opened->locations);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  const auto idOf = [](std::string_view text) {
    auto id = dwe::assets::AssetId::parse(text);
    REQUIRE_MESSAGE(id.has_value(), errorOf(id));
    return *id;
  };
  const auto digimon = dwe::assets::loadDigimonTable(*vfs, idOf("data/digimon"));
  REQUIRE_MESSAGE(digimon.has_value(), errorOf(digimon));
  const auto raise = dwe::assets::loadRaiseTable(*vfs, idOf("data/raise"));
  REQUIRE_MESSAGE(raise.has_value(), errorOf(raise));
  const auto moves = dwe::assets::loadMoveTable(*vfs, idOf("data/moves"));
  REQUIRE_MESSAGE(moves.has_value(), errorOf(moves));
  const auto evolution = dwe::assets::loadEvolutionTable(*vfs, idOf("data/evolution"));
  REQUIRE_MESSAGE(evolution.has_value(), errorOf(evolution));
  CHECK(digimon->digimon.size() > 60);
  CHECK(raise->raise.size() > 60);
  CHECK(moves->entries().size() > 100);
  CHECK(evolution->requirements.size() > 60);
  CHECK(evolution->paths.size() > 60);
  // Every form a chart names is in the Digimon table, or a raw number dwx could not resolve.
  for (const dwe::assets::EvolutionPathEntry& path : evolution->paths) {
    for (const dwe::assets::TableReference& target : path.to) {
      CHECK_MESSAGE((!target || digimon->find(*target) != nullptr || target->front() == '-'),
                    target.value_or("null"));
    }
  }
}
