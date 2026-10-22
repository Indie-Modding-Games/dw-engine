#include <dwe/assets/map_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::size_t DoorsPerSet = 6;

glm::ivec3 readVector(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a vector", {"x", "y", "z"})) {
    return {};
  }
  return {reader.integer<std::int32_t>(reader.field(node, "x")),
          reader.integer<std::int32_t>(reader.field(node, "y")),
          reader.integer<std::int32_t>(reader.field(node, "z"))};
}

MapEntry readEntry(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a map",
                         {"id", EntryIndexKey, "filename", "images_8bpp", "images_4bpp", "flags",
                          "sound_id", "fixed_time_of_day", "has_npcs", "doors", "toilet",
                          "loading_name"})) {
    return {};
  }
  const auto integer = [&](std::string_view key) {
    return reader.integer<std::int32_t>(reader.field(node, key));
  };
  return {.id = reader.text(reader.field(node, "id")),
          .index = integer(EntryIndexKey),
          .filename = reader.text(reader.field(node, "filename")),
          .images8bpp = integer("images_8bpp"),
          .images4bpp = integer("images_4bpp"),
          .flags = integer("flags"),
          .soundId = integer("sound_id"),
          .fixedTimeOfDay = reader.boolean(reader.field(node, "fixed_time_of_day")),
          .hasNpcs = reader.boolean(reader.field(node, "has_npcs")),
          .doors = reader.optionalInteger<std::int32_t>(reader.field(node, "doors")),
          .toilet = reader.optionalInteger<std::int32_t>(reader.field(node, "toilet")),
          .loadingName = integer("loading_name")};
}

MapDoorSet readDoorSet(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a door set", {"index", "doors"})) {
    return {};
  }
  MapDoorSet set{.index = reader.integer<std::int32_t>(reader.field(node, "index")), .doors = {}};
  const std::span<const YamlNode> doors =
      reader.sequence(reader.field(node, "doors"), "doors", DoorsPerSet);
  for (std::size_t i = 0; i < doors.size(); ++i) {
    if (reader.expectKeys(doors[i], "a door", {"model_id", "position", "rotation"})) {
      set.doors.at(i) = {
          .modelId = reader.optionalInteger<std::int32_t>(reader.field(doors[i], "model_id")),
          .position = readVector(reader, reader.field(doors[i], "position")),
          .rotation = reader.integer<std::int32_t>(reader.field(doors[i], "rotation"))};
    }
  }
  return set;
}

MapToilet readToilet(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a toilet", {"index", "x1", "z1", "x2", "z2"})) {
    return {};
  }
  const auto integer = [&](std::string_view key) {
    return reader.integer<std::int32_t>(reader.field(node, key));
  };
  return {.index = integer("index"),
          .x1 = integer("x1"),
          .z1 = integer("z1"),
          .x2 = integer("x2"),
          .z2 = integer("z2")};
}

MapLightUpdate readLightUpdate(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a light update", {"map", "mode", "start_id", "count", "trigger"})) {
    return {};
  }
  const auto integer = [&](std::string_view key) {
    return reader.integer<std::int32_t>(reader.field(node, key));
  };
  return {.map = reader.text(reader.field(node, "map")),
          .mode = integer("mode"),
          .startId = integer("start_id"),
          .count = integer("count"),
          .trigger = reader.optionalInteger<std::int32_t>(reader.field(node, "trigger"))};
}

} // namespace

const MapEntry* MapTable::find(std::string_view id) const {
  const auto found = std::ranges::find(maps, id, &MapEntry::id);
  return found == maps.end() ? nullptr : &*found;
}

const MapEntry* MapTable::findByIndex(std::int32_t index) const {
  const auto found = std::ranges::find(maps, index, &MapEntry::index);
  return found == maps.end() ? nullptr : &*found;
}

const MapEntry* MapTable::findByOriginalIndex(std::int32_t index) const {
  const auto found = std::ranges::find_if(
      maps, [index](const MapEntry& entry) { return entry.originalIndex && entry.index == index; });
  return found == maps.end() ? nullptr : &*found;
}

std::string_view MapTable::displayName(const MapEntry& entry) const {
  if (entry.loadingName < 0 || static_cast<std::size_t>(entry.loadingName) >= mapNames.size()) {
    return {};
  }
  std::string_view name = mapNames[static_cast<std::size_t>(entry.loadingName)];
  const std::size_t first = name.find_first_not_of(' ');
  if (first == std::string_view::npos) {
    return {};
  }
  name.remove_prefix(first);
  name.remove_suffix(name.size() - name.find_last_not_of(' ') - 1);
  return name;
}

const MapDoorSet* MapTable::doorsOf(const MapEntry& entry) const {
  if (!entry.doors) {
    return nullptr;
  }
  const auto found = std::ranges::find(doors, *entry.doors, &MapDoorSet::index);
  return found == doors.end() ? nullptr : &*found;
}

core::Result<MapTable> parseMapTable(std::string_view yaml,
                                     const std::set<std::string, std::less<>>& engineNumbered) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("map table: {}", tree.error().message)}};
  }
  YamlReader reader;
  const YamlNode& root = *tree;
  if (!reader.expectKeys(root, "the map table",
                         {"maps", "doors", "toilets", "map_names", "light_updates"})) {
    return std::unexpected{reader.error("map table")};
  }
  MapTable table;
  for (const YamlNode& item : reader.sequence(reader.field(root, "maps"), "maps")) {
    table.maps.push_back(readEntry(reader, item));
    table.maps.back().originalIndex = !engineNumbered.contains(table.maps.back().id);
  }
  for (const YamlNode& item : reader.sequence(reader.field(root, "doors"), "doors")) {
    table.doors.push_back(readDoorSet(reader, item));
  }
  for (const YamlNode& item : reader.sequence(reader.field(root, "toilets"), "toilets")) {
    table.toilets.push_back(readToilet(reader, item));
  }
  for (const YamlNode& item : reader.sequence(reader.field(root, "map_names"), "map_names")) {
    table.mapNames.push_back(reader.text(item));
  }
  for (const YamlNode& item :
       reader.sequence(reader.field(root, "light_updates"), "light_updates")) {
    table.lightUpdates.push_back(readLightUpdate(reader, item));
  }
  for (std::size_t i = 0; reader.ok() && i < table.maps.size(); ++i) {
    for (std::size_t j = 0; j < i; ++j) {
      if (table.maps[i].id == table.maps[j].id || table.maps[i].index == table.maps[j].index) {
        reader.fail(
            reader.sequence(reader.field(root, "maps"), "maps")[i],
            std::format("map '{}' ({}) is listed twice", table.maps[i].id, table.maps[i].index));
        break;
      }
    }
  }
  return reader.finish(std::move(table), "map table");
}

core::Result<MapTable> loadMapTable(const VirtualFileSystem& vfs) {
  auto id = AssetId::parse("data/maps");
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto composed = readComposedTableText(vfs, *id, MapComposition);
  if (!composed) {
    return std::unexpected{composed.error()};
  }
  return parseMapTable(composed->text, composed->engineNumbered);
}

} // namespace dwe::assets
