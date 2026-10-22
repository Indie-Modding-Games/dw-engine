#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// The data of a field map, `map/<id>/data` (content/schemas/map.schema.json,
// docs/formats/mapas.md). Every number is kept as the extractor wrote it, in the original's axes
// and units; the world-space helpers below convert them, in one place
// (docs/architecture/world-space.md).

// The 3D camera of the map (map.c: loadMapSetup). The original doubles each coordinate when it
// applies them.
struct MapCameraSetup {
  glm::ivec3 viewpoint{};
  glm::ivec3 reference{};
  // The distance from the eye to the screen (GsSetProjection), in pixels of the 320 x 240 screen.
  std::int32_t projection = 0;
};

struct MapColor {
  std::int32_t r = 0;
  std::int32_t g = 0;
  std::int32_t b = 0;
};

struct MapLight {
  glm::ivec3 direction{};
  MapColor color;
};

// Compared with the partner's favored region.
struct MapRegions {
  // An unused slot of a list.
  static constexpr std::int32_t NoRegion = -1;
  std::array<std::int32_t, 4> a{};
  std::array<std::int32_t, 4> b{};
};

// The background's grid of 128 x 128 pixel tiles.
struct MapTiles {
  static constexpr std::int32_t NoTile = -1;
  std::int32_t width = 0;
  std::int32_t height = 0;
  // Row by row; NoTile where the grid is empty.
  std::vector<std::int32_t> cells;
};

// A TIM section of the .MAP file, by byte range.
struct MapImage {
  std::int32_t bitsPerPixel = 0;
  std::int64_t offset = 0;
  std::int64_t size = 0;
};

// A piece of the map's textures drawn over the background (overworld.c: loadMapObjects).
struct MapSprite {
  std::int32_t texX = 0;
  std::int32_t texY = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  glm::ivec3 position{};
  // -1 = the map palette; 0-15 = a 4-bit CLUT; 16 or more = the 8-bit CLUT row clut + 468.
  std::int32_t clut = 0;
  std::int32_t transparency = 0;
};

struct MapSpriteFrame {
  // An index into MapObjects::sprites, or BlankSprite.
  std::int32_t sprite = 0;
  std::int32_t time = 0;

  static constexpr std::int32_t BlankSprite = -2;
};

// An animated object over the background (LocalMapObjectInstance).
struct MapObjectInstance {
  std::vector<MapSpriteFrame> frames;
  // In pixels of the background, or of the screen with ScreenSpaceFlag.
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t flag = 0;

  static constexpr std::int32_t HiddenFlag = 0x01;
  static constexpr std::int32_t ScreenSpaceFlag = 0x80;
};

struct MapObjects {
  std::vector<MapSprite> sprites;
  std::vector<MapObjectInstance> instances;
};

// An exit of the map (tamer.c: MapWarps). Exit i says where the player appears when entering the
// map through it, and where the warp trigger 110 + i of this map's collision leads (tamer.c:
// checkMapInteraction).
struct MapWarp {
  std::int32_t exit = 0;
  glm::ivec3 spawn{};
  // 4096 = one turn.
  std::int32_t rotation = 0;
  // The raw map index; meaningless for unused exits.
  std::int32_t targetMap = 0;
  // The id of the target map in data/maps, or nothing when the raw index is outside the table. As
  // read; once resolveWarpTargets ran (map_warps.hpp), the map the warp leads to, or nothing when
  // it leads nowhere.
  std::optional<std::string> targetMapId;
  std::int32_t targetExit = 0;
};

// An NPC record (overworld.c: loadMapDigimon), raw.
struct MapNpc {
  std::int32_t typeId = 0;
  std::optional<std::string> digimon;
  std::int32_t followMode = 0;
  glm::ivec3 position{};
  glm::ivec3 rotation{};
  std::int32_t trackingRange = 0;
  std::int32_t unknown12 = 0;
  std::int32_t scriptId = 0;
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  std::int32_t currentHp = 0;
  std::int32_t currentMp = 0;
  std::int32_t off = 0;
  std::int32_t def = 0;
  std::int32_t speed = 0;
  std::int32_t brain = 0;
  std::int32_t bits = 0;
  std::int32_t chargeMode = 0;
  std::int32_t unknown2A = 0;
  std::array<std::int32_t, 4> moves{};
  std::array<std::int32_t, 4> movesPriority{};
  glm::ivec3 flee{};
  std::array<std::int32_t, 8> aiSections{};
  std::vector<glm::ivec3> waypoints;
};

// What a collision cell does (map_collision.c).
enum class CollisionKind : std::uint8_t {
  Free,    // 0
  Blocked, // bit 0x80
  Trigger, // any other value: the trigger id
};

// A cell of the collision grid: column along world +X, row along world +Z.
struct CollisionCell {
  std::int32_t column = 0;
  std::int32_t row = 0;

  friend bool operator==(const CollisionCell&, const CollisionCell&) = default;
};

// The 100 x 100 collision grid, one cell per 100 world units.
class MapCollision {
public:
  static constexpr std::int32_t Size = 100;
  static constexpr std::uint8_t BlockedBit = 0x80;

  // A grid of free cells.
  MapCollision();

  // A grid from Size * Size raw values, row by row. Returns nothing if the size is wrong.
  static std::optional<MapCollision> fromValues(std::vector<std::uint8_t> values);

  // Whether the cell is inside the grid.
  [[nodiscard]] static bool contains(CollisionCell cell);

  // The raw value of a cell; cells outside the grid are blocked.
  [[nodiscard]] std::uint8_t value(CollisionCell cell) const;

  // What a cell does; cells outside the grid are blocked.
  [[nodiscard]] CollisionKind kind(CollisionCell cell) const;

  // Whether a cell stops movement (map_collision.c: checkMapCollisionX/Z test bit 0x80).
  [[nodiscard]] bool isBlocked(CollisionCell cell) const {
    return kind(cell) == CollisionKind::Blocked;
  }

  // The trigger id of a cell, or nothing for free and blocked cells.
  [[nodiscard]] std::optional<std::uint8_t> trigger(CollisionCell cell) const;

  // Changes a cell, for tests and scripts that close an area (map_collision.c: setImpassableRect).
  void setValue(CollisionCell cell, std::uint8_t value);

  // All the raw values, row by row.
  [[nodiscard]] const std::vector<std::uint8_t>& values() const { return values_; }

private:
  explicit MapCollision(std::vector<std::uint8_t> values) : values_(std::move(values)) {}

  // The index of a cell inside the grid in values_.
  static std::size_t indexOf(CollisionCell cell);

  std::vector<std::uint8_t> values_;
};

// A background image of the map: `map/<id>/background_<variant>`.
struct MapBackground {
  // day, dusk, night, then palette3, palette4...
  std::string variant;
  AssetId id;
};

// A texture page of the map: `map/<id>/texture8/<nnn>` or `texture4/<nnn>`, and its `<nnn>_meta`
// document.
struct MapTexture {
  std::int32_t bitsPerPixel = 0;
  std::int32_t index = 0;
  AssetId image;
  std::optional<AssetId> meta;
};

// The assets that come with a map's data in the virtual file system.
struct MapAssetIds {
  std::vector<MapBackground> backgrounds;
  std::vector<MapTexture> textures;

  // The background of a variant, or nullptr.
  [[nodiscard]] const AssetId* background(std::string_view variant) const;
};

struct MapDocument {
  // The map id (`twna01`); empty when parsed from text without a VFS.
  std::string id;
  MapCameraSetup camera;
  std::array<MapLight, 3> lights{};
  // Stored but ignored by the original.
  MapColor ambient;
  MapRegions regions;
  MapTiles tiles;
  std::vector<MapImage> images;
  // Nothing in maps without TIMs.
  std::optional<MapObjects> objects;
  std::vector<MapWarp> warps;
  std::int32_t digimonCount = 0;
  std::vector<MapNpc> npcs;
  MapCollision collision;
  MapAssetIds assets;

  // The exit with this number, or nullptr.
  [[nodiscard]] const MapWarp* warp(std::int32_t exit) const;
};

// Parses the text of `map/<id>/data`. Unknown and missing keys are errors.
core::Result<MapDocument> parseMapDocument(std::string_view yaml);

// The asset id of a map's data, `map/<id>/data`; a namespaced id is a folder per part,
// `map/<pack>/<name>/data` (ContentId::assetPath).
core::Result<AssetId> mapDataId(std::string_view mapId);

// Finds the backgrounds and textures of a map, sorted by variant order and by index.
MapAssetIds findMapAssets(const VirtualFileSystem& vfs, std::string_view mapId);

// Reads and parses a map's data from the virtual file system and fills in its id and assets.
core::Result<MapDocument> loadMapDocument(const VirtualFileSystem& vfs, std::string_view mapId);

// --- World space
// -----------------------------------------------------------------------------------------------------

// The side of a collision cell, in world units.
inline constexpr float CollisionCellSize = 100.0F;

// Where a character stands, on the ground, facing `yaw`.
struct SpawnPoint {
  glm::vec3 position{};
  float yaw = 0.0F;
};

// A position of the original's data (x, y down, z) in world space.
glm::vec3 toWorldPosition(glm::ivec3 original);

// The cell of the collision grid under a world position. The original computes column x / 100 + 50
// and row 50 - z / 100 in its own axes (map_collision.c: getTileTrigger, getModelTile), which in
// world space is floor(x / 100) + 50 and floor(z / 100) + 50. The original truncates and then
// corrects negative values, so on the exact lines x = -100 k and z = 100 k it picks the neighbour
// cell; this uses floor everywhere.
CollisionCell collisionCellAt(glm::vec3 world);

// The world-space corner of a cell with the lowest x and z.
glm::vec3 collisionCellMin(CollisionCell cell);

// The world-space centre of a cell, at height 0.
glm::vec3 collisionCellCenter(CollisionCell cell);

// The yaw in radians of an original Y rotation (4096 per turn). The original's Y axis points down,
// so the yaw turns the other way: yaw = -rotation * 2 pi / 4096. At yaw 0 a character faces world
// +Z, and forwardOfYaw gives its facing; in the original's axes rotation 0 faces -Z (tamer.c:
// tamerTickWalkingState, getMapRotation).
float yawFromOriginal(std::int32_t rotation);

// The original Y rotation nearest to a yaw, in 0..4095.
std::int32_t yawToOriginal(float yaw);

// The ground direction a character with this yaw faces: (sin yaw, 0, cos yaw).
glm::vec3 forwardOfYaw(float yaw);

// The yaw that faces a ground direction; 0 for a zero direction.
float yawOfDirection(glm::vec3 direction);

// Where the player appears when entering a map through an exit, and the way it faces (tamer.c:
// loadMapEntities).
SpawnPoint warpSpawn(const MapWarp& warp);

} // namespace dwe::assets
