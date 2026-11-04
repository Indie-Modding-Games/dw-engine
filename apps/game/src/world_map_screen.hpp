#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/field/script_warps.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/ui/ui.hpp>

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "world_map.hpp"

namespace dwe::app {

class FieldScene;

struct WorldMapScreenDesc {
  // Both must outlive the screen.
  ui::Ui* ui = nullptr;
  const assets::VirtualFileSystem* files = nullptr;
};

// The island map's window (world_map.hpp) on the world_map document: the island painted from the
// layout with the areas the player knows, where he is, and the places his map's exits lead to. It
// publishes them as settings (world_map.*), bumping world_map.revision once per opening; the
// document only draws them.
class WorldMapScreen {
public:
  explicit WorldMapScreen(WorldMapScreenDesc desc);

  // Opens the window over the scene: `visited` are the maps the player has stood on. False (and
  // nothing opens) when the packs have no layout, logged once.
  bool open(const FieldScene& scene, const std::set<std::string, std::less<>>& visited);
  void close();
  [[nodiscard]] bool isOpen() const { return open_; }

private:
  bool load();
  [[nodiscard]] screens::ScreenSettings settings(const FieldScene& scene,
                                                 const std::vector<bool>& known) const;

  WorldMapScreenDesc desc_;
  bool open_ = false;
  bool loadTried_ = false;
  std::optional<WorldMapLayout> layout_;
  std::vector<game::field::ScriptWarp> scriptWarps_;
  // The image shown, and the known areas it was painted with: it is painted again only when they
  // change.
  std::string image_;
  std::vector<bool> paintedKnown_;
  int opened_ = 0;
};

} // namespace dwe::app
