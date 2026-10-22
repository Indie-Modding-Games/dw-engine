#pragma once

#include <dwe/ui/text_font.hpp>
#include <dwe/ui/ui.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "user_settings.hpp"

namespace dwe::app {

// The font "original look" text starts with: --text-font when given, else the saved setting when it
// names a font, else nothing, which keeps the packs' default (ui/fonts/original_look.yaml).
std::optional<ui::TextFont> startTextFont(const std::optional<ui::TextFont>& commandLine,
                                          const std::optional<std::string>& saved);

// Changes the player's text font while the game runs: the UI, the published setting and the saved
// setting.
class TextFontChanger {
public:
  // settingsFile is where the choice is saved; nothing for runs that must not touch the user's
  // settings (autotests). Everything given must outlive the changer.
  TextFontChanger(ui::Ui& ui, UserSettings& settings,
                  std::optional<std::filesystem::path> settingsFile);

  // Switches to a font by its name (modern, original); an unknown name is logged and changes
  // nothing.
  void change(std::string_view name);

private:
  ui::Ui* ui_;
  UserSettings* settings_;
  std::optional<std::filesystem::path> settingsFile_;
};

} // namespace dwe::app
