#include <dwe/assets/fishing_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "document_text.hpp"

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view FishingId = "data/fishing";
constexpr std::string_view Context = "fishing table";
constexpr std::string_view BaitChancesKey = "bait_chances";
constexpr std::string_view ItemKey = "item";
constexpr std::string_view ThresholdKey = "threshold";
constexpr std::string_view BiteChancesKey = "bite_chances";
constexpr std::string_view FirstDepthKey = "first_depth";
constexpr std::string_view RowsKey = "rows";
constexpr std::string_view UnusedKey = "unused";
constexpr std::string_view KindsKey = "kinds";
constexpr std::string_view IndexKey = "index";
constexpr std::string_view Unknown0Key = "unknown_0";
constexpr std::string_view SizeMinKey = "size_min";
constexpr std::string_view SizeMaxKey = "size_max";
constexpr std::string_view MoveDelayMinKey = "move_delay_min";
constexpr std::string_view MoveDelayMaxKey = "move_delay_max";
constexpr std::string_view BaitKey = "bait";
constexpr std::string_view Unknown16Key = "unknown_16";
constexpr std::string_view ShorelinesKey = "shorelines";
constexpr std::string_view AxisKey = "axis";
constexpr std::string_view PointsKey = "points";
constexpr std::string_view XKey = "x";
constexpr std::string_view YKey = "y";
constexpr std::string_view MapsKey = "maps";
constexpr std::string_view MapKey = "map";
constexpr std::string_view Unknown2Key = "unknown_2";
constexpr std::string_view ScaleKey = "scale";
constexpr std::string_view Unknown8Key = "unknown_8";
constexpr std::string_view FishScaleSmallKey = "fish_scale_small";
constexpr std::string_view FishScaleLargeKey = "fish_scale_large";
constexpr std::string_view SeadramonTimesKey = "seadramon_times";
constexpr std::string_view SpotsKey = "spots";
constexpr std::string_view Unknown4Key = "unknown_4";
constexpr std::string_view UnknownDKey = "unknown_d";
constexpr std::string_view Unknown1CKey = "unknown_1c";
constexpr std::string_view ShorelineKey = "shoreline";
constexpr std::string_view CastRangeKey = "cast_range";
constexpr std::string_view Unknown2AKey = "unknown_2a";
constexpr std::string_view SpriteFileKey = "sprite_file";
constexpr std::string_view WaypointsKey = "seadramon_waypoints";

std::int32_t integer(YamlReader& reader, const YamlNode& node, std::string_view key) {
  return reader.integer<std::int32_t>(reader.field(node, key));
}

std::string reference(YamlReader& reader, const YamlNode& node, std::string_view key) {
  const YamlNode& value = reader.field(node, key);
  return YamlReader::isNull(value) ? std::string{} : reader.text(value);
}

std::vector<std::int32_t> integers(YamlReader& reader, const YamlNode& node, std::string_view key) {
  std::vector<std::int32_t> values;
  for (const YamlNode& value : reader.sequence(reader.field(node, key), key)) {
    values.push_back(reader.integer<std::int32_t>(value));
  }
  return values;
}

std::vector<glm::ivec2> points(YamlReader& reader, const YamlNode& node, std::string_view key) {
  std::vector<glm::ivec2> result;
  for (const YamlNode& point : reader.sequence(reader.field(node, key), key)) {
    if (reader.expectKeys(point, "a point", {XKey, YKey})) {
      result.emplace_back(integer(reader, point, XKey), integer(reader, point, YKey));
    }
  }
  return result;
}

FishingKindEntry readKind(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a fish kind",
                         {IndexKey, Unknown0Key, SizeMinKey, SizeMaxKey, MoveDelayMinKey,
                          MoveDelayMaxKey, BaitKey, Unknown16Key})) {
    return {};
  }
  return {.item = integer(reader, node, Unknown0Key),
          .sizeMin = integer(reader, node, SizeMinKey),
          .sizeMax = integer(reader, node, SizeMaxKey),
          .moveDelayMin = integer(reader, node, MoveDelayMinKey),
          .moveDelayMax = integer(reader, node, MoveDelayMaxKey),
          .bait = reference(reader, node, BaitKey)};
}

FishingSpotEntry readSpot(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a fishing spot",
                         {Unknown0Key, Unknown4Key, PointsKey, UnknownDKey})) {
    return {};
  }
  const std::vector<std::int32_t> head = integers(reader, node, Unknown0Key);
  return {.weight = head.empty() ? 0 : head.front(), .points = points(reader, node, PointsKey)};
}

// A path of the disc ("\\ETCDAT\\FISHING.TIM") as the image dwx extracts from it
// ("texture/etcdat/fishing").
std::string imageOfDiscPath(std::string_view path) {
  std::string image;
  for (const char c : path) {
    image += c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  while (image.starts_with('/')) {
    image.erase(0, 1);
  }
  constexpr std::string_view Extension = ".tim";
  if (image.ends_with(Extension)) {
    image.resize(image.size() - Extension.size());
  }
  return image.empty() ? image : "texture/" + image;
}

FishingMapEntry readMap(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a fishing map",
                         {MapKey, Unknown2Key, ScaleKey, Unknown8Key, FishScaleSmallKey,
                          FishScaleLargeKey, SeadramonTimesKey, SpotsKey, Unknown1CKey,
                          ShorelineKey, CastRangeKey, Unknown2AKey, SpriteFileKey, WaypointsKey})) {
    return {};
  }
  FishingMapEntry map{.map = reference(reader, node, MapKey),
                      .splash = integer(reader, node, Unknown8Key),
                      .seaSerpentTimes = integers(reader, node, SeadramonTimesKey),
                      .spots = {},
                      .shoreline = reader.optionalInteger<std::size_t>(node, ShorelineKey),
                      .castRange = integer(reader, node, CastRangeKey),
                      .seaSerpentPath = points(reader, node, WaypointsKey),
                      .scale = integer(reader, node, ScaleKey),
                      .image = imageOfDiscPath(reference(reader, node, SpriteFileKey))};
  for (const YamlNode& spot : reader.sequence(reader.field(node, SpotsKey), SpotsKey)) {
    map.spots.push_back(readSpot(reader, spot));
  }
  return map;
}

} // namespace

const FishingMapEntry* FishingTable::findMap(std::string_view id) const {
  const auto found = std::ranges::find(maps, id, &FishingMapEntry::map);
  return found == maps.end() ? nullptr : &*found;
}

core::Result<FishingTable> parseFishingTable(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
  }
  YamlReader reader;
  const YamlNode& root = *tree;
  if (!reader.expectKeys(root, "the fishing table",
                         {BaitChancesKey, BiteChancesKey, KindsKey, ShorelinesKey, MapsKey})) {
    return std::unexpected{reader.error(Context)};
  }
  FishingTable table;
  for (const YamlNode& junk : reader.sequence(reader.field(root, BaitChancesKey), BaitChancesKey)) {
    if (reader.expectKeys(junk, "a bait chance", {ItemKey, ThresholdKey})) {
      table.junk.push_back({.item = reference(reader, junk, ItemKey),
                            .threshold = integer(reader, junk, ThresholdKey)});
    }
  }
  const YamlNode& bites = reader.field(root, BiteChancesKey);
  if (reader.expectKeys(bites, BiteChancesKey, {FirstDepthKey, RowsKey, UnusedKey})) {
    table.firstBait = integer(reader, bites, FirstDepthKey);
    for (const YamlNode& row : reader.sequence(reader.field(bites, RowsKey), RowsKey)) {
      std::vector<std::int32_t> values;
      for (const YamlNode& value : reader.sequence(row, RowsKey)) {
        values.push_back(reader.integer<std::int32_t>(value));
      }
      table.biteChances.push_back(std::move(values));
    }
  }
  for (const YamlNode& kind : reader.sequence(reader.field(root, KindsKey), KindsKey)) {
    table.kinds.push_back(readKind(reader, kind));
  }
  for (const YamlNode& line : reader.sequence(reader.field(root, ShorelinesKey), ShorelinesKey)) {
    if (!reader.expectKeys(line, "a shoreline", {AxisKey, PointsKey})) {
      continue;
    }
    const std::string axis = reader.text(reader.field(line, AxisKey));
    if (axis != "x" && axis != "y") {
      reader.fail(line, "a shoreline's axis is x or y");
    }
    table.shorelines.push_back(
        {.axis = axis.empty() ? 'x' : axis.front(), .points = points(reader, line, PointsKey)});
  }
  for (const YamlNode& map : reader.sequence(reader.field(root, MapsKey), MapsKey)) {
    table.maps.push_back(readMap(reader, map));
  }
  return reader.finish(std::move(table), Context);
}

core::Result<std::optional<FishingTable>> loadFishingTable(const VirtualFileSystem& vfs) {
  auto id = AssetId::parse(FishingId);
  if (!id) {
    return std::unexpected{id.error()};
  }
  if (vfs.resolve(*id) == nullptr) {
    return std::optional<FishingTable>{};
  }
  auto text = readDocumentText(vfs, FishingId);
  if (!text) {
    return std::unexpected{text.error()};
  }
  auto table = parseFishingTable(*text);
  if (!table) {
    return std::unexpected{table.error()};
  }
  return std::optional<FishingTable>{std::move(*table)};
}

} // namespace dwe::assets
