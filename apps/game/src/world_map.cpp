#include "world_map.hpp"

#include <dwe/core/collision_triggers.hpp>
#include <dwe/core/number_text.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <map>
#include <ranges>
#include <system_error>
#include <utility>

namespace dwe::app {
namespace {

using core::YamlNode;

// Collision triggers 110 to 119 take the player through warps 0 to 9 (checkMapInteraction). The
// world map shows the original's exits, whatever field/triggers says.
using core::collision::FirstWarpTrigger;
constexpr std::int32_t WarpCount = core::collision::WarpTriggerCount;

core::Error layoutError(const YamlNode& at, std::string_view problem) {
  return core::Error{std::format("{}: line {}: {}", WorldMapLayoutAsset, at.line(), problem)};
}

const YamlNode* scalarAt(const YamlNode& node, std::string_view key) {
  const YamlNode* value = node.isMapping() ? node.find(key) : nullptr;
  return value != nullptr && value->isScalar() ? value : nullptr;
}

std::optional<float> numberOf(const YamlNode& node) {
  if (!node.isScalar()) {
    return std::nullopt;
  }
  const std::string& text = node.text();
  float value = 0.0F;
  const auto [end, error] = core::fromChars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::int32_t> integerOf(const YamlNode& node) {
  if (!node.isScalar()) {
    return std::nullopt;
  }
  const std::string& text = node.text();
  std::int32_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// Two numbers: [x, y].
std::optional<std::pair<float, float>> pairOf(const YamlNode* node) {
  if (node == nullptr || !node->isSequence() || node->items().size() != 2) {
    return std::nullopt;
  }
  const auto first = numberOf(node->items()[0]);
  const auto second = numberOf(node->items()[1]);
  if (!first || !second) {
    return std::nullopt;
  }
  return std::pair{*first, *second};
}

// "#rrggbb" as opaque RGBA.
std::optional<std::uint32_t> colourOf(const YamlNode* node) {
  constexpr std::size_t Length = 7;
  if (node == nullptr || !node->isScalar() || node->text().size() != Length ||
      node->text().front() != '#') {
    return std::nullopt;
  }
  const std::string& text = node->text();
  std::uint32_t rgb = 0;
  constexpr int Hex = 16;
  const auto [end, error] = std::from_chars(text.data() + 1, text.data() + text.size(), rgb, Hex);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  constexpr std::uint32_t Opaque = 0xFF;
  return (rgb << 8U) | Opaque;
}

core::Result<WorldMapRegion> regionOf(const YamlNode& node) {
  WorldMapRegion region;
  const YamlNode* id = scalarAt(node, "id");
  const YamlNode* name = scalarAt(node, "name");
  const YamlNode* places = node.isMapping() ? node.find("places") : nullptr;
  const auto at = pairOf(node.isMapping() ? node.find("at") : nullptr);
  const YamlNode* radius = scalarAt(node, "radius");
  const auto colour = colourOf(node.isMapping() ? node.find("colour") : nullptr);
  if (id == nullptr || id->text().empty() || name == nullptr || !integerOf(*name) ||
      places == nullptr || !places->isSequence() || !at || radius == nullptr ||
      !numberOf(*radius) || !colour) {
    return std::unexpected{layoutError(
        node, "an area needs id, name, places, at: [x, y], radius and colour: \"#rrggbb\"")};
  }
  region.id = id->text();
  region.name = *integerOf(*name);
  for (const YamlNode& place : places->items()) {
    const auto value = integerOf(place);
    if (!value) {
      return std::unexpected{layoutError(place, "a place is a slot of main.map_names")};
    }
    region.places.push_back(*value);
  }
  region.x = at->first;
  region.y = at->second;
  region.radius = *numberOf(*radius);
  region.colour = *colour;
  return region;
}

} // namespace

std::optional<std::size_t> WorldMapLayout::regionOfPlace(std::int32_t place) const {
  for (std::size_t i = 0; i < regions.size(); ++i) {
    if (std::ranges::contains(regions[i].places, place)) {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> WorldMapLayout::regionOfMap(const assets::MapTable& maps,
                                                       std::string_view mapId) const {
  const assets::MapEntry* entry = maps.find(mapId);
  return entry != nullptr ? regionOfPlace(entry->loadingName) : std::nullopt;
}

core::Result<WorldMapLayout> parseWorldMapLayout(std::string_view yaml) {
  auto root = core::parseYaml(yaml);
  if (!root) {
    return std::unexpected{root.error()};
  }
  WorldMapLayout layout;
  const auto canvas = pairOf(root->isMapping() ? root->find("canvas") : nullptr);
  const YamlNode* regions = root->isMapping() ? root->find("regions") : nullptr;
  if (!canvas || canvas->first <= 0.0F || canvas->second <= 0.0F || regions == nullptr ||
      !regions->isSequence()) {
    return std::unexpected{
        layoutError(*root, "the layout needs canvas: [width, height] and a list of regions")};
  }
  layout.width = canvas->first;
  layout.height = canvas->second;
  std::map<std::int32_t, std::string> owners;
  for (const YamlNode& node : regions->items()) {
    auto region = regionOf(node);
    if (!region) {
      return std::unexpected{region.error()};
    }
    if (std::ranges::contains(layout.regions, region->id, &WorldMapRegion::id)) {
      return std::unexpected{layoutError(node, std::format("two areas are {}", region->id))};
    }
    for (const std::int32_t place : region->places) {
      if (const auto [owner, added] = owners.emplace(place, region->id); !added) {
        return std::unexpected{layoutError(
            node, std::format("the place {} is in {} and {}", place, owner->second, region->id))};
      }
    }
    layout.regions.push_back(std::move(*region));
  }
  const YamlNode* roads = root->find("roads");
  if (roads != nullptr && !roads->isSequence()) {
    return std::unexpected{layoutError(*roads, "roads is a list of [area, area]")};
  }
  const auto indexOf = [&](const YamlNode& node) -> std::optional<std::size_t> {
    if (!node.isScalar()) {
      return std::nullopt;
    }
    const auto found = std::ranges::find(layout.regions, node.text(), &WorldMapRegion::id);
    if (found == layout.regions.end()) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(layout.regions.begin(), found));
  };
  for (const YamlNode& road : roads != nullptr ? roads->items() : std::span<const YamlNode>{}) {
    const bool pair = road.isSequence() && road.items().size() == 2;
    const auto from = pair ? indexOf(road.items()[0]) : std::nullopt;
    const auto to = pair ? indexOf(road.items()[1]) : std::nullopt;
    if (!from || !to) {
      return std::unexpected{layoutError(road, "a road joins two areas of the list")};
    }
    layout.roads.push_back({.from = *from, .to = *to});
  }
  return layout;
}

std::vector<bool> knownRegions(const WorldMapLayout& layout, const assets::MapTable& maps,
                               const std::set<std::string, std::less<>>& visited,
                               std::optional<std::size_t> here) {
  std::vector<bool> known(layout.regions.size(), false);
  for (const std::string& map : visited) {
    if (const auto region = layout.regionOfMap(maps, map)) {
      known[*region] = true;
    }
  }
  if (here && *here < known.size()) {
    known[*here] = true;
  }
  return known;
}

ui::WorldMapPicture worldMapPicture(const WorldMapLayout& layout, const std::vector<bool>& known) {
  ui::WorldMapPicture picture{
      .width = layout.width, .height = layout.height, .areas = {}, .roads = layout.roads};
  for (std::size_t i = 0; i < layout.regions.size(); ++i) {
    const WorldMapRegion& region = layout.regions[i];
    picture.areas.push_back({.x = region.x,
                             .y = region.y,
                             .radius = region.radius,
                             .colour = region.colour,
                             .known = i < known.size() && known[i]});
  }
  return picture;
}

std::optional<ui::WorldMapPoint> worldMapMarker(const WorldMapLayout& layout,
                                                std::optional<std::size_t> here) {
  if (!here || *here >= layout.regions.size()) {
    return std::nullopt;
  }
  const WorldMapRegion& region = layout.regions[*here];
  return ui::locateOnWorldMap(
      ui::WorldMapPicture{.width = layout.width, .height = layout.height, .areas = {}, .roads = {}},
      region.x, region.y);
}

std::vector<std::string> exitMapsOf(const assets::MapDocument& map, std::string_view mapId,
                                    std::span<const game::field::ScriptWarp> scriptWarps) {
  std::vector<std::string> out;
  const auto add = [&out](const std::string& target) {
    if (!std::ranges::contains(out, target)) {
      out.push_back(target);
    }
  };
  std::array<bool, WarpCount> triggered{};
  for (const std::uint8_t value : map.collision.values()) {
    const std::int32_t exit = static_cast<std::int32_t>(value) - FirstWarpTrigger;
    if (exit >= 0 && exit < WarpCount) {
      triggered.at(static_cast<std::size_t>(exit)) = true;
    }
  }
  for (const assets::MapWarp& warp : map.warps) {
    if (warp.exit >= 0 && warp.exit < WarpCount &&
        triggered.at(static_cast<std::size_t>(warp.exit)) && warp.targetMapId) {
      add(*warp.targetMapId);
    }
  }
  for (const game::field::ScriptWarp& warp : scriptWarps) {
    if (warp.map == mapId && warp.targetMap &&
        (warp.via == game::field::WarpVia::Step || warp.via == game::field::WarpVia::Confirm)) {
      add(*warp.targetMap);
    }
  }
  return out;
}

std::vector<WorldMapExit> worldMapExits(const WorldMapLayout& layout, const assets::MapTable& maps,
                                        std::span<const std::string> exitMaps,
                                        std::optional<std::int32_t> herePlace) {
  std::vector<WorldMapExit> out;
  for (const std::string& map : exitMaps) {
    const assets::MapEntry* entry = maps.find(map);
    if (entry == nullptr || entry->loadingName == herePlace ||
        std::ranges::contains(out, entry->loadingName, &WorldMapExit::place)) {
      continue;
    }
    out.push_back(
        {.place = entry->loadingName, .region = layout.regionOfPlace(entry->loadingName)});
  }
  return out;
}

WorldMapKeyStep worldMapKeyStep(const WorldMapKeys& keys) {
  if (keys.open) {
    return keys.mapPressed || keys.cancelPressed ? WorldMapKeyStep::Close : WorldMapKeyStep::None;
  }
  return keys.mapPressed && keys.canOpen && keys.enabled ? WorldMapKeyStep::Open
                                                         : WorldMapKeyStep::None;
}

} // namespace dwe::app
