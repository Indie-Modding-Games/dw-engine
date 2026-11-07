#include "world_map_screen.hpp"

#include <dwe/assets/content_id.hpp>
#include <dwe/core/log.hpp>
#include <dwe/localization/catalog.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "field_scene.hpp"

namespace dwe::app {
namespace {

constexpr std::string_view Revision = "world_map.revision";
constexpr std::string_view RegionList = "world_map.region";
constexpr std::string_view ExitList = "world_map.exit";
constexpr std::string_view ImageName = "world_map";

// In percent of the map's frame, which the document positions the labels and the marker in.
double percent(float unit) {
  constexpr double Hundred = 100.0;
  return static_cast<double>(unit) * Hundred;
}

// The name of the map the player is on: a pack's map has a name of its own (mapNameKeyOf), an
// original one the place name of its entry.
std::string hereKey(const FieldScene& scene, std::optional<std::int32_t> herePlace) {
  const auto id = assets::ContentId::parse(scene.field().mapId());
  if (id && id->isNamespaced()) {
    return scene.nameKeyOf(scene.field().mapId());
  }
  return herePlace ? localization::mapNameKey(*herePlace) : std::string{};
}

} // namespace

WorldMapScreen::WorldMapScreen(WorldMapScreenDesc desc) : desc_(desc) {}

bool WorldMapScreen::load() {
  if (loadTried_) {
    return layout_.has_value();
  }
  loadTried_ = true;
  const auto id = assets::AssetId::parse(WorldMapLayoutAsset);
  auto bytes = id ? desc_.files->read(*id) : core::Result<std::vector<std::byte>>{};
  if (!id || !bytes) {
    core::logWarning("world map: {} cannot be read; the M key opens nothing", WorldMapLayoutAsset);
    return false;
  }
  const std::string_view text{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
  auto layout = parseWorldMapLayout(text);
  if (!layout) {
    core::logWarning("world map: {}", layout.error().message);
    return false;
  }
  layout_ = std::move(*layout);
  // Without the scripts' map changes the exits list only the walk warps.
  if (auto warps = game::field::loadScriptWarps(*desc_.files)) {
    scriptWarps_ = std::move(*warps);
  } else {
    core::logWarning("world map: field/script_warps: {}", warps.error().message);
  }
  return true;
}

bool WorldMapScreen::open(const FieldScene& scene,
                          const std::set<std::string, std::less<>>& visited) {
  const assets::MapTable* maps = scene.mapTable();
  if (maps == nullptr || !load()) {
    return false;
  }
  const std::string& mapId = scene.field().mapId();
  const std::optional<std::size_t> here = layout_->regionOfMap(*maps, mapId);
  const std::vector<bool> known = knownRegions(*layout_, *maps, visited, here);
  if (image_.empty() || known != paintedKnown_) {
    image_ =
        desc_.ui->setGeneratedImage(ImageName, ui::paintWorldMap(worldMapPicture(*layout_, known)));
    paintedKnown_ = known;
  }
  ++opened_;
  screens::publish(*desc_.ui, settings(scene, known), Revision);
  desc_.ui->show(WorldMapDocument, true);
  open_ = true;
  core::logDebug("world map: opened on {} (area {})", mapId,
                 here ? layout_->regions[*here].id : std::string{"none"});
  return true;
}

void WorldMapScreen::close() {
  if (open_) {
    desc_.ui->hide(WorldMapDocument);
    open_ = false;
  }
}

screens::ScreenSettings WorldMapScreen::settings(const FieldScene& scene,
                                                 const std::vector<bool>& known) const {
  const WorldMapLayout& layout = *layout_;
  const assets::MapTable& maps = *scene.mapTable();
  const std::string& mapId = scene.field().mapId();
  const assets::MapEntry* entry = maps.find(mapId);
  const std::optional<std::int32_t> herePlace =
      entry != nullptr ? std::optional{entry->loadingName} : std::nullopt;
  const std::optional<std::size_t> here = layout.regionOfMap(maps, mapId);
  const std::vector<WorldMapExit> exits =
      worldMapExits(layout, maps, exitMapsOf(scene.map(), mapId, scriptWarps_), herePlace);
  const std::optional<ui::WorldMapPoint> marker = worldMapMarker(layout, here);

  screens::ScreenSettings out{
      {.name = "world_map.opened", .value = static_cast<double>(opened_)},
      {.name = "world_map.image", .value = image_},
      {.name = "world_map.aspect", .value = static_cast<double>(layout.width / layout.height)},
      {.name = "world_map.here_key", .value = hereKey(scene, herePlace)},
      {.name = "world_map.area_key",
       .value = here ? localization::mapNameKey(layout.regions[*here].name) : std::string{}},
      {.name = "world_map.marker", .value = marker.has_value()},
      {.name = "world_map.marker_x", .value = marker ? percent(marker->u) : 0.0},
      {.name = "world_map.marker_y", .value = marker ? percent(marker->v) : 0.0},
      {.name = "world_map.regions", .value = static_cast<double>(layout.regions.size())},
      {.name = "world_map.exits", .value = static_cast<double>(exits.size())},
  };
  for (std::size_t i = 0; i < layout.regions.size(); ++i) {
    const WorldMapRegion& region = layout.regions[i];
    const ui::WorldMapPoint at = ui::locateOnWorldMap(
        {.width = layout.width, .height = layout.height, .areas = {}, .roads = {}}, region.x,
        region.y);
    const bool exit = std::ranges::any_of(
        exits, [i](const WorldMapExit& place) { return place.region == i && place.region; });
    out.push_back({.name = screens::listEntryName(RegionList, i, "name"),
                   .value = localization::mapNameKey(region.name)});
    out.push_back({.name = screens::listEntryName(RegionList, i, "x"), .value = percent(at.u)});
    out.push_back({.name = screens::listEntryName(RegionList, i, "y"), .value = percent(at.v)});
    out.push_back({.name = screens::listEntryName(RegionList, i, "known"), .value = known[i]});
    out.push_back({.name = screens::listEntryName(RegionList, i, "here"), .value = here == i});
    out.push_back({.name = screens::listEntryName(RegionList, i, "exit"), .value = exit});
  }
  for (std::size_t i = 0; i < exits.size(); ++i) {
    const std::optional<std::size_t> region = exits[i].region;
    out.push_back({.name = screens::listEntryName(ExitList, i, "name"),
                   .value = localization::mapNameKey(exits[i].place)});
    // The area's name too when the place is not the area itself (Lava Cave, in the Drill Tunnel).
    const bool sameName = region && layout.regions[*region].name == exits[i].place;
    out.push_back({.name = screens::listEntryName(ExitList, i, "area"),
                   .value = region && !sameName && region != here
                                ? localization::mapNameKey(layout.regions[*region].name)
                                : std::string{}});
  }
  return out;
}

} // namespace dwe::app
