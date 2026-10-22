#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/platform/events.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/map_renderer.hpp>
#include <dwe/render/map_scene.hpp>
#include <dwe/render/renderer.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dwe::app {

// What `dwe-game --map <id>` shows.
struct MapPreviewDesc {
  std::filesystem::path content;
  // The map shown first, then the others in turn with debug_next_map.
  std::vector<std::string> mapIds;
  render::TimeOfDay time = render::TimeOfDay::Day;
  // The focus point on the ground (world x, z); the map's exit 0 when not given.
  std::optional<glm::vec2> focus;
};

// A development view of a field map until the field scene exists: the map drawn by MapRenderer with
// a marker the size of a character standing on a focus point that the camera follows. The move
// actions walk the marker relative to the camera, a click sends it to the ground point under the
// cursor (MapCamera::pickGround), debug_time_day, debug_time_dusk and debug_time_night fade to that
// time of day, and debug_next_map switches to the next map of the list, logging how long the switch
// took.
class MapPreview {
public:
  static core::Result<std::unique_ptr<MapPreview>> create(render::Renderer& renderer,
                                                          const MapPreviewDesc& desc);

  MapPreview(const MapPreview&) = delete;
  MapPreview& operator=(const MapPreview&) = delete;
  MapPreview(MapPreview&&) = delete;
  MapPreview& operator=(MapPreview&&) = delete;
  ~MapPreview() = default;

  // Sees raw events the preview reacts to: clicks, with the cursor position. MouseMoved must give
  // drawable pixels (the platform scales window points by the pixel density), the space
  // MapCamera::pickGround and the viewport use.
  void handle(const platform::Event& event);

  // One logic tick.
  void tick(const input::InputState& input, double seconds);

  // Prepares the next frame.
  void update(double frameSeconds);

  [[nodiscard]] render::Drawer& backgroundDrawer() { return map_->backgroundDrawer(); }
  [[nodiscard]] render::Drawer& layerDrawer() { return map_->layerDrawer(); }

private:
  // The mounted content, kept to load the next map: the VFS reads through the packs, which read
  // through the disk.
  struct Content {
    assets::DiskFileSystem disk;
    assets::OpenedPacks packs;
    // Set by create before any map is shown.
    std::unique_ptr<assets::VirtualFileSystem> vfs;
  };

  MapPreview(std::unique_ptr<Content> content, std::unique_ptr<render::MapRenderer> map,
             MapPreviewDesc desc);

  // Loads a map and shows it with the marker on its focus point.
  core::Result<void> show(std::size_t mapIndex, std::optional<glm::vec2> focus);
  void showNextMap();

  std::unique_ptr<Content> content_;
  std::unique_ptr<render::MapRenderer> map_;
  MapPreviewDesc desc_;
  std::size_t mapIndex_ = 0;
  render::MapSceneData scene_;
  // The map keeps its day palette whatever the time (MapEntry::fixedTimeOfDay).
  bool fixedTime_ = false;
  glm::vec3 focus_{0.0F};
  std::optional<glm::vec3> target_;
  glm::vec2 cursor_{0.0F};
  // Frame times still to log after a map switch.
  int framesToLog_ = 0;
};

} // namespace dwe::app
