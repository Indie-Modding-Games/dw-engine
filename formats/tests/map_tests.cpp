#include <dwe/core/yaml.hpp>
#include <dwe/formats/map/map_file.hpp>
#include <dwe/formats/map/map_yaml.hpp>
#include <dwe/formats/map/tfs_file.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "schema_check.hpp"

using dwe::core::YamlNode;
using dwe::formats::map::backgroundVariantName;
using dwe::formats::map::composeBackgrounds;
using dwe::formats::map::mapFilePath;
using dwe::formats::map::MapReadOptions;
using dwe::formats::map::mapToYaml;
using dwe::formats::map::MapYamlIds;
using dwe::formats::map::readMapFile;
using dwe::formats::map::readTfsFile;
using dwe::formats::test::checkAgainstSchema;

namespace {

// Appends little-endian fields.
class Writer {
public:
    void i16(std::int64_t value) { put(static_cast<std::uint32_t>(value), 2); }
    void i32(std::int64_t value) { put(static_cast<std::uint32_t>(value), 4); }
    void u8(std::uint8_t value) { bytes_.push_back(std::byte{value}); }
    void fill(std::size_t count, std::uint8_t value) { bytes_.insert(bytes_.end(), count, std::byte{value}); }
    void append(const std::vector<std::byte>& other) { bytes_.insert(bytes_.end(), other.begin(), other.end()); }
    void setI32(std::size_t offset, std::int64_t value) {
        for (std::size_t i = 0; i < 4; ++i) {
            bytes_[offset + i] = static_cast<std::byte>((static_cast<std::uint32_t>(value) >> (8U * i)) & 0xFFU);
        }
    }
    [[nodiscard]] std::size_t size() const { return bytes_.size(); }
    [[nodiscard]] const std::vector<std::byte>& bytes() const { return bytes_; }

private:
    void put(std::uint32_t value, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            bytes_.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
        }
    }

    std::vector<std::byte> bytes_;
};

// The parts of a synthetic map, so that tests can break one of them.
struct MapParts {
    std::int32_t gridWidth = 2;
    std::int32_t gridHeight = 1;
    std::vector<std::int32_t> tiles = {0, -1};
    std::int16_t spriteCount = 1;
    std::int16_t instanceCount = 1;
    std::int16_t npcCount = 1;
    std::int16_t waypointCount = 2;
    std::size_t collisionSize = 10000;
};

std::vector<std::byte> setupSection(const MapParts& parts) {
    Writer w;
    for (int i = 0; i < 6; ++i) {
        w.i32(std::int64_t{100} + i); // camera
    }
    for (int light = 0; light < 3; ++light) {
        w.i32(-4096);
        w.i32(light);
        w.i32(0);
        w.i32(128);
        w.i32(64);
        w.i32(32);
    }
    w.i32(1);
    w.i32(2);
    w.i32(3); // ambient
    w.i32(512);
    for (int i = 0; i < 8; ++i) {
        w.i32(i); // regions
    }
    w.i32(parts.gridWidth);
    w.i32(parts.gridHeight);
    for (const std::int32_t tile : parts.tiles) {
        w.i32(tile);
    }
    return w.bytes();
}

std::vector<std::byte> objectsSection(const MapParts& parts) {
    Writer w;
    w.i16(parts.spriteCount);
    for (int i = 0; i < std::min<int>(parts.spriteCount, 2); ++i) {
        for (const int value : {256, 16, 32, 48, 10, 20, 30, -1, 4}) {
            w.i16(value);
        }
    }
    w.i16(parts.instanceCount);
    for (int i = 0; i < std::min<int>(parts.instanceCount, 2); ++i) {
        for (const int sprite : {0, 0, -1, 0, 0, 0, 0, 0}) {
            w.i16(sprite);
        }
        for (const int time : {5, 6, 0, 0, 0, 0, 0, 0}) {
            w.i16(time);
        }
        w.i16(64);
        w.i16(80);
        w.i16(1);
    }
    return w.bytes();
}

std::vector<std::byte> entitiesSection(const MapParts& parts) {
    Writer w;
    for (int field = 0; field < 6; ++field) {
        for (int exit = 0; exit < 10; ++exit) {
            if (field < 4) {
                w.i16((field * 100) + exit); // spawn and rotation
            } else if (exit == 0) {
                w.i16(field == 4 ? 1 : 0); // exit 0 leads to map 1, exit 0
            } else {
                w.i16(-1);
            }
        }
    }
    w.i16(parts.npcCount);
    for (int npc = 0; npc < std::min<int>(parts.npcCount, 2); ++npc) {
        w.i16(2); // typeId
        for (int i = 1; i < 33; ++i) {
            w.i16(i);
        }
        w.i16(parts.waypointCount);
        for (int i = 0; i < 8; ++i) {
            w.i16(-1); // aiSections
        }
        for (int i = 0; i < std::min<int>(parts.waypointCount, 2) * 3; ++i) {
            w.i16(std::int64_t{i} * 10);
        }
    }
    return w.bytes();
}

std::vector<std::byte> collisionSection(const MapParts& parts) {
    std::vector<std::byte> cells(parts.collisionSize, std::byte{0});
    if (cells.size() >= 10000) {
        cells[0] = std::byte{0x80};
        cells[101] = std::byte{51};
        cells[9999] = std::byte{120};
    }
    return cells;
}

// A map with one 8-bit TIM, one 4-bit TIM, objects, one NPC and the collision grid.
std::vector<std::byte> makeMap(const MapParts& parts = {}) {
    const std::vector<std::vector<std::byte>> sections = {
        setupSection(parts),
        std::vector<std::byte>(16, std::byte{0x10}),
        std::vector<std::byte>(8, std::byte{0x10}),
        objectsSection(parts),
        entitiesSection(parts),
        collisionSection(parts),
    };
    Writer w;
    std::size_t offset = sections.size() * 4;
    for (const auto& section : sections) {
        w.i32(static_cast<std::int64_t>(offset));
        offset += section.size();
    }
    for (const auto& section : sections) {
        w.append(section);
    }
    return w.bytes();
}

constexpr MapReadOptions FullMap{.imageCount8 = 1, .imageCount4 = 1, .hasNpcs = true};

std::vector<std::byte> makeTfs(std::uint32_t paletteCount, std::size_t tileCount) {
    Writer w;
    w.i32(0x00800100);
    w.i32(paletteCount);
    for (std::uint32_t palette = 0; palette < paletteCount; ++palette) {
        for (int colour = 0; colour < 256; ++colour) {
            w.i16((std::int64_t{palette} * 1000) + colour);
        }
    }
    for (std::size_t tile = 0; tile < tileCount; ++tile) {
        w.i32(0);
        for (std::size_t pixel = 0; pixel < std::size_t{128} * 128; ++pixel) {
            w.u8(static_cast<std::uint8_t>((pixel % 128) + tile));
        }
    }
    return w.bytes();
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
        CHECK_FALSE(readMapFile(makeMap(), {.imageCount8 = 3, .imageCount4 = 1, .hasNpcs = true}).has_value());
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
    const auto& frames = document.find("objects")->find("instances")->items()[0].find("frames")->items();
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
