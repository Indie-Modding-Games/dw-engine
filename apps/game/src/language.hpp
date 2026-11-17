#pragma once

#include <dwe/core/error.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/ui/ui.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "user_settings.hpp"

namespace dwe::app {

// The UI setting with the player's language and the command that changes it.
inline constexpr std::string_view LanguageSetting = "language";
inline constexpr std::string_view LanguageCommand = "language";
// The title's setting that shows the language box before the "Press Start" screen (an addition).
inline constexpr std::string_view LanguageFirstSetting = "title.language_first";

// The language the game starts in: --lang when given (an error if the packs do not have it), else
// the saved setting when the packs still have it, else the system's (localization::pickLanguage).
core::Result<std::string> startLanguage(std::span<const std::string> available,
                                        const std::optional<std::string>& commandLine,
                                        const std::optional<std::string>& saved,
                                        std::string_view system);

// Whether the title asks the language on this boot: only until the player has chosen one (here or
// on the first start's screen) and --lang does not name one. The options page changes it later.
[[nodiscard]] bool asksLanguage(const std::optional<std::string>& commandLine,
                                const std::optional<std::string>& saved);

// Changes the player's language while the game runs: the catalog, every text the UI shows, and the
// saved setting. Once chosen, the title no longer asks it (LanguageFirstSetting).
class LanguageChanger {
public:
  // settingsFile is where the choice is saved; nothing for runs that must not touch the user's
  // settings (autotests). Everything given must outlive the changer.
  LanguageChanger(localization::Catalog& texts, ui::Ui& ui, UserSettings& settings,
                  std::optional<std::filesystem::path> settingsFile);

  // Switches to a language the packs have; a failure is logged and keeps the current one.
  void change(std::string_view language);

private:
  localization::Catalog* texts_;
  ui::Ui* ui_;
  UserSettings* settings_;
  std::optional<std::filesystem::path> settingsFile_;
};

} // namespace dwe::app
