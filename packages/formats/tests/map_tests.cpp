#include <dwe/core/yaml.hpp>
#include <dwe/formats/map/map_file.hpp>
#include <dwe/formats/map/map_yaml.hpp>
#include <dwe/formats/map/tfs_file.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "map_fixtures.hpp"
#include "schema_check.hpp"

using dwe::core::YamlNode;
using dwe::formats::map::backgroundVariantName;
using dwe::formats::map::composeBackgrounds;
using dwe::formats::map::inferMapReadOptions;
using dwe::formats::map::mapFilePath;
using dwe::formats::map::MapFileShape;
using dwe::formats::map::MapReadOptions;
using dwe::formats::map::mapToYaml;
using dwe::formats::map::MapYamlIds;
using dwe::formats::map::readMapFile;
using dwe::formats::map::readMapFileShape;
using dwe::formats::map::readTfsFile;
using dwe::formats::test::checkAgainstSchema;
using dwe::formats::test::entitiesSection;
using dwe::formats::test::makeMap;
using dwe::formats::test::makeTfs;
using dwe::formats::test::MapParts;
using dwe::formats::test::setupSection;
using dwe::formats::test::Writer;

namespace {

constexpr MapReadOptions FullMap{.imageCount8 = 1, .imageCount4 = 1, .hasNpcs = true};

// The start of a TIM: magic and flags (mode in the low 3 bits), then some bytes the inference does
// not read.
std::vector<std::byte> timStart(std::uint32_t flags) {
  Writer w;
  w.i32(0x10);
  w.i32(flags);
  w.fill(8, 0);
  return w.bytes();
}

MapParts partsWithTims(std::uint32_t firstFlags, std::uint32_t secondFlags) {
  MapParts parts;
  parts.image8 = timStart(firstFlags);
  parts.image4 = timStart(secondFlags);
  return parts;
}

} // namespace

TEST_CASE("a map with every section is read") {
  const auto file = makeMap();
  const auto map = readMapFile(file, FullMap);
  REQUIRE_MESSAGE(map.has_value(), map.error().message);

  CHECK(map->setup.vpx == 100);
  CHECK(map->setup.vrz == 105);
  CHECK(map->setup.lights[2].vy == 2);
  CHECK(map->setup.lights[0].r == 128);
  CHECK(map->setup.ambientB == 3);
  CHECK(map->setup.projection == 512);
  CHECK(map->setup.regionB[3] == 7);
  CHECK(map->setup.tiles == std::vector<std::int32_t>{0, -1});

  REQUIRE(map->images.size() == 2);
  CHECK(map->images[0].bitsPerPixel == 8);
  CHECK(map->images[0].size == 16);
  CHECK(map->images[1].bitsPerPixel == 4);
  CHECK(map->images[1].offset == map->images[0].offset + 16);

  REQUIRE(map->objects.has_value());
  const auto objects = map->objects.value_or(dwe::formats::map::MapObjects{});
  REQUIRE(objects.sprites.size() == 1);
  CHECK(objects.sprites[0].texX == 256);
  CHECK(objects.sprites[0].someZ == 30);
  CHECK(objects.sprites[0].clut == -1);
  REQUIRE(objects.instances.size() == 1);
  CHECK(objects.instances[0].animTimes[1] == 6);
  CHECK(objects.instances[0].flag == 1);

  CHECK(map->entities.warps.spawnX[3] == 3);
  CHECK(map->entities.warps.rotation[9] == 309);
  CHECK(map->entities.warps.targetMap[0] == 1);
  CHECK(map->entities.warps.targetExit[1] == -1);
  REQUIRE(map->entities.npcs.size() == 1);
  const auto& npc = map->entities.npcs[0];
  CHECK(npc.typeId == 2);
  CHECK(npc.followMode == 1);
  CHECK(npc.scriptId == 10);
  CHECK(npc.unknown2A == 21);
  CHECK(npc.moves[0] == 22);
  CHECK(npc.fleeZ == 32);
  CHECK(npc.aiSections[7] == -1);
  REQUIRE(npc.waypoints.size() == 2);
  CHECK(npc.waypoints[1].z == 50);

  REQUIRE(map->collision.size() == 10000);
  CHECK(map->collision[0] == 0x80);
  CHECK(map->collision[101] == 51);
}

TEST_CASE("a map without images has no objects section and skips NPC records without the flag") {
  Writer w;
  const auto setup = setupSection({});
  auto entities = entitiesSection({});
  const std::size_t headerSize = 12;
  w.i32(static_cast<std::int64_t>(headerSize));
  w.i32(static_cast<std::int64_t>(headerSize + setup.size()));
  w.i32(static_cast<std::int64_t>(headerSize + setup.size() + entities.size()));
  w.append(setup);
  w.append(entities);
  w.fill(10000, 0);

  const auto map = readMapFile(w.bytes(), {});
  REQUIRE_MESSAGE(map.has_value(), map.error().message);
  CHECK_FALSE(map->objects.has_value());
  CHECK(map->images.empty());
  CHECK(map->entities.digimonCount == 1);
  CHECK(map->entities.npcs.empty());
}

TEST_CASE("a map file gives its image counts and the size of its entities") {
  const auto withNpcs = readMapFileShape(makeMap(partsWithTims(0x9, 0x8)));
  REQUIRE_MESSAGE(withNpcs.has_value(), withNpcs.error().message);
  CHECK(withNpcs->imageCount8 == 1);
  CHECK(withNpcs->imageCount4 == 1);
  CHECK(withNpcs->entitiesSize > dwe::formats::map::EntitiesWithoutNpcs);

  const auto twoEightBit = readMapFileShape(makeMap(partsWithTims(0x1, 0x9)));
  REQUIRE(twoEightBit.has_value());
  CHECK(twoEightBit->imageCount8 == 2);
  CHECK(twoEightBit->imageCount4 == 0);
}

TEST_CASE("a map without NPC data gets its options from the file and reads the same way") {
  MapParts quiet = partsWithTims(0x9, 0x8);
  quiet.npcCount = 0;
  const auto file = makeMap(quiet);
  const auto options = inferMapReadOptions(file);
  REQUIRE_MESSAGE(options.has_value(), options.error().message);
  CHECK(*options == MapReadOptions{.imageCount8 = 1, .imageCount4 = 1, .hasNpcs = false});
  const auto inferred = readMapFile(file, *options);
  const auto given = readMapFile(file, FullMap);
  REQUIRE(inferred.has_value());
  REQUIRE(given.has_value());
  CHECK(*inferred == *given);

  // With bytes after the warps, the file cannot say whether the game reads them as NPCs.
  const auto unclear = inferMapReadOptions(makeMap(partsWithTims(0x9, 0x8)));
  REQUIRE_FALSE(unclear.has_value());
  CHECK(unclear.error().message.contains("past its warps"));
}

TEST_CASE("files whose options cannot be worked out are errors") {
  CHECK_FALSE(readMapFileShape(makeMap(partsWithTims(0x8, 0x9))).has_value()); // 8-bit after 4-bit
  CHECK_FALSE(readMapFileShape(makeMap(partsWithTims(0x9, 0x2))).has_value()); // 16-bit
  CHECK_FALSE(readMapFileShape(makeMap()).has_value());                        // no TIM magic
  Writer fourSections;
  for (const int offset : {16, 16, 16, 16}) {
    fourSections.i32(offset);
  }
  CHECK_FALSE(readMapFileShape(fourSections.bytes()).has_value());
  Writer odd;
  odd.i32(6);
  odd.fill(8, 0);
  CHECK_FALSE(readMapFileShape(odd.bytes()).has_value());
  CHECK_FALSE(readMapFileShape({}).has_value());
}

TEST_CASE("malformed maps are errors") {
  SUBCASE("an offset past the end") {
    auto file = makeMap();
    Writer header;
    header.append(file);
    header.setI32(16, static_cast<std::int64_t>(file.size() + 1));
    CHECK_FALSE(readMapFile(header.bytes(), FullMap).has_value());
  }
  SUBCASE("offsets out of order") {
    Writer header;
    header.append(makeMap());
    header.setI32(4, 0x1000);
    CHECK_FALSE(readMapFile(header.bytes(), FullMap).has_value());
  }
  SUBCASE("a file shorter than its table of offsets") {
    CHECK_FALSE(readMapFile(std::vector<std::byte>(8), FullMap).has_value());
  }
  SUBCASE("image counts that do not match the file") {
    CHECK_FALSE(
        readMapFile(makeMap(), {.imageCount8 = 3, .imageCount4 = 1, .hasNpcs = true}).has_value());
    CHECK_FALSE(readMapFile(makeMap(), {.imageCount8 = -1}).has_value());
  }
  SUBCASE("a tile grid larger than the game's") {
    MapParts parts;
    parts.gridWidth = 6;
    parts.gridHeight = 6;
    parts.tiles.assign(36, 0);
    CHECK_FALSE(readMapFile(makeMap(parts), FullMap).has_value());
  }
  SUBCASE("a setup that ends inside the grid") {
    MapParts parts;
    parts.gridWidth = 3;
    CHECK_FALSE(readMapFile(makeMap(parts), FullMap).has_value());
  }
  SUBCASE("too many sprites, instances, NPCs or waypoints") {
    MapParts parts;
    SUBCASE("sprites") {
      parts.spriteCount = -1;
    }
    SUBCASE("instances") {
      parts.instanceCount = -1;
    }
    SUBCASE("npcs") {
      parts.npcCount = 9;
    }
    SUBCASE("waypoints") {
      parts.waypointCount = 9;
    }
    CHECK_FALSE(readMapFile(makeMap(parts), FullMap).has_value());
  }
  SUBCASE("records that run past their section") {
    MapParts parts;
    SUBCASE("sprites") {
      parts.spriteCount = 5;
    }
    SUBCASE("npcs") {
      parts.npcCount = 3;
    }
    CHECK_FALSE(readMapFile(makeMap(parts), FullMap).has_value());
  }
  SUBCASE("a collision grid of the wrong size") {
    MapParts parts;
    parts.collisionSize = 9999;
    CHECK_FALSE(readMapFile(makeMap(parts), FullMap).has_value());
  }
}

TEST_CASE("a map path follows the folder of its id") {
  CHECK(mapFilePath(0, "START", ".MAP") == "\\MAP\\MAP1\\START.MAP");
  CHECK(mapFilePath(149, "ABC", ".TFS") == "\\MAP\\MAP10\\ABC.TFS");
}

TEST_CASE("the cell under an exit's spawn is the one the game reads the player on") {
  using dwe::formats::map::CollisionSize;
  dwe::formats::map::MapFile map;
  map.collision.assign(CollisionSize * CollisionSize, 0);
  const auto put = [&](std::size_t column, std::size_t row, std::uint8_t value) {
    map.collision.at((row * CollisionSize) + column) = value;
  };
  constexpr std::uint8_t Step = 54;
  constexpr std::uint8_t Other = 66;
  constexpr std::uint8_t Corner = 70;
  // Exit 9 at (266, -2130): column 2 + 50, row 50 + 21.
  map.entities.warps.spawnX.at(9) = 266;
  map.entities.warps.spawnZ.at(9) = -2130;
  put(52, 71, Step);
  // Exit 0 at (-50, 50): one less on both axes than C's division gives, so column 49 and row 49.
  map.entities.warps.spawnX.at(0) = -50;
  map.entities.warps.spawnZ.at(0) = 50;
  put(49, 49, Other);
  put(50, 50, Corner);
  CHECK(dwe::formats::map::collisionUnderSpawn(map, 9) == Step);
  CHECK(dwe::formats::map::collisionUnderSpawn(map, 0) == Other);
  // An exit at the origin reads cell (50, 50); one past the ten, or a spawn off the grid, reads
  // nothing.
  CHECK(dwe::formats::map::collisionUnderSpawn(map, 1) == Corner);
  CHECK_FALSE(dwe::formats::map::collisionUnderSpawn(map, 10).has_value());
  map.entities.warps.spawnX.at(2) = 6000;
  CHECK_FALSE(dwe::formats::map::collisionUnderSpawn(map, 2).has_value());
}

TEST_CASE("a TFS file is read and laid out on the grid once per palette") {
  const auto tfs = readTfsFile(makeTfs(3, 1));
  REQUIRE_MESSAGE(tfs.has_value(), tfs.error().message);
  CHECK(tfs->unknown00 == 0x00800100);
  REQUIRE(tfs->palettes.size() == 3);
  CHECK(tfs->palettes[1][5] == 1005);
  REQUIRE(tfs->tiles.size() == 1);

  const auto map = readMapFile(makeMap(), FullMap);
  REQUIRE(map.has_value());
  const auto backgrounds = composeBackgrounds(*tfs, map->setup);
  REQUIRE_MESSAGE(backgrounds.has_value(), backgrounds.error().message);
  REQUIRE(backgrounds->size() == 3);
  const auto& day = (*backgrounds)[0];
  CHECK(day.width == 256);
  CHECK(day.height == 128);
  CHECK(day.indices.size() == std::size_t{256} * 128);
  CHECK(day.indices[(3 * 256) + 7] == 7);       // inside the tile
  CHECK(day.indices[(3 * 256) + 128 + 7] == 0); // the empty cell
  CHECK((*backgrounds)[2].palette[0] == 2000);
  CHECK((*backgrounds)[2].indices == day.indices);
  CHECK(backgroundVariantName(0) == "day");
  CHECK(backgroundVariantName(2) == "night");
  CHECK(backgroundVariantName(4) == "palette4");
}

TEST_CASE("malformed TFS files and grids that need missing tiles are errors") {
  CHECK_FALSE(readTfsFile(std::vector<std::byte>(4)).has_value());
  CHECK_FALSE(readTfsFile(makeTfs(32, 0)).has_value());
  auto partial = makeTfs(1, 1);
  partial.pop_back();
  CHECK_FALSE(readTfsFile(partial).has_value());
  auto shortPalettes = makeTfs(2, 0);
  shortPalettes.resize(8 + 512);
  CHECK_FALSE(readTfsFile(shortPalettes).has_value());

  const auto tfs = readTfsFile(makeTfs(1, 1));
  REQUIRE(tfs.has_value());
  dwe::formats::map::MapSetup setup;
  setup.width = 2;
  setup.height = 1;
  setup.tiles = {0, 1};
  CHECK_FALSE(composeBackgrounds(*tfs, setup).has_value());
  setup.tiles = {0};
  CHECK_FALSE(composeBackgrounds(*tfs, setup).has_value());
}

TEST_CASE("a map becomes a YAML document that matches its schema") {
  const auto map = readMapFile(makeMap(), FullMap);
  REQUIRE(map.has_value());
  const std::vector<std::string> mapIds = {"start", "town"};
  const std::vector<std::string> digimonIds = {"a", "b", "c"};
  const YamlNode document = mapToYaml(*map, MapYamlIds{.maps = mapIds, .digimon = digimonIds});

  CHECK(checkAgainstSchema(document, "map").empty());
  CHECK(document.find("tiles")->find("rows")->items()[0].text() == "0 -1");
  const auto& warps = document.find("warps")->items();
  CHECK(warps[0].find("target_map_id")->text() == "town");
  CHECK(warps[1].find("target_map_id")->text() == "null");
  CHECK(document.find("npcs")->items()[0].find("digimon")->text() == "c");
  const auto& frames =
      document.find("objects")->find("instances")->items()[0].find("frames")->items();
  CHECK(frames.size() == 2);

  const YamlNode* collision = document.find("collision");
  CHECK(collision->find("encoding")->text() == "legend");
  const auto& rows = collision->find("rows")->items();
  REQUIRE(rows.size() == 100);
  CHECK(rows[0].text().size() == 100);
  CHECK(rows[0].text().starts_with("#."));
  CHECK(rows[1].text().starts_with(".A"));
  CHECK(rows[99].text().ends_with("B"));
  CHECK(collision->find("legend")->items().size() == 4);

  const auto first = dwe::core::emitYaml(document);
  const auto second = dwe::core::emitYaml(mapToYaml(*map, {.maps = mapIds, .digimon = digimonIds}));
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  CHECK(*first == *second);
}

TEST_CASE("a collision grid with more values than symbols is written in hex") {
  auto map = readMapFile(makeMap(), FullMap);
  REQUIRE(map.has_value());
  for (std::size_t i = 0; i < map->collision.size(); ++i) {
    map->collision[i] = static_cast<std::uint8_t>(i % 100);
  }
  const YamlNode document = mapToYaml(*map);
  const YamlNode* collision = document.find("collision");
  CHECK(collision->find("encoding")->text() == "hex");
  CHECK(collision->find("rows")->items()[0].text().size() == 200);
  CHECK(collision->find("rows")->items()[0].text().starts_with("000102"));
  CHECK(checkAgainstSchema(document, "map").empty());
}
