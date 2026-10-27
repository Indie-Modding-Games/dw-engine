#pragma once

#include <dwe/ui/text_font.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace dwe::app {

struct UiDemoOptions {
  // The content folder with the extracted packs (base).
  std::filesystem::path content;
  // The map whose minimap is shown; a built-in test grid when the map cannot be loaded.
  std::string map = "twna01";
  // The window's size in pixels.
  int width = 1280;
  int height = 720;
  // A file of virtual gamepad commands, read as it grows (see ui_demo.cpp), to drive the menu
  // without hardware.
  std::optional<std::filesystem::path> virtualGamepad;
  std::optional<std::string> uiSkin;
  std::optional<ui::TextFont> textFont;
  bool fishing = false;
  int bagSlots = 0;
};

// `dwe-game --ui-demo`: the pause menu and the HUD with the minimap of a map, a player and a
// partner walking on it, and the menu's commands applied. For trying menus and for scripted input
// on the host.
int runUiDemo(const UiDemoOptions& options);

} // namespace dwe::app
