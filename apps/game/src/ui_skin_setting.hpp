#pragma once

#include <dwe/ui/ui.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "user_settings.hpp"

namespace dwe::app {

// The skin the UI starts with: --ui-skin when given, else the saved setting, else nothing, which
// keeps the packs' default (ui/ui_skin.yaml). The UI checks that the packs offer it. A saved skin
// the game pack no longer has starts as the one that replaced it (legacy: original).
std::optional<std::string> startUiSkin(const std::optional<std::string>& commandLine,
                                       const std::optional<std::string>& saved);

// The command the first run's skin picker (ui/skin_picker) sends as its cursor moves: the skin is
// shown, not saved, until the player confirms one with the ui_skin command.
inline constexpr std::string_view UiSkinPreviewCommand = "ui_skin_preview";

// The first run's box that asks the UI skin, and the command it sends once one is confirmed (its
// ui_skin command, sent first, saved it).
inline constexpr std::string_view SkinPickerDocument = "skin_picker";
inline constexpr std::string_view SkinPickerDoneCommand = "skin_picker_done";

// Whether the title asks the player's skin first (ui/skin_picker, before the language box): when
// neither --ui-skin nor a saved choice names one and the packs offer more than one.
[[nodiscard]] bool asksUiSkin(const std::optional<std::string>& commandLine,
                              const std::optional<std::string>& saved, std::size_t offered);

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
  // Shows a skin the packs offer and publishes it, without saving it (UiSkinPreviewCommand); false,
  // logged, for any other id.
  bool preview(std::string_view id);

private:
  ui::Ui* ui_;
  UserSettings* settings_;
  std::optional<std::filesystem::path> settingsFile_;
};

// The title's first step on the player's first run (an addition): the skin picker, before the
// language box, until a skin is confirmed; asked once.
class SkinPickerStep {
public:
  explicit SkinPickerStep(bool asks) : asks_{asks} {}

  // Shows the picker while the skin is still to be asked; false when it is not, and the title goes
  // on.
  bool show(ui::Ui& ui) const;
  // Hides it on its done command, for the title to go on; false for any other command.
  bool handle(const ui::UiCommand& command, ui::Ui& ui);
  void hide(ui::Ui& ui) const;

private:
  bool asks_;
};

// Puts on the skin the run starts with, before the documents load, and publishes the one shown as
// the ui_skin setting. A start skin the packs do not offer is logged and the packs' default stays.
void applyStartUiSkin(ui::Ui& ui, const std::optional<std::string>& skin);

} // namespace dwe::app
