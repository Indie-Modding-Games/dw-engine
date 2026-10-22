#include "language.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>
#include <format>
#include <utility>

#include "messages.hpp"
#include "options.hpp"

namespace dwe::app {
namespace {

constexpr std::string_view LanguageListSeparator = ", ";

std::string languageList(std::span<const std::string> languages) {
  std::string list;
  for (const std::string& language : languages) {
    list += list.empty() ? language : std::format("{}{}", LanguageListSeparator, language);
  }
  return list;
}

} // namespace

core::Result<std::string> startLanguage(std::span<const std::string> available,
                                        const std::optional<std::string>& commandLine,
                                        const std::optional<std::string>& saved,
                                        std::string_view system) {
  if (commandLine) {
    if (!std::ranges::contains(available, *commandLine)) {
      return core::fail(
          std::format(messages::UnknownLanguage, *commandLine, languageList(available)));
    }
    core::logInfo(messages::StartLanguage, *commandLine, LanguageOption);
    return *commandLine;
  }
  if (saved) {
    if (std::ranges::contains(available, *saved)) {
      core::logInfo(messages::StartLanguage, *saved, messages::FromSavedSetting);
      return *saved;
    }
    core::logWarning(messages::SavedLanguageGone, *saved);
  }
  std::string picked = localization::pickLanguage(available, system);
  core::logInfo(messages::StartLanguage, picked, messages::FromSystem);
  return picked;
}

LanguageChanger::LanguageChanger(localization::Catalog& texts, ui::Ui& ui, UserSettings& settings,
                                 std::optional<std::filesystem::path> settingsFile)
    : texts_{&texts}, ui_{&ui}, settings_{&settings}, settingsFile_{std::move(settingsFile)} {}

void LanguageChanger::change(std::string_view language) {
  if (auto changed = texts_->setLanguage(language); !changed) {
    core::logWarning(messages::CannotChangeLanguage, changed.error().message);
    return;
  }
  core::logInfo(messages::LanguageChanged, language);
  ui_->setSetting(LanguageSetting, std::string{language});
  ui_->refreshTexts();
  settings_->language = std::string{language};
  if (settingsFile_) {
    saveUserSettings(*settingsFile_, *settings_);
  }
}

} // namespace dwe::app
