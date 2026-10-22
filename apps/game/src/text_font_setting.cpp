#include "text_font_setting.hpp"

#include <dwe/core/log.hpp>

#include <utility>

#include "messages.hpp"

namespace dwe::app {

std::optional<ui::TextFont> startTextFont(const std::optional<ui::TextFont>& commandLine,
                                          const std::optional<std::string>& saved) {
  if (commandLine) {
    return commandLine;
  }
  if (!saved) {
    return std::nullopt;
  }
  const std::optional<ui::TextFont> font = ui::textFontNamed(*saved);
  if (!font) {
    core::logWarning(messages::UnknownSavedTextFont, *saved);
  }
  return font;
}

TextFontChanger::TextFontChanger(ui::Ui& ui, UserSettings& settings,
                                 std::optional<std::filesystem::path> settingsFile)
    : ui_{&ui}, settings_{&settings}, settingsFile_{std::move(settingsFile)} {}

void TextFontChanger::change(std::string_view name) {
  const std::optional<ui::TextFont> font = ui::textFontNamed(name);
  if (!font) {
    core::logWarning(messages::UnknownTextFont, name);
    return;
  }
  ui_->setTextFont(*font);
  ui_->setSetting(ui::TextFontSetting, std::string{ui::textFontName(*font)});
  settings_->textFont = std::string{ui::textFontName(*font)};
  if (settingsFile_) {
    saveUserSettings(*settingsFile_, *settings_);
  }
}

} // namespace dwe::app
