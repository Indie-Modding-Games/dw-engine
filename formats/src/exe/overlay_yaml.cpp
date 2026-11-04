#include <dwe/formats/exe/overlay_yaml.hpp>

#include <yaml/yaml_values.hpp>

#include <concepts>
#include <cstdint>
#include <format>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::formats::exe {

namespace {

using core::YamlNode;
using yaml::null;
using yaml::number;
using yaml::numbers;
using yaml::quoted;

constexpr std::uint8_t NoMove = 0xFF;

// A reference by index: the id, null for the "none" value, or the raw number (with a note) outside
// the table.
template <std::integral T>
YamlNode reference(const std::vector<std::string>& ids, T index, std::optional<T> none,
                   std::string_view where, std::vector<std::string>& notes) {
  if (none && index == *none) {
    return null();
  }
  if (std::cmp_greater_equal(index, 0) && std::cmp_less(index, ids.size())) {
    return quoted(ids[static_cast<std::size_t>(index)]);
  }
  notes.push_back(std::format("{}: reference {} is outside its table", where, index));
  return number(index);
}

YamlNode point(const Point16& value) {
  YamlNode node = YamlNode::mapping();
  node.set("x", number(value.x));
  node.set("y", number(value.y));
  return node;
}

YamlNode points(const std::vector<Point16>& values) {
  YamlNode list = YamlNode::sequence();
  for (const Point16& value : values) {
    list.append(point(value));
  }
  return list;
}

template <typename Rows>
YamlNode rows(const Rows& values) {
  YamlNode list = YamlNode::sequence();
  for (const auto& row : values) {
    list.append(numbers(row));
  }
  return list;
}

YamlNode fishingSpot(const FishingSpot& spot) {
  YamlNode node = YamlNode::mapping();
  node.set("unknown_0", numbers(spot.unknown0));
  node.set("unknown_4", number(spot.unknown4));
  node.set("points", points(spot.points));
  node.set("unknown_d", numbers(spot.unknownD));
  return node;
}

YamlNode fishingMap(const FishingMap& map, const TableIds& ids, std::string_view where,
                    std::vector<std::string>& notes) {
  YamlNode node = YamlNode::mapping();
  node.set("map", reference<std::int16_t>(ids.maps, map.mapId, std::nullopt, where, notes));
  node.set("unknown_2", number(map.unknown2));
  node.set("scale", number(map.scale));
  node.set("unknown_8", number(map.unknown8));
  node.set("fish_scale_small", number(map.fishScaleSmall));
  node.set("fish_scale_large", number(map.fishScaleLarge));
  node.set("seadramon_times", numbers(map.seadramonTimes));
  YamlNode spots = YamlNode::sequence();
  for (const FishingSpot& spot : map.spots) {
    spots.append(fishingSpot(spot));
  }
  node.set("spots", std::move(spots));
  node.set("unknown_1c", number(map.unknown1C));
  node.set("shoreline", map.shoreline ? number(*map.shoreline) : null());
  node.set("cast_range", number(map.castRange));
  node.set("unknown_2a", number(map.unknown2A));
  node.set("sprite_file", quoted(map.spriteFile));
  node.set("seadramon_waypoints", points(map.seadramonWaypoints));
  return node;
}

} // namespace

TablesYaml fishingToYaml(const FishingTables& tables, const TableIds& ids) {
  std::vector<std::string> notes;
  YamlNode bait = YamlNode::sequence();
  for (std::size_t i = 0; i < tables.baitChances.size(); ++i) {
    const FishBaitChance& chance = tables.baitChances[i];
    YamlNode node = YamlNode::mapping();
    node.set("item", reference<std::uint8_t>(ids.items, chance.item, std::nullopt,
                                             std::format("FISH_BAIT_CHANCES[{}]", i), notes));
    node.set("threshold", number(chance.threshold));
    bait.append(std::move(node));
  }

  YamlNode bite = YamlNode::mapping();
  bite.set("first_depth", number(tables.firstBiteDepth));
  bite.set("rows", rows(tables.biteChances));
  bite.set("unused", numbers(tables.unusedBiteBytes));

  YamlNode kinds = YamlNode::sequence();
  for (std::size_t i = 0; i < tables.kinds.size(); ++i) {
    const FishKind& kind = tables.kinds[i];
    YamlNode node = YamlNode::mapping();
    node.set("index", number(i));
    node.set("unknown_0", number(kind.unknown0));
    node.set("size_min", number(kind.sizeMin));
    node.set("size_max", number(kind.sizeMax));
    node.set("move_delay_min", number(kind.moveDelayMin));
    node.set("move_delay_max", number(kind.moveDelayMax));
    node.set("bait", reference<std::int16_t>(ids.items, kind.bait, std::int16_t{-1},
                                             std::format("FISH_KIND_INFO[{}]", i), notes));
    node.set("unknown_16", number(kind.unknown16));
    kinds.append(std::move(node));
  }

  YamlNode shorelines = YamlNode::sequence();
  for (const FishingShoreline& shoreline : tables.shorelines) {
    YamlNode node = YamlNode::mapping();
    node.set("axis", quoted(std::string(1, shoreline.axis)));
    node.set("points", points(shoreline.points));
    shorelines.append(std::move(node));
  }

  YamlNode maps = YamlNode::sequence();
  for (std::size_t i = 0; i < tables.maps.size(); ++i) {
    maps.append(fishingMap(tables.maps[i], ids, std::format("FISHING_MAP_DATA[{}]", i), notes));
  }

  YamlNode root = YamlNode::mapping();
  root.set("bait_chances", std::move(bait));
  root.set("bite_chances", std::move(bite));
  root.set("kinds", std::move(kinds));
  root.set("shorelines", std::move(shorelines));
  root.set("maps", std::move(maps));
  TablesYaml out;
  out.documents.push_back({.name = "fishing", .document = std::move(root)});
  out.notes = std::move(notes);
  return out;
}

TablesYaml battleStageToYaml(const BattleStageTables& tables, const std::string& name) {
  YamlNode cameras = YamlNode::sequence();
  for (const CameraPreset& preset : tables.cameraPresets) {
    cameras.append(numbers(preset.values));
  }
  YamlNode arenas = YamlNode::sequence();
  for (const BattleArena& arena : tables.arenas) {
    YamlNode node = YamlNode::mapping();
    node.set("index", number(arena.index));
    node.set("model", quoted(arena.model));
    node.set("texture", quoted(arena.texture));
    node.set("collision", quoted(arena.collision));
    node.set("model_count", number(arena.modelCount));
    node.set("texture_count", number(arena.textureCount));
    arenas.append(std::move(node));
  }
  YamlNode root = YamlNode::mapping();
  if (!tables.opponentBaseStats.empty()) {
    root.set("opponent_base_stats", numbers(tables.opponentBaseStats));
  }
  if (!tables.grades.empty()) {
    YamlNode grades = YamlNode::sequence();
    for (const std::string& grade : tables.grades) {
      grades.append(quoted(grade));
    }
    root.set("grades", std::move(grades));
  }
  if (!tables.winChances.empty()) {
    root.set("win_chances", rows(tables.winChances));
  }
  root.set("camera_presets", std::move(cameras));
  root.set("arenas", std::move(arenas));
  if (!tables.bracket.paths.empty()) {
    const BracketTables& bracket = tables.bracket;
    YamlNode paths = YamlNode::sequence();
    for (const BracketPath& path : bracket.paths) {
      YamlNode node = YamlNode::mapping();
      node.set("x", numbers(path.x));
      node.set("flip", numbers(path.flip));
      paths.append(std::move(node));
    }
    YamlNode node = YamlNode::mapping();
    node.set("row_y", numbers(bracket.rowY));
    node.set("hop_y", numbers(bracket.hopY));
    node.set("paths", std::move(paths));
    node.set("sprite_palettes", numbers(bracket.spritePalettes));
    node.set("projectile_icons", numbers(bracket.projectileIcons));
    node.set("projectile_palettes", numbers(bracket.projectilePalettes));
    root.set("bracket", std::move(node));
  }
  TablesYaml out;
  out.documents.push_back({.name = name, .document = std::move(root)});
  return out;
}

TablesYaml trainingToYaml(const TrainingTables& tables, const TableIds& ids) {
  std::vector<std::string> notes;
  YamlNode animations = YamlNode::sequence();
  for (std::size_t i = 0; i < tables.animations.size(); ++i) {
    YamlNode node = YamlNode::mapping();
    node.set("digimon", reference<std::size_t>(ids.digimon, i, std::nullopt,
                                               std::format("TRN_TRAINING_ANIM_IDS[{}]", i), notes));
    node.set("animation", number(tables.animations[i].animation));
    node.set("hit_frame", number(tables.animations[i].hitFrame));
    animations.append(std::move(node));
  }
  YamlNode moves = YamlNode::sequence();
  for (std::size_t i = 0; i < tables.learnableMoves.size(); ++i) {
    YamlNode row = YamlNode::sequence();
    for (const std::uint8_t move : tables.learnableMoves[i]) {
      row.append(reference<std::uint8_t>(ids.moves, move, NoMove,
                                         std::format("TRN_D_8008F14C[{}]", i), notes));
    }
    moves.append(std::move(row));
  }
  YamlNode spots = YamlNode::sequence();
  for (const TrainingMapSpots& map : tables.spots) {
    YamlNode list = YamlNode::sequence();
    for (const TrainingSpot& spot : map.spots) {
      YamlNode node = YamlNode::mapping();
      node.set("x", number(spot.x));
      node.set("y", number(spot.y));
      node.set("z", number(spot.z));
      node.set("stat", number(spot.stat));
      list.append(std::move(node));
    }
    YamlNode node = YamlNode::mapping();
    node.set("map",
             reference<std::int32_t>(ids.maps, map.mapId, std::nullopt, "training spots", notes));
    node.set("spots", std::move(list));
    spots.append(std::move(node));
  }
  YamlNode root = YamlNode::mapping();
  root.set("animations", std::move(animations));
  root.set("learnable_moves", std::move(moves));
  root.set("learn_weights", rows(tables.learnWeights));
  root.set("slot_reels", rows(tables.slotReels));
  root.set("assist_rolls", numbers(tables.assistRolls));
  root.set("spots", std::move(spots));
  TablesYaml out;
  out.documents.push_back({.name = "training", .document = std::move(root)});
  out.notes = std::move(notes);
  return out;
}

TablesYaml battleToYaml(const BattleTables& tables, const TableIds& ids) {
  constexpr std::int16_t NoSprite = -1;
  std::vector<std::string> notes;
  YamlNode shots = YamlNode::sequence();
  for (std::size_t i = 0; i < tables.flattenedShotSprites.size(); ++i) {
    const std::int16_t sprite = tables.flattenedShotSprites[i];
    if (sprite == NoSprite) {
      continue;
    }
    YamlNode node = YamlNode::mapping();
    node.set("digimon",
             reference<std::size_t>(ids.digimon, i, std::nullopt,
                                    std::format("BTL_AURA_PROJECTILE_TYPES[{}]", i), notes));
    node.set("sprite", number(sprite));
    shots.append(std::move(node));
  }
  YamlNode root = YamlNode::mapping();
  root.set("flattened_shots", std::move(shots));
  TablesYaml out;
  out.documents.push_back({.name = "battle", .document = std::move(root)});
  out.notes = std::move(notes);
  return out;
}

} // namespace dwe::formats::exe
