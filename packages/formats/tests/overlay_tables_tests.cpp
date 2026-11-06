#include <dwe/core/yaml.hpp>
#include <dwe/formats/exe/memory_image.hpp>
#include <dwe/formats/exe/overlay_tables.hpp>
#include <dwe/formats/exe/overlay_yaml.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "exe_fixture.hpp"
#include "schema_check.hpp"

using dwe::core::YamlNode;
using dwe::formats::exe::BattleLayout;
using dwe::formats::exe::BattleStageLayout;
using dwe::formats::exe::battleStageToYaml;
using dwe::formats::exe::battleToYaml;
using dwe::formats::exe::findOverlayTablesLayout;
using dwe::formats::exe::FishingLayout;
using dwe::formats::exe::fishingToYaml;
using dwe::formats::exe::MemoryImage;
using dwe::formats::exe::Point16;
using dwe::formats::exe::readBattleStageTables;
using dwe::formats::exe::readBattleTables;
using dwe::formats::exe::readFishingTables;
using dwe::formats::exe::readTrainingTables;
using dwe::formats::exe::ShorelineLocation;
using dwe::formats::exe::TableIds;
using dwe::formats::exe::TableLocation;
using dwe::formats::exe::TrainingLayout;
using dwe::formats::exe::trainingToYaml;
using dwe::formats::test::checkAgainstSchema;
using dwe::formats::test::ImageBuilder;

namespace {

constexpr std::uint32_t OverlayBase = 0x80070000;
constexpr std::uint32_t WaterTestX = 0x80070100;
constexpr std::uint32_t WaterTestY = 0x80070200;

TableIds fixtureIds() {
  TableIds ids;
  ids.digimon = {"mon_a", "mon_b"};
  ids.moves = {"move_a", "move_b"};
  ids.items = {"item_a", "item_b", "item_c"};
  ids.maps = {"map_a", "map_b"};
  return ids;
}

std::uint32_t placePoints(ImageBuilder& image, const std::vector<Point16>& points) {
  const std::uint32_t address = image.place(points.size() * 4);
  for (std::size_t i = 0; i < points.size(); ++i) {
    image.i16(static_cast<std::uint32_t>(address + (i * 4)), points[i].x);
    image.i16(static_cast<std::uint32_t>(address + (i * 4) + 2), points[i].y);
  }
  return address;
}

const YamlNode& at(const YamlNode& node, std::string_view key) {
  const YamlNode* found = node.find(key);
  REQUIRE(found != nullptr);
  return *found;
}

// A fishing overlay with made-up tables: two bait chances, two depths of two kinds, one fish kind,
// two shorelines and one map of two spots.
struct FishingFixture {
  ImageBuilder image{OverlayBase};
  FishingLayout layout;

  FishingFixture() {
    layout.baitChances = {.address = image.place(4), .count = 2};
    image.u8(layout.baitChances.address, 1);
    image.u8(layout.baitChances.address + 1, 40);
    image.u8(layout.baitChances.address + 2, 9); // outside the items
    image.u8(layout.baitChances.address + 3, 99);
    layout.biteChances = {.address = image.place(5), .count = 5};
    for (std::uint32_t i = 0; i < 5; ++i) {
      image.u8(layout.biteChances.address + i, static_cast<std::uint8_t>(10 + i));
    }
    layout.biteKinds = 2;
    layout.biteRows = 2;
    layout.firstBiteDepth = 0x26;
    layout.kinds = {.address = image.place(0x18), .count = 1};
    image.u32(layout.kinds.address + 4, 50);
    image.u32(layout.kinds.address + 8, 80);
    image.i16(layout.kinds.address + 0x14, -1);
    layout.shorelines = {
        ShorelineLocation{
            .points = {.address = placePoints(image, {{.x = 1, .y = 2}, {.x = 3, .y = 4}}),
                       .count = 2},
            .axis = 'x',
            .waterTest = WaterTestX},
        ShorelineLocation{.points = {.address = placePoints(image, {{.x = 5, .y = 6}}), .count = 1},
                          .axis = 'y',
                          .waterTest = WaterTestY}};
    layout.spotsPerMap = 2;
    const std::uint32_t spots = image.place(0x20);
    for (std::uint32_t i = 0; i < 2; ++i) {
      const std::uint32_t spot = spots + (i * 0x10);
      image.u8(spot, static_cast<std::uint8_t>(i + 1));
      image.u32(spot + 8, placePoints(image, {{.x = -7, .y = 8}}));
      image.u8(spot + 0xC, 1);
    }
    const std::uint32_t sprite = image.string("\\DATA\\FISH.TIM");
    const std::uint32_t waypoints = placePoints(image, {{.x = 10, .y = 20}, {.x = 30, .y = 40}});
    layout.maps = {.address = image.place(0x38), .count = 1};
    const std::uint32_t map = layout.maps.address;
    image.i16(map, 1);
    image.u32(map + 4, 4096);
    image.i16(map + 0x14, 1700);
    image.i16(map + 0x16, 9999);
    image.u32(map + 0x18, spots);
    image.u32(map + 0x1C, 2);
    image.u32(map + 0x20, WaterTestY);
    image.i16(map + 0x28, 300);
    image.u32(map + 0x2C, sprite);
    image.u32(map + 0x30, waypoints);
    image.u32(map + 0x34, 2);
  }
};

} // namespace

TEST_CASE("the fishing tables are read field by field and converted with references as ids") {
  FishingFixture fixture;
  const MemoryImage image = MemoryImage::fromOverlay(fixture.image.text(), OverlayBase);
  const auto tables = readFishingTables(image, fixture.layout);
  REQUIRE(tables.has_value());
  REQUIRE(tables->baitChances.size() == 2);
  CHECK(tables->baitChances[1].threshold == 99);
  REQUIRE(tables->biteChances.size() == 2);
  CHECK(tables->biteChances[1] == std::vector<std::int8_t>{12, 13});
  CHECK(tables->unusedBiteBytes == std::vector<std::int8_t>{14});
  REQUIRE(tables->kinds.size() == 1);
  CHECK(tables->kinds[0].sizeMax == 80);
  CHECK(tables->kinds[0].bait == -1);
  REQUIRE(tables->shorelines.size() == 2);
  CHECK(tables->shorelines[0].points.size() == 2);
  REQUIRE(tables->maps.size() == 1);
  const auto& map = tables->maps[0];
  CHECK(map.mapId == 1);
  CHECK(map.seadramonTimes[0] == 1700);
  REQUIRE(map.spots.size() == 2);
  CHECK(map.spots[1].unknown0[0] == 2);
  CHECK(map.spots[1].points == std::vector<Point16>{{.x = -7, .y = 8}});
  CHECK(map.shoreline == std::size_t{1});
  CHECK(map.castRange == 300);
  CHECK(map.spriteFile == "\\DATA\\FISH.TIM");
  CHECK(map.seadramonWaypoints.size() == 2);

  const auto yaml = fishingToYaml(*tables, fixtureIds());
  REQUIRE(yaml.documents.size() == 1);
  CHECK(yaml.documents[0].name == "fishing");
  const YamlNode& document = yaml.documents[0].document;
  CHECK(checkAgainstSchema(document, "fishing").empty());
  CHECK(at(document, "bait_chances").items()[0].find("item")->text() == "item_b");
  CHECK(at(document, "bait_chances").items()[1].find("item")->text() == "9");
  CHECK(at(document, "kinds").items()[0].find("bait")->text() == "null");
  CHECK(at(document, "maps").items()[0].find("map")->text() == "map_b");
  REQUIRE(yaml.notes.size() == 1);
  CHECK(yaml.notes[0].contains("FISH_BAIT_CHANCES[1]"));
}

TEST_CASE("a fishing map whose water test is no known shoreline keeps a null shoreline") {
  FishingFixture fixture;
  fixture.image.u32(fixture.layout.maps.address + 0x20, 0x80070300);
  const auto tables = readFishingTables(MemoryImage::fromOverlay(fixture.image.text(), OverlayBase),
                                        fixture.layout);
  REQUIRE(tables.has_value());
  CHECK_FALSE(tables->maps[0].shoreline.has_value());
  const auto yaml = fishingToYaml(*tables, fixtureIds());
  CHECK(checkAgainstSchema(yaml.documents[0].document, "fishing").empty());
}

TEST_CASE("fishing tables that point outside the overlay are errors") {
  FishingFixture fixture;
  fixture.image.u32(fixture.layout.maps.address + 0x2C, 0x80090000);
  CHECK_FALSE(
      readFishingTables(MemoryImage::fromOverlay(fixture.image.text(), OverlayBase), fixture.layout)
          .has_value());
  FishingFixture tooMany;
  tooMany.image.u32(tooMany.layout.maps.address + 0x34, 1000);
  CHECK_FALSE(
      readFishingTables(MemoryImage::fromOverlay(tooMany.image.text(), OverlayBase), tooMany.layout)
          .has_value());
}

namespace {

// A battle overlay with three arenas, the last without files, and the executable that holds their
// counts.
struct BattleFixture {
  ImageBuilder overlay{OverlayBase};
  ImageBuilder executable{0x80100000};
  BattleStageLayout layout;

  BattleFixture() {
    layout.opponentBaseStats = {.address = overlay.place(4), .count = 2};
    overlay.i16(layout.opponentBaseStats.address, 100);
    overlay.i16(layout.opponentBaseStats.address + 2, -3);
    layout.grades = {.address = overlay.stringTable({"\\G\\A.TIM", "\\G\\B.TIM"}), .count = 2};
    layout.winChances = {.address = overlay.place(6), .count = 2};
    overlay.u8(layout.winChances.address + 5, 77);
    layout.cameraPresets = {.address = overlay.place(0x18), .count = 2};
    overlay.i16(layout.cameraPresets.address + 0xC, -400);
    layout.arenaModels = paths({"\\A\\ONE.TMD", "\\A\\TWO.TMD"});
    layout.arenaTextures = paths({"\\A\\ONE.TIM", "\\A\\TWO.TIM"});
    layout.arenaCollisions = paths({"\\A\\ONE.ATR", "\\A\\ONE.ATR"});
    layout.arenaModelCounts = {.address = executable.place(3), .count = 3};
    layout.arenaTextureCounts = {.address = executable.place(3), .count = 3};
    executable.u8(layout.arenaModelCounts.address + 1, 4);
    executable.u8(layout.arenaTextureCounts.address + 1, 2);
    layout.bracketRowY = {.address = overlay.place(4), .count = 2};
    overlay.i16(layout.bracketRowY.address + 2, -25);
    layout.bracketHopY = {.address = overlay.place(2), .count = 2};
    overlay.u8(layout.bracketHopY.address + 1, 0x1a);
    layout.bracketPaths = {.address = overlay.place(48), .count = 2};
    overlay.i16(layout.bracketPaths.address + 24 + 2, -110);
    overlay.u8(layout.bracketPaths.address + 24 + 16 + 3, 1);
    layout.spritePalettes = {.address = overlay.place(3), .count = 3};
    overlay.u8(layout.spritePalettes.address + 2, 6);
    layout.projectileIcons = {.address = overlay.place(3), .count = 3};
    overlay.u8(layout.projectileIcons.address + 1, 13);
    layout.projectilePalettes = {.address = overlay.place(2), .count = 2};
    overlay.u8(layout.projectilePalettes.address, 3);
  }

  // Three pointers: two strings and a null.
  TableLocation paths(std::initializer_list<std::string_view> texts) {
    std::vector<std::uint32_t> pointers;
    for (const std::string_view text : texts) {
      pointers.push_back(overlay.string(text));
    }
    const std::uint32_t table = overlay.place(12);
    for (std::size_t i = 0; i < pointers.size(); ++i) {
      overlay.u32(static_cast<std::uint32_t>(table + (i * 4)), pointers[i]);
    }
    return {.address = table, .count = 3};
  }

  [[nodiscard]] std::vector<std::byte> executableFile() const { return executable.executable(); }
};

} // namespace

TEST_CASE("the tables of a battle overlay are read, with the arena counts from the executable") {
  BattleFixture fixture;
  const auto exeFile = fixture.executableFile();
  const auto exe = MemoryImage::fromExecutable(exeFile);
  REQUIRE(exe.has_value());
  const MemoryImage overlay = MemoryImage::fromOverlay(fixture.overlay.text(), OverlayBase);
  const auto tables = readBattleStageTables(overlay, *exe, fixture.layout);
  REQUIRE(tables.has_value());
  CHECK(tables->opponentBaseStats == std::vector<std::int16_t>{100, -3});
  CHECK(tables->grades == std::vector<std::string>{"\\G\\A.TIM", "\\G\\B.TIM"});
  REQUIRE(tables->winChances.size() == 2);
  CHECK(tables->winChances[1][2] == 77);
  REQUIRE(tables->cameraPresets.size() == 2);
  CHECK(tables->cameraPresets[1].values[0] == -400);
  REQUIRE(tables->arenas.size() == 2);
  CHECK(tables->arenas[1].index == 1);
  CHECK(tables->arenas[1].model == "\\A\\TWO.TMD");
  CHECK(tables->arenas[1].collision == "\\A\\ONE.ATR");
  CHECK(tables->arenas[1].modelCount == 4);
  CHECK(tables->arenas[1].textureCount == 2);
  CHECK(tables->bracket.rowY == std::vector<std::int16_t>{0, -25});
  CHECK(tables->bracket.hopY == std::vector<std::int8_t>{0, 0x1a});
  REQUIRE(tables->bracket.paths.size() == 2);
  CHECK(tables->bracket.paths[1].x[1] == -110);
  CHECK(tables->bracket.paths[1].flip[3] == 1);
  CHECK(tables->bracket.spritePalettes == std::vector<std::uint8_t>{0, 0, 6});
  CHECK(tables->bracket.projectileIcons[1] == 13);
  CHECK(tables->bracket.projectilePalettes == std::vector<std::uint8_t>{3, 0});

  const auto stadium = battleStageToYaml(*tables, "stadium");
  REQUIRE(stadium.documents.size() == 1);
  CHECK(stadium.documents[0].name == "stadium");
  CHECK(checkAgainstSchema(stadium.documents[0].document, "stadium").empty());

  // The versus overlay has only cameras and arenas.
  BattleStageLayout versus = fixture.layout;
  versus.opponentBaseStats = {};
  versus.grades = {};
  versus.winChances = {};
  versus.bracketPaths = {};
  const auto versusTables = readBattleStageTables(overlay, *exe, versus);
  REQUIRE(versusTables.has_value());
  CHECK(versusTables->opponentBaseStats.empty());
  const auto yaml = battleStageToYaml(*versusTables, "versus");
  CHECK(yaml.documents[0].document.find("grades") == nullptr);
  CHECK(yaml.documents[0].document.find("bracket") == nullptr);
  CHECK(stadium.documents[0].document.find("bracket") != nullptr);
  CHECK(checkAgainstSchema(yaml.documents[0].document, "versus").empty());
}

TEST_CASE("an arena that names some of its files and not others is an error") {
  BattleFixture fixture;
  fixture.overlay.u32(fixture.layout.arenaTextures.address + 4, 0);
  const auto exeFile = fixture.executableFile();
  const auto exe = MemoryImage::fromExecutable(exeFile);
  REQUIRE(exe.has_value());
  CHECK_FALSE(readBattleStageTables(MemoryImage::fromOverlay(fixture.overlay.text(), OverlayBase),
                                    *exe, fixture.layout)
                  .has_value());
  BattleFixture uncounted;
  uncounted.layout.arenaModelCounts.count = 1;
  const auto uncountedFile = uncounted.executableFile();
  const auto uncountedExe = MemoryImage::fromExecutable(uncountedFile);
  REQUIRE(uncountedExe.has_value());
  CHECK_FALSE(readBattleStageTables(MemoryImage::fromOverlay(uncounted.overlay.text(), OverlayBase),
                                    *uncountedExe, uncounted.layout)
                  .has_value());
}

namespace {

// A training overlay: two partners, two rows of learnable moves, two maps of spots.
struct TrainingFixture {
  ImageBuilder image{OverlayBase};
  TrainingLayout layout;
  std::uint32_t lastSpot = 0;

  TrainingFixture() {
    layout.animations = {.address = image.place(4), .count = 2};
    image.u8(layout.animations.address + 2, 46);
    image.u8(layout.animations.address + 3, 10);
    layout.learnableMoves = {.address = image.place(4), .count = 2};
    layout.learnableMovesWidth = 2;
    image.u8(layout.learnableMoves.address, 1);
    image.u8(layout.learnableMoves.address + 1, 0xFF);
    image.u8(layout.learnableMoves.address + 2, 0);
    image.u8(layout.learnableMoves.address + 3, 5); // outside the moves
    layout.learnWeights = {.address = image.place(6), .count = 2};
    layout.learnWeightsWidth = 3;
    image.u8(layout.learnWeights.address + 4, 0xFE);
    layout.slotReels = {.address = image.place(6), .count = 2};
    layout.slotReelWidth = 3;
    image.u8(layout.slotReels.address + 2, 7);
    layout.assistRolls = {.address = image.place(3), .count = 3};
    image.u8(layout.assistRolls.address + 1, 2);
    const std::uint32_t first = spots({{1, 0, 2, 0}, {3, 0, 4, 1}});
    const std::uint32_t second = spots({{5, 0, 6, 3}});
    layout.spots = {.address = image.place(32), .count = 4};
    image.u32(layout.spots.address, 0);
    image.u32(layout.spots.address + 4, first);
    image.u32(layout.spots.address + 8, 1);
    image.u32(layout.spots.address + 12, second);
    image.u32(layout.spots.address + 16, 0xFFFFFFFF);
  }

  // Places spots and the stat -1 that ends them.
  std::uint32_t spots(std::initializer_list<std::array<std::int16_t, 4>> values) {
    const std::uint32_t address = image.place((values.size() + 1) * 8);
    std::uint32_t at = address;
    for (const auto& value : values) {
      for (std::uint32_t i = 0; i < 4; ++i) {
        image.i16(at + (i * 2), value[i]);
      }
      at += 8;
    }
    image.i16(at + 6, -1);
    lastSpot = at;
    return address;
  }
};

} // namespace

TEST_CASE("the training tables are read and their spots end at the markers") {
  TrainingFixture fixture;
  const auto tables = readTrainingTables(
      MemoryImage::fromOverlay(fixture.image.text(), OverlayBase), fixture.layout);
  REQUIRE(tables.has_value());
  REQUIRE(tables->animations.size() == 2);
  CHECK(tables->animations[1].animation == 46);
  CHECK(tables->animations[1].hitFrame == 10);
  CHECK(tables->learnableMoves == std::vector<std::vector<std::uint8_t>>{{1, 0xFF}, {0, 5}});
  REQUIRE(tables->learnWeights.size() == 2);
  CHECK(tables->learnWeights[1][1] == -2);
  CHECK(tables->slotReels[0][2] == 7);
  CHECK(tables->assistRolls == std::vector<std::int8_t>{0, 2, 0});
  REQUIRE(tables->spots.size() == 2);
  CHECK(tables->spots[0].spots.size() == 2);
  CHECK(tables->spots[0].spots[1].stat == 1);
  CHECK(tables->spots[1].mapId == 1);
  CHECK(tables->spots[1].spots.size() == 1);

  const auto yaml = trainingToYaml(*tables, fixtureIds());
  REQUIRE(yaml.documents.size() == 1);
  const YamlNode& document = yaml.documents[0].document;
  CHECK(checkAgainstSchema(document, "training").empty());
  CHECK(at(document, "animations").items()[1].find("digimon")->text() == "mon_b");
  CHECK(at(document, "learnable_moves").items()[0].items()[0].text() == "move_b");
  CHECK(at(document, "learnable_moves").items()[0].items()[1].text() == "null");
  CHECK(at(document, "spots").items()[1].find("map")->text() == "map_b");
  REQUIRE(yaml.notes.size() == 1);

  // Without the learnable moves (TRN2_REL.BIN) the lists are empty and the document still matches.
  TrainingLayout second = fixture.layout;
  second.learnableMoves = {};
  second.learnWeights = {};
  const auto without =
      readTrainingTables(MemoryImage::fromOverlay(fixture.image.text(), OverlayBase), second);
  REQUIRE(without.has_value());
  CHECK(without->learnableMoves.empty());
  CHECK(checkAgainstSchema(trainingToYaml(*without, fixtureIds()).documents[0].document, "training")
            .empty());
}

TEST_CASE("training spots without their end markers are errors") {
  TrainingFixture noMapEnd;
  noMapEnd.layout.spots.count = 2;
  CHECK_FALSE(readTrainingTables(MemoryImage::fromOverlay(noMapEnd.image.text(), OverlayBase),
                                 noMapEnd.layout)
                  .has_value());
  TrainingFixture noSpotEnd;
  noSpotEnd.image.i16(noSpotEnd.lastSpot + 6, 0);
  CHECK_FALSE(readTrainingTables(MemoryImage::fromOverlay(noSpotEnd.image.text(), OverlayBase),
                                 noSpotEnd.layout)
                  .has_value());
}

TEST_CASE("the US overlay tables are located and other versions have none") {
  REQUIRE(findOverlayTablesLayout("us").has_value());
  const auto us = findOverlayTablesLayout("us").value_or(dwe::formats::exe::OverlayTablesLayout{});
  CHECK(us.fishing.maps.located());
  CHECK(us.stadium.grades.located());
  CHECK_FALSE(us.versus.grades.located());
  CHECK(us.training.learnableMoves.located());
  CHECK_FALSE(us.training2.learnableMoves.located());
  CHECK_FALSE(findOverlayTablesLayout("other").has_value());
}

TEST_CASE("the battle overlay's flattened shot sprites become the battle document") {
  // battle_effect.c: BTL_AURA_PROJECTILE_TYPES: records of 12 bytes, the texel column at byte 10.
  ImageBuilder image{OverlayBase};
  BattleLayout layout;
  layout.flattenedShotTypes = {.address = image.place(36), .count = 3};
  image.i16(layout.flattenedShotTypes.address + 10, -1);
  image.i16(layout.flattenedShotTypes.address + 22, 0x70);
  image.i16(layout.flattenedShotTypes.address + 34, 0x10);
  const auto tables = readBattleTables(MemoryImage::fromOverlay(image.text(), OverlayBase), layout);
  REQUIRE(tables.has_value());
  CHECK(tables->flattenedShotSprites == std::vector<std::int16_t>{-1, 0x70, 0x10});

  const auto yaml = battleToYaml(*tables, fixtureIds());
  REQUIRE(yaml.documents.size() == 1);
  CHECK(yaml.documents[0].name == "battle");
  const YamlNode& document = yaml.documents[0].document;
  CHECK(checkAgainstSchema(document, "battle").empty());
  // Type 0 has no sprite and is left out.
  REQUIRE(at(document, "flattened_shots").items().size() == 2);
  CHECK(at(document, "flattened_shots").items()[0].find("digimon")->text() == "mon_b");
  CHECK(at(document, "flattened_shots").items()[1].find("sprite")->text() == "16");

  CHECK_FALSE(readBattleTables(MemoryImage::fromOverlay(image.text(), OverlayBase), BattleLayout{})
                  .has_value());
}
