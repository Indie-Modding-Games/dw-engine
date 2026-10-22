#pragma once

#include <dwe/ui/ui.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "user_settings.hpp"

namespace dwe::app {

// The skin the UI starts with: --ui-skin when given, else the saved setting, else nothing, which
// keeps the packs' default (ui/ui_skin.yaml). The UI checks that the packs offer it.
std::optional<std::string> startUiSkin(const std::optional<std::string>& commandLine,
                                       const std::optional<std::string>& saved);

// Changes the player's skin while the game runs: the UI, the published setting and the saved
// setting.
class UiSkinChanger {
public:
  // settingsFile is where the choice is saved; nothing for runs that must not touch the user's
  // settings (autotests). Everything given must outlive the changer.
  UiSkinChanger(ui::Ui& ui, UserSettings& settings,
                std::optional<std::filesystem::path> settingsFile);

  // Switches to a skin the packs offer, by its id; any other id is logged and changes nothing.
  void change(std::string_view id);

private:
  ui::Ui* ui_;
  UserSettings* settings_;
  std::optional<std::filesystem::path> settingsFile_;
};

// Puts on the skin the run starts with, before the documents load, and publishes the one shown as
// the ui_skin setting. A start skin the packs do not offer is logged and the packs' default stays.
void applyStartUiSkin(ui::Ui& ui, const std::optional<std::string>& skin);

} // namespace dwe::app
