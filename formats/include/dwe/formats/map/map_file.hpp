#pragma once

#include <dwe/core/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The .MAP file of a field map (map.c: loadMap): a table of u32 offsets to its sections, in this
// order: setup, the 8-bit TIMs, the 4-bit TIMs, the sprite objects (only when the map has TIMs),
// the entities and the collision grid. Field names follow the decomp; fields it does not name are
// unknownXX, with XX their byte offset in the record.
namespace dwe::formats::map {

// GsF_LIGHT as the setup stores it: direction and colour (map.c: loadMapSetup).
struct MapLight {
  std::int32_t vx = 0;
  std::int32_t vy = 0;
  std::int32_t vz = 0;
  std::int32_t r = 0;
  std::int32_t g = 0;
  std::int32_t b = 0;

  bool operator==(const MapLight&) const = default;
};

// The setup section, all int32 (map.c: loadMapSetup).
struct MapSetup {
  // GsRVIEW2 viewpoint and reference point. The game doubles each value when it applies them.
  std::int32_t vpx = 0;
  std::int32_t vpy = 0;
  std::int32_t vpz = 0;
  std::int32_t vrx = 0;
  std::int32_t vry = 0;
  std::int32_t vrz = 0;
  std::array<MapLight, 3> lights{};
  // Read and ignored by the game, which always sets the ambient to 0x800 for each channel.
  std::int32_t ambientR = 0;
  std::int32_t ambientG = 0;
  std::int32_t ambientB = 0;
  std::int32_t projection = 0; // GsSetProjection distance
  // Compared with RAISE_DATA[partner].favoredRegion: a match in A or B sets the partner's response
  // to the area.
  std::array<std::int32_t, 4> regionA{};
  std::array<std::int32_t, 4> regionB{};
  std::int32_t width = 0;          // background tiles across
  std::int32_t height = 0;         // background tiles down
  std::vector<std::int32_t> tiles; // width * height, row by row; -1 = no tile

  bool operator==(const MapSetup&) const = default;
};

// A TIM section, left as a byte range of the file for the image reader. Both kinds are standard
// TIMs.
struct MapImageSection {
  std::uint8_t bitsPerPixel = 8; // 8 (overworld.c: loadMapImage1) or 4 (loadMapImage2)
  std::size_t offset = 0;
  std::size_t size = 0;

  bool operator==(const MapImageSection&) const = default;
};

// A sprite of the objects section: a rectangle of the map's textures drawn over the background
// (overworld.c: loadMapObjects, buildMapOverlayPrim). The file stores every field as int16.
struct MapSprite {
  std::int16_t texX = 0;
  std::int16_t texY = 0;
  std::int16_t width = 0;
  std::int16_t height = 0;
  std::int16_t someX = 0; // world position; it orders the sprite against the 3D scene
  std::int16_t someY = 0;
  std::int16_t someZ = 0;
  std::int16_t clut =
      0; // -1 = the map's 8-bit palette; 0-15 = a 4-bit CLUT; 16 or more = 8-bit row clut + 468
  std::int16_t transparency = 0;

  bool operator==(const MapSprite&) const = default;
};

// An animated instance of sprites (map.h: LocalMapObjectInstance).
struct MapObjectInstance {
  std::array<std::int16_t, 8> animSprites{}; // sprite per frame; -1 ends the list, -2 = blank frame
  std::array<std::int16_t, 8> animTimes{};   // ticks per frame
  std::int16_t x = 0;                        // position in background pixels
  std::int16_t y = 0;
  std::int16_t flag = 0; // 1 = hidden, 0x80 = drawn in screen space

  bool operator==(const MapObjectInstance&) const = default;
};

struct MapObjects {
  std::vector<MapSprite> sprites;
  std::vector<MapObjectInstance> instances;

  bool operator==(const MapObjects&) const = default;
};

// tamer.c: MapWarps. Exit i: where the player appears when entering through it, and where it leads.
struct MapWarps {
  std::array<std::int16_t, 10> spawnX{};
  std::array<std::int16_t, 10> spawnY{};
  std::array<std::int16_t, 10> spawnZ{};
  std::array<std::int16_t, 10> rotation{}; // 4096 = a full turn
  std::array<std::int16_t, 10> targetMap{};
  std::array<std::int16_t, 10> targetExit{};

  bool operator==(const MapWarps&) const = default;
};

struct MapWaypoint {
  std::int16_t x = 0;
  std::int16_t y = 0;
  std::int16_t z = 0;

  bool operator==(const MapWaypoint&) const = default;
};

// An NPC record, all int16 (overworld.c: loadMapDigimon).
struct MapNpc {
  std::int16_t typeId = 0;
  std::int16_t followMode = 0;
  std::int16_t posX = 0;
  std::int16_t posY = 0;
  std::int16_t posZ = 0;
  std::int16_t rotX = 0;
  std::int16_t rotY = 0;
  std::int16_t rotZ = 0;
  std::int16_t trackingRange = 0;
  std::int16_t unknown12 = 0; // NPCEntity.unk2
  std::int16_t scriptId = 0;
  std::int16_t hp = 0;
  std::int16_t mp = 0;
  std::int16_t currentHP = 0;
  std::int16_t currentMP = 0;
  std::int16_t off = 0;
  std::int16_t def = 0;
  std::int16_t speed = 0;
  std::int16_t brain = 0;
  std::int16_t bits = 0;
  std::int16_t chargeMode = 0;
  std::int16_t unknown2A = 0; // NPCEntity.unk1
  std::array<std::int16_t, 4> moves{};
  std::array<std::int16_t, 4> movesPrio{};
  std::int16_t fleeX = 0;
  std::int16_t fleeY = 0;
  std::int16_t fleeZ = 0;
  std::array<std::int16_t, 8> aiSections{};
  std::vector<MapWaypoint> waypoints;

  bool operator==(const MapNpc&) const = default;
};

struct MapEntities {
  MapWarps warps;
  std::int16_t digimonCount = 0; // as stored, even when the map has no NPC records
  std::vector<MapNpc> npcs;      // empty unless the map entry has the NPC flag

  bool operator==(const MapEntities&) const = default;
};

// The collision grid: 100 x 100 cells of 100 world units (map_collision.c). Bit 0x80 = blocked; the
// low 7 bits, when not zero, are a trigger id.
inline constexpr std::size_t CollisionSize = 100;

struct MapFile {
  MapSetup setup;
  std::vector<MapImageSection> images;
  std::optional<MapObjects> objects; // only in maps with TIMs
  MapEntities entities;
  std::vector<std::uint8_t> collision; // CollisionSize * CollisionSize, row by row

  bool operator==(const MapFile&) const = default;
};

// What the reader needs from the map's MAP_ENTRIES record.
struct MapReadOptions {
  int imageCount8 = 0;  // num8bppImages
  int imageCount4 = 0;  // num4bppImages
  bool hasNpcs = false; // flags & 0x80

  bool operator==(const MapReadOptions&) const = default;
};

// Reads a .MAP file. Every offset, count and record is checked against the file and against the
// game's fixed arrays (35 tiles, 188 instances, 8 NPCs, 8 waypoints).
core::Result<MapFile> readMapFile(std::span<const std::byte> file, const MapReadOptions& options);

// What a .MAP file says about its own layout: the first offset gives the size of the table of
// offsets and so the number of sections, and each image section starts with a TIM whose mode says 8
// or 4 bits (the 8-bit ones come first).
struct MapFileShape {
  int imageCount8 = 0;
  int imageCount4 = 0;
  std::size_t entitiesSize = 0; // bytes in the entities section

  bool operator==(const MapFileShape&) const = default;
};

// The entities section of a map without NPC records: six lists of 10 warps and the NPC count, all
// s16.
inline constexpr std::size_t EntitiesWithoutNpcs = ((std::size_t{6} * 10) + 1) * 2;

core::Result<MapFileShape> readMapFileShape(std::span<const std::byte> file);

// Works out the options of a .MAP file that no MAP_ENTRIES record names. The image counts come from
// its shape. Whether the game reads NPC records only shows when the entities section ends after the
// warps: some maps without the NPC flag keep bytes there that the game never reads, so a longer
// section cannot be told apart and is an error.
core::Result<MapReadOptions> inferMapReadOptions(std::span<const std::byte> file);

// The collision value of the cell under an exit's spawn, as the game reads the player's cell there
// (map_collision.c: getTileTrigger: column x / 100 + 50 and row 50 - z / 100 with C's division, one
// less for a negative x and for a positive z). Nothing for an exit past the ten or a spawn off the
// grid.
std::optional<std::uint8_t> collisionUnderSpawn(const MapFile& map, std::size_t exit);

// Builds the disc path of a map's file (map.c: buildMapPath): \MAP\MAP<mapId / 15 +
// 1>\<name><extension>.
std::string mapFilePath(int mapId, std::string_view filename, std::string_view extension);

} // namespace dwe::formats::map
