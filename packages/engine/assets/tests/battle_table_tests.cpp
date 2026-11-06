#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view TwoMoves = R"(moves:
- id: "spit_fire"
  index: 6
  name: "Test spit_fire"
  distance: 150000
  power: 60
  mp_cost: 12
  iframes: 2
  range: 1
  special: 0
  status: 0
  accuracy: 80
  status_chance: 0
  unknown_0d: 3
  unknown_0e: 0
  unknown_0f: 0
  learn_chances: [25, 16, 11]
- id: "giga_freeze"
  index: 60
  name: "Test giga_freeze"
  distance: 0
  power: 400
  mp_cost: 0
  iframes: 1
  range: 3
  special: 4
  status: 3
  accuracy: 100
  status_chance: 50
  unknown_0d: 0
  unknown_0e: 0
  unknown_0f: 0
)";

} // namespace

TEST_CASE("the move table reads the battle's fields: flags and the learn chances when present") {
  const auto table = dwe::assets::parseMoveTable(TwoMoves);
  REQUIRE_MESSAGE(table.has_value(), table.error().message);
  REQUIRE(table->entries().size() == 2);
  const dwe::assets::MoveEntry* spit = table->find("spit_fire");
  REQUIRE(spit != nullptr);
  CHECK(spit->distance == 150000);
  CHECK(spit->mpCost == 12);
  CHECK(spit->flags == 3);
  CHECK(spit->unknown0e == 0);
  CHECK(spit->learnChances == dwe::assets::LearnChances{25, 16, 11});
  const dwe::assets::MoveEntry* freeze = table->findByOriginalIndex(60);
  REQUIRE(freeze != nullptr);
  CHECK(freeze->id == "giga_freeze");
  CHECK_FALSE(freeze->learnChances.has_value());
  CHECK(table->find("nothing") == nullptr);
}

TEST_CASE("the move table refuses a repeated index, a short learn_chances and an unknown key") {
  std::string twice{TwoMoves};
  twice.replace(twice.find("index: 60"), std::string_view{"index: 60"}.size(), "index: 6");
  const auto repeated = dwe::assets::parseMoveTable(twice);
  REQUIRE_FALSE(repeated.has_value());
  CHECK(repeated.error().message.contains("repeats the index 6"));
  std::string shortChances{TwoMoves};
  shortChances.replace(shortChances.find("[25, 16, 11]"), std::string_view{"[25, 16, 11]"}.size(),
                       "[25, 16]");
  CHECK_FALSE(dwe::assets::parseMoveTable(shortChances).has_value());
  std::string unknown{TwoMoves};
  unknown.replace(unknown.find("unknown_0f: 0\n"), 0, "colour: 3\n  ");
  CHECK_FALSE(dwe::assets::parseMoveTable(unknown).has_value());
}

TEST_CASE(
    "the user's disc, extracted: every move has a battle range and every move a Digimon knows is listed") {
  const std::optional<std::filesystem::path> mountedContent = dwe::test_support::userContent();
  if (!mountedContent) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
    return;
  }
  const std::filesystem::path& content = *mountedContent;
  const dwe::assets::DiskFileSystem disk;
  const auto found = dwe::assets::findPacks(disk, content);
  REQUIRE_MESSAGE(found.has_value(), found.error().message);
  const auto opened = dwe::assets::openPacks(disk, found->packs, dwe::assets::PackOrigin::Engine);
  REQUIRE_MESSAGE(opened.has_value(), opened.error().message);
  const auto vfs = dwe::assets::VirtualFileSystem::mount(opened->locations);
  REQUIRE_MESSAGE(vfs.has_value(), vfs.error().message);
  const auto moves = dwe::assets::loadMoveTable(*vfs, *dwe::assets::AssetId::parse("data/moves"));
  REQUIRE_MESSAGE(moves.has_value(), moves.error().message);
  CHECK(moves->entries().size() >= 100);
  for (const dwe::assets::MoveEntry& move : moves->entries()) {
    CHECK(move.range >= 0);
    CHECK(move.range <= 4);
  }
  const auto digimon =
      dwe::assets::loadDigimonTable(*vfs, *dwe::assets::AssetId::parse("data/digimon"));
  REQUIRE_MESSAGE(digimon.has_value(), digimon.error().message);
  CHECK(digimon->digimon.size() >= 60);
  for (const dwe::assets::DigimonRow& entry : digimon->digimon) {
    for (const auto& move : entry.moves) {
      if (move.has_value()) {
        CHECK_MESSAGE(moves->find(*move) != nullptr, entry.id, " knows an unknown move ", *move);
      }
    }
  }
}
