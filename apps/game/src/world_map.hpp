#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/field/script_warps.hpp>
#include <dwe/ui/world_map.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

// The island map (a dwe improvement, docs/modulos/apps/game.md «Mapa de la isla»): the original has
// no map of the island. M (or the pad's left shoulder) opens it over the field, which waits.
inline constexpr std::string_view WorldMapDocument = "world_map";
inline constexpr std::string_view WorldMapLayoutAsset = "ui/world_map/layout";
// The user setting that lets M open it (settings.yaml `world_map`, on by default).
inline constexpr std::string_view WorldMapSetting = "world_map";
// The action that opens and closes it.
inline constexpr std::string_view WorldMapAction = "world_map";

// An area of the island: its name (a slot of main.map_names), the names of the maps that lie in it
// (MapEntry::loadingName) and where it is drawn.
struct WorldMapRegion {
  std::string id;
  std::int32_t name = 0;
  std::vector<std::int32_t> places;
  float x = 0.0F;
  float y = 0.0F;
  float radius = 0.0F;
  std::uint32_t colour = 0;
};

// The island's areas and the roads between them (ui/world_map/layout.yaml).
struct WorldMapLayout {
  float width = 0.0F;
  float height = 0.0F;
  std::vector<WorldMapRegion> regions;
  std::vector<ui::WorldMapRoad> roads;

  // The area a map's name (MapEntry::loadingName) lies in; nothing when no area lists it.
  [[nodiscard]] std::optional<std::size_t> regionOfPlace(std::int32_t place) const;
  // The area a map lies in, by its id in the map table.
  [[nodiscard]] std::optional<std::size_t> regionOfMap(const assets::MapTable& maps,
                                                       std::string_view mapId) const;
};

// Reads the layout; an error names what is wrong (a missing key, a road to an unknown area, a
// place listed by two areas, a colour that is not #rrggbb).
[[nodiscard]] core::Result<WorldMapLayout> parseWorldMapLayout(std::string_view yaml);

// The areas the player knows: those with a visited map, and the one he is in.
[[nodiscard]] std::vector<bool> knownRegions(const WorldMapLayout& layout,
                                             const assets::MapTable& maps,
                                             const std::set<std::string, std::less<>>& visited,
                                             std::optional<std::size_t> here);

// The picture the painter draws: the layout with the known areas.
[[nodiscard]] ui::WorldMapPicture worldMapPicture(const WorldMapLayout& layout,
                                                  const std::vector<bool>& known);

// Where the player's marker goes on the map, from 0 to 1 on each axis: the centre of his area.
[[nodiscard]] std::optional<ui::WorldMapPoint> worldMapMarker(const WorldMapLayout& layout,
                                                              std::optional<std::size_t> here);

// The maps a map's exits lead to, in order and without repeats: its walk warps whose trigger cell
// (110 + exit) the map has and that lead to a map (checkMapInteraction), then the map
// changes its event scripts make on a step or a confirm (field/script_warps), whatever their
// conditions.
[[nodiscard]] std::vector<std::string>
exitMapsOf(const assets::MapDocument& map, std::string_view mapId,
           std::span<const game::field::ScriptWarp> scriptWarps);

// A place the player can go from where he is: the name the original shows for it
// (MapEntry::loadingName) and the area it lies in.
struct WorldMapExit {
  std::int32_t place = 0;
  std::optional<std::size_t> region;

  friend bool operator==(const WorldMapExit&, const WorldMapExit&) = default;
};

// The places the exits lead to, without repeats and without the player's own place.
[[nodiscard]] std::vector<WorldMapExit> worldMapExits(const WorldMapLayout& layout,
                                                      const assets::MapTable& maps,
                                                      std::span<const std::string> exitMaps,
                                                      std::optional<std::int32_t> herePlace);

// What a press of the map's key or of cancel does: M opens the map where the player walks freely
// (no window, script or fade holds the game) and the setting allows it; M or cancel close it.
enum class WorldMapKeyStep : std::uint8_t { None, Open, Close };
struct WorldMapKeys {
  bool mapPressed = false;
  bool cancelPressed = false;
  bool open = false;
  bool canOpen = false;
  bool enabled = true;
};
[[nodiscard]] WorldMapKeyStep worldMapKeyStep(const WorldMapKeys& keys);

} // namespace dwe::app
