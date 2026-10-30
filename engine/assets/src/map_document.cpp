#include <dwe/assets/content_id.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>
#include <system_error>
#include <utility>

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::size_t CellCount = static_cast<std::size_t>(MapCollision::Size) * MapCollision::Size;
constexpr std::int32_t GridOrigin = MapCollision::Size / 2;
constexpr std::size_t LightCount = 3;
constexpr std::size_t WarpCount = 10;

glm::ivec3 readVector(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a vector", {"x", "y", "z"})) {
    return {};
  }
  return {reader.integer<std::int32_t>(reader.field(node, "x")),
          reader.integer<std::int32_t>(reader.field(node, "y")),
          reader.integer<std::int32_t>(reader.field(node, "z"))};
}

MapColor readColor(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a color", {"r", "g", "b"})) {
    return {};
  }
  return {.r = reader.integer<std::int32_t>(reader.field(node, "r")),
          .g = reader.integer<std::int32_t>(reader.field(node, "g")),
          .b = reader.integer<std::int32_t>(reader.field(node, "b"))};
}

template <std::size_t N>
std::array<std::int32_t, N> readIntegers(YamlReader& reader, const YamlNode& node,
                                         std::string_view what) {
  std::array<std::int32_t, N> values{};
  const std::span<const YamlNode> items = reader.sequence(node, what, N);
  for (std::size_t i = 0; i < items.size(); ++i) {
    values.at(i) = reader.integer<std::int32_t>(items[i]);
  }
  return values;
}

MapCameraSetup readCamera(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "camera", {"viewpoint", "reference", "projection"})) {
    return {};
  }
  return {.viewpoint = readVector(reader, reader.field(node, "viewpoint")),
          .reference = readVector(reader, reader.field(node, "reference")),
          .projection = reader.integer<std::int32_t>(reader.field(node, "projection"))};
}

std::array<MapLight, 3> readLights(YamlReader& reader, const YamlNode& node) {
  std::array<MapLight, LightCount> lights{};
  const std::span<const YamlNode> items = reader.sequence(node, "lights", LightCount);
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (reader.expectKeys(items[i], "a light", {"direction", "color"})) {
      lights.at(i) = {.direction = readVector(reader, reader.field(items[i], "direction")),
                      .color = readColor(reader, reader.field(items[i], "color"))};
    }
  }
  return lights;
}

MapRegions readRegions(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "regions", {"a", "b"})) {
    return {};
  }
  return {.a = readIntegers<4>(reader, reader.field(node, "a"), "region a"),
          .b = readIntegers<4>(reader, reader.field(node, "b"), "region b")};
}

// Splits a row of space-separated numbers.
void readTileRow(YamlReader& reader, const YamlNode& node, std::int32_t width,
                 std::vector<std::int32_t>& cells) {
  const std::string text = reader.text(node);
  std::int32_t count = 0;
  const char* cursor = text.data();
  const char* const end = text.data() + text.size();
  while (reader.ok() && cursor < end) {
    std::int32_t value = 0;
    const auto [next, error] = std::from_chars(cursor, end, value);
    if (error != std::errc{} || (next != end && *next != ' ')) {
      reader.fail(node, "a tile row must be numbers separated by single spaces");
      return;
    }
    cells.push_back(value);
    ++count;
    cursor = next == end ? end : next + 1;
  }
  if (reader.ok() && count != width) {
    reader.fail(node, std::format("a tile row must have {} tiles, not {}", width, count));
  }
}

MapTiles readTiles(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "tiles", {"width", "height", "rows"})) {
    return {};
  }
  MapTiles tiles{.width = reader.integer<std::int32_t>(reader.field(node, "width")),
                 .height = reader.integer<std::int32_t>(reader.field(node, "height")),
                 .cells = {}};
  if (tiles.width < 0 || tiles.height < 0) {
    reader.fail(node, "the tile grid cannot have a negative size");
    return {};
  }
  const std::span<const YamlNode> rows = reader.sequence(reader.field(node, "rows"), "tile rows",
                                                         static_cast<std::size_t>(tiles.height));
  tiles.cells.reserve(static_cast<std::size_t>(tiles.width) *
                      static_cast<std::size_t>(tiles.height));
  for (const YamlNode& row : rows) {
    readTileRow(reader, row, tiles.width, tiles.cells);
  }
  return tiles;
}

std::vector<MapImage> readImages(YamlReader& reader, const YamlNode& node) {
  std::vector<MapImage> images;
  for (const YamlNode& item : reader.sequence(node, "images")) {
    if (reader.expectKeys(item, "an image", {"bits_per_pixel", "offset", "size"})) {
      images.push_back(
          {.bitsPerPixel = reader.integer<std::int32_t>(reader.field(item, "bits_per_pixel")),
           .offset = reader.integer<std::int64_t>(reader.field(item, "offset")),
           .size = reader.integer<std::int64_t>(reader.field(item, "size"))});
    }
  }
  return images;
}

MapSprite readSprite(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(
          node, "a sprite",
          {"tex_x", "tex_y", "width", "height", "position", "clut", "transparency"})) {
    return {};
  }
  return {.texX = reader.integer<std::int32_t>(reader.field(node, "tex_x")),
          .texY = reader.integer<std::int32_t>(reader.field(node, "tex_y")),
          .width = reader.integer<std::int32_t>(reader.field(node, "width")),
          .height = reader.integer<std::int32_t>(reader.field(node, "height")),
          .position = readVector(reader, reader.field(node, "position")),
          .clut = reader.integer<std::int32_t>(reader.field(node, "clut")),
          .transparency = reader.integer<std::int32_t>(reader.field(node, "transparency"))};
}

MapObjectInstance readInstance(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "an object instance", {"frames", "x", "y", "flag"})) {
    return {};
  }
  MapObjectInstance instance{.frames = {},
                             .x = reader.integer<std::int32_t>(reader.field(node, "x")),
                             .y = reader.integer<std::int32_t>(reader.field(node, "y")),
                             .flag = reader.integer<std::int32_t>(reader.field(node, "flag"))};
  for (const YamlNode& frame : reader.sequence(reader.field(node, "frames"), "frames")) {
    if (reader.expectKeys(frame, "a frame", {"sprite", "time"})) {
      instance.frames.push_back(
          {.sprite = reader.integer<std::int32_t>(reader.field(frame, "sprite")),
           .time = reader.integer<std::int32_t>(reader.field(frame, "time"))});
    }
  }
  return instance;
}

std::optional<MapObjects> readObjects(YamlReader& reader, const YamlNode& node) {
  if (YamlReader::isNull(node) || !reader.expectKeys(node, "objects", {"sprites", "instances"})) {
    return std::nullopt;
  }
  MapObjects objects;
  for (const YamlNode& item : reader.sequence(reader.field(node, "sprites"), "sprites")) {
    objects.sprites.push_back(readSprite(reader, item));
  }
  for (const YamlNode& item : reader.sequence(reader.field(node, "instances"), "instances")) {
    objects.instances.push_back(readInstance(reader, item));
  }
  return objects;
}

std::vector<MapWarp> readWarps(YamlReader& reader, const YamlNode& node) {
  std::vector<MapWarp> warps;
  for (const YamlNode& item : reader.sequence(node, "warps", WarpCount)) {
    // An editor that only knows the original's numbers may leave target_map_id out (map_warps.hpp).
    if (!reader.expectKeys(item, "a warp",
                           {"exit", "spawn", "rotation", "target_map", "target_exit"},
                           {"target_map_id"})) {
      break;
    }
    const YamlNode* targetId = item.find("target_map_id");
    warps.push_back(
        {.exit = reader.integer<std::int32_t>(reader.field(item, "exit")),
         .spawn = readVector(reader, reader.field(item, "spawn")),
         .rotation = reader.integer<std::int32_t>(reader.field(item, "rotation")),
         .targetMap = reader.integer<std::int32_t>(reader.field(item, "target_map")),
         .targetMapId = targetId != nullptr ? reader.optionalText(*targetId) : std::nullopt,
         .targetExit = reader.integer<std::int32_t>(reader.field(item, "target_exit"))});
  }
  return warps;
}

MapNpc readNpc(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "an NPC",
                         {"type_id",        "digimon",    "follow_mode", "position",   "rotation",
                          "tracking_range", "unknown_12", "script_id",   "hp",         "mp",
                          "current_hp",     "current_mp", "off",         "def",        "speed",
                          "brain",          "bits",       "charge_mode", "unknown_2a", "moves",
                          "moves_prio",     "flee",       "ai_sections", "waypoints"})) {
    return {};
  }
  const auto integer = [&](std::string_view key) {
    return reader.integer<std::int32_t>(reader.field(node, key));
  };
  MapNpc npc{
      .typeId = integer("type_id"),
      .digimon = reader.optionalText(reader.field(node, "digimon")),
      .followMode = integer("follow_mode"),
      .position = readVector(reader, reader.field(node, "position")),
      .rotation = readVector(reader, reader.field(node, "rotation")),
      .trackingRange = integer("tracking_range"),
      .unknown12 = integer("unknown_12"),
      .scriptId = integer("script_id"),
      .hp = integer("hp"),
      .mp = integer("mp"),
      .currentHp = integer("current_hp"),
      .currentMp = integer("current_mp"),
      .off = integer("off"),
      .def = integer("def"),
      .speed = integer("speed"),
      .brain = integer("brain"),
      .bits = integer("bits"),
      .chargeMode = integer("charge_mode"),
      .unknown2A = integer("unknown_2a"),
      .moves = readIntegers<4>(reader, reader.field(node, "moves"), "moves"),
      .movesPriority = readIntegers<4>(reader, reader.field(node, "moves_prio"), "moves_prio"),
      .flee = readVector(reader, reader.field(node, "flee")),
      .aiSections = readIntegers<8>(reader, reader.field(node, "ai_sections"), "ai_sections"),
      .waypoints = {}};
  for (const YamlNode& item : reader.sequence(reader.field(node, "waypoints"), "waypoints")) {
    npc.waypoints.push_back(readVector(reader, item));
  }
  return npc;
}

std::optional<std::uint8_t> hexDigit(char c) {
  if (c >= '0' && c <= '9') {
    return static_cast<std::uint8_t>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<std::uint8_t>(c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<std::uint8_t>(c - 'A' + 10);
  }
  return std::nullopt;
}

MapCollision readCollision(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "collision", {"width", "height", "encoding", "legend", "rows"})) {
    return {};
  }
  const auto width = reader.integer<std::int32_t>(reader.field(node, "width"));
  const auto height = reader.integer<std::int32_t>(reader.field(node, "height"));
  if (reader.ok() && (width != MapCollision::Size || height != MapCollision::Size)) {
    reader.fail(node, std::format("the collision grid must be {0} x {0}", MapCollision::Size));
  }
  const std::string encoding = reader.text(reader.field(node, "encoding"));
  const bool hex = encoding == "hex";
  if (reader.ok() && !hex && encoding != "legend") {
    reader.fail(reader.field(node, "encoding"), "the collision encoding must be 'legend' or 'hex'");
  }

  std::array<std::optional<std::uint8_t>, 256> legend{};
  for (const YamlNode& item : reader.sequence(reader.field(node, "legend"), "legend")) {
    if (!reader.expectKeys(item, "a legend entry", {"cell", "value"})) {
      break;
    }
    const std::string cell = reader.text(reader.field(item, "cell"));
    const auto value = reader.integer<std::uint8_t>(reader.field(item, "value"));
    if (reader.ok() && cell.size() != 1) {
      reader.fail(item, "a legend cell must be one character");
    }
    if (reader.ok()) {
      legend.at(static_cast<unsigned char>(cell.front())) = value;
    }
  }

  std::vector<std::uint8_t> values;
  values.reserve(CellCount);
  const std::size_t rowLength = hex ? 2 * MapCollision::Size : MapCollision::Size;
  for (const YamlNode& row :
       reader.sequence(reader.field(node, "rows"), "collision rows", MapCollision::Size)) {
    const std::string text = reader.text(row);
    if (reader.ok() && text.size() != rowLength) {
      reader.fail(row, std::format("a collision row must have {} characters, not {}", rowLength,
                                   text.size()));
    }
    for (std::size_t i = 0; reader.ok() && i < text.size(); i += hex ? 2 : 1) {
      std::optional<std::uint8_t> value;
      if (hex) {
        const auto high = hexDigit(text[i]);
        const auto low = hexDigit(text[i + 1]);
        if (high && low) {
          value = static_cast<std::uint8_t>((*high << 4U) | *low);
        }
      } else {
        value = legend.at(static_cast<unsigned char>(text[i]));
      }
      if (!value) {
        reader.fail(row, std::format("collision cell '{}' is not in the legend", text[i]));
        break;
      }
      values.push_back(*value);
    }
  }
  if (!reader.ok()) {
    return {};
  }
  return MapCollision::fromValues(std::move(values)).value_or(MapCollision{});
}

int variantOrder(std::string_view variant) {
  constexpr std::array<std::string_view, 3> Named = {"day", "dusk", "night"};
  for (std::size_t i = 0; i < Named.size(); ++i) {
    if (variant == Named.at(i)) {
      return static_cast<int>(i);
    }
  }
  int number = 0;
  constexpr std::string_view Palette = "palette";
  if (variant.starts_with(Palette)) {
    const std::string_view digits = variant.substr(Palette.size());
    std::from_chars(digits.data(), digits.data() + digits.size(), number);
  }
  return static_cast<int>(Named.size()) + number;
}

std::optional<std::int32_t> parseIndex(std::string_view text) {
  std::int32_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || text.empty()) {
    return std::nullopt;
  }
  return value;
}

} // namespace

std::size_t MapCollision::indexOf(CollisionCell cell) {
  return (static_cast<std::size_t>(cell.row) * static_cast<std::size_t>(Size)) +
         static_cast<std::size_t>(cell.column);
}

MapCollision::MapCollision() : values_(CellCount, 0) {}

std::optional<MapCollision> MapCollision::fromValues(std::vector<std::uint8_t> values) {
  if (values.size() != CellCount) {
    return std::nullopt;
  }
  return MapCollision{std::move(values)};
}

bool MapCollision::contains(CollisionCell cell) {
  return cell.column >= 0 && cell.column < Size && cell.row >= 0 && cell.row < Size;
}

std::uint8_t MapCollision::value(CollisionCell cell) const {
  if (!contains(cell)) {
    return BlockedBit;
  }
  return values_[indexOf(cell)];
}

CollisionKind MapCollision::kind(CollisionCell cell) const {
  const std::uint8_t raw = value(cell);
  if (raw == 0) {
    return CollisionKind::Free;
  }
  if ((raw & BlockedBit) != 0) {
    return CollisionKind::Blocked;
  }
  return CollisionKind::Trigger;
}

std::optional<std::uint8_t> MapCollision::trigger(CollisionCell cell) const {
  if (kind(cell) != CollisionKind::Trigger) {
    return std::nullopt;
  }
  return value(cell);
}

void MapCollision::setValue(CollisionCell cell, std::uint8_t value) {
  if (contains(cell)) {
    values_[indexOf(cell)] = value;
  }
}

const AssetId* MapAssetIds::background(std::string_view variant) const {
  const auto found = std::ranges::find(backgrounds, variant, &MapBackground::variant);
  return found == backgrounds.end() ? nullptr : &found->id;
}

const MapWarp* MapDocument::warp(std::int32_t exit) const {
  const auto found = std::ranges::find(warps, exit, &MapWarp::exit);
  return found == warps.end() ? nullptr : &*found;
}

core::Result<MapDocument> parseMapDocument(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("map data: {}", tree.error().message)}};
  }
  YamlReader reader;
  const YamlNode& root = *tree;
  if (!reader.expectKeys(root, "the map data",
                         {"camera", "lights", "ambient", "regions", "tiles", "images", "objects",
                          "warps", "digimon_count", "npcs", "collision"})) {
    return std::unexpected{reader.error("map data")};
  }
  MapDocument document;
  document.camera = readCamera(reader, reader.field(root, "camera"));
  document.lights = readLights(reader, reader.field(root, "lights"));
  document.ambient = readColor(reader, reader.field(root, "ambient"));
  document.regions = readRegions(reader, reader.field(root, "regions"));
  document.tiles = readTiles(reader, reader.field(root, "tiles"));
  document.images = readImages(reader, reader.field(root, "images"));
  document.objects = readObjects(reader, reader.field(root, "objects"));
  document.warps = readWarps(reader, reader.field(root, "warps"));
  document.digimonCount = reader.integer<std::int32_t>(reader.field(root, "digimon_count"));
  for (const YamlNode& item : reader.sequence(reader.field(root, "npcs"), "npcs")) {
    document.npcs.push_back(readNpc(reader, item));
  }
  document.collision = readCollision(reader, reader.field(root, "collision"));
  return reader.finish(std::move(document), "map data");
}

core::Result<AssetId> mapDataId(std::string_view mapId) {
  return AssetId::parse(std::format("map/{}/data", contentAssetPath(mapId)));
}

MapAssetIds findMapAssets(const VirtualFileSystem& vfs, std::string_view mapId) {
  const std::string prefix = std::format("map/{}/", contentAssetPath(mapId));
  constexpr std::string_view BackgroundPrefix = "background_";
  constexpr std::string_view MetaSuffix = "_meta";
  constexpr std::array<std::pair<std::string_view, std::int32_t>, 2> TextureFolders = {{
      {"texture8/", 8},
      {"texture4/", 4},
  }};

  MapAssetIds assets;
  std::vector<AssetId> metas;
  for (const AssetId& id : vfs.ids()) {
    const std::string_view text = id.str();
    if (!text.starts_with(prefix)) {
      continue;
    }
    const std::string_view name = text.substr(prefix.size());
    if (name.starts_with(BackgroundPrefix) && !name.contains('/')) {
      assets.backgrounds.push_back(
          {.variant = std::string{name.substr(BackgroundPrefix.size())}, .id = id});
      continue;
    }
    for (const auto& [folder, bits] : TextureFolders) {
      if (!name.starts_with(folder)) {
        continue;
      }
      const std::string_view file = name.substr(folder.size());
      if (file.ends_with(MetaSuffix)) {
        metas.push_back(id);
      } else if (const auto index = parseIndex(file)) {
        assets.textures.push_back(
            {.bitsPerPixel = bits, .index = *index, .image = id, .meta = std::nullopt});
      }
    }
  }
  for (MapTexture& texture : assets.textures) {
    const std::string metaText = texture.image.str() + std::string{MetaSuffix};
    const auto meta = std::ranges::find(metas, metaText, &AssetId::str);
    if (meta != metas.end()) {
      texture.meta = *meta;
    }
  }
  std::ranges::sort(assets.backgrounds, {},
                    [](const MapBackground& b) { return variantOrder(b.variant); });
  std::ranges::sort(assets.textures, [](const MapTexture& a, const MapTexture& b) {
    return a.bitsPerPixel != b.bitsPerPixel ? a.bitsPerPixel > b.bitsPerPixel : a.index < b.index;
  });
  return assets;
}

core::Result<MapDocument> loadMapDocument(const VirtualFileSystem& vfs, std::string_view mapId) {
  auto id = mapDataId(mapId);
  if (!id) {
    return std::unexpected{core::Error{std::format("map '{}': {}", mapId, id.error().message)}};
  }
  auto bytes = vfs.read(*id);
  if (!bytes) {
    return std::unexpected{core::Error{std::format("map '{}': {}", mapId, bytes.error().message)}};
  }
  const std::string_view text{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
  auto document = parseMapDocument(text);
  if (!document) {
    return std::unexpected{
        core::Error{std::format("map '{}': {}", mapId, document.error().message)}};
  }
  document->id = std::string{mapId};
  document->assets = findMapAssets(vfs, mapId);
  return document;
}

glm::vec3 toWorldPosition(glm::ivec3 original) {
  return core::fromOriginalAxes(glm::vec3{original});
}

// map_collision.c: getModelTile, with floor instead of its truncation toward zero.
CollisionCell collisionCellAt(glm::vec3 world) {
  return {.column = static_cast<std::int32_t>(std::floor(world.x / CollisionCellSize)) + GridOrigin,
          .row = static_cast<std::int32_t>(std::floor(world.z / CollisionCellSize)) + GridOrigin};
}

CollisionCell originalCellAt(glm::vec3 world) {
  // The original's whole units; a world position between two of them truncates as its integers
  // would hold it.
  const auto x = static_cast<std::int32_t>(world.x);
  const auto z = static_cast<std::int32_t>(-world.z);
  const auto size = static_cast<std::int32_t>(CollisionCellSize);
  return {.column = (x / size) + GridOrigin - (x < 0 ? 1 : 0),
          .row = GridOrigin - (z / size) - (z > 0 ? 1 : 0)};
}

glm::vec3 collisionCellMin(CollisionCell cell) {
  return {static_cast<float>(cell.column - GridOrigin) * CollisionCellSize, 0.0F,
          static_cast<float>(cell.row - GridOrigin) * CollisionCellSize};
}

glm::vec3 collisionCellCenter(CollisionCell cell) {
  return collisionCellMin(cell) +
         glm::vec3{CollisionCellSize / 2.0F, 0.0F, CollisionCellSize / 2.0F};
}

float yawFromOriginal(std::int32_t rotation) {
  return -static_cast<float>(rotation) * 2.0F * std::numbers::pi_v<float> /
         static_cast<float>(core::OriginalAnglePerTurn);
}

std::int32_t yawToOriginal(float yaw) {
  const float turns = -yaw / (2.0F * std::numbers::pi_v<float>);
  const auto rotation = static_cast<std::int32_t>(
      std::lround(turns * static_cast<float>(core::OriginalAnglePerTurn)));
  return ((rotation % core::OriginalAnglePerTurn) + core::OriginalAnglePerTurn) %
         core::OriginalAnglePerTurn;
}

glm::vec3 forwardOfYaw(float yaw) {
  return {std::sin(yaw), 0.0F, std::cos(yaw)};
}

float yawOfDirection(glm::vec3 direction) {
  if (direction.x == 0.0F && direction.z == 0.0F) {
    return 0.0F;
  }
  return std::atan2(direction.x, direction.z);
}

SpawnPoint warpSpawn(const MapWarp& warp) {
  return {.position = toWorldPosition(warp.spawn), .yaw = yawFromOriginal(warp.rotation)};
}

} // namespace dwe::assets
