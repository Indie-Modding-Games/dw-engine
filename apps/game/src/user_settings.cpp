#include "user_settings.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/platform/platform.hpp>
#include <dwe/ui/skin.hpp>

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "common.hpp"
#include "messages.hpp"

namespace dwe::app {
namespace {

// The settings file in the game's user data folder (common.hpp).
constexpr std::string_view SettingsFileName = "settings.yaml";

constexpr std::string_view LanguageKey = "language";
constexpr std::string_view TextFontKey = "text_font";
constexpr std::string_view MinimapKey = "minimap";
constexpr std::string_view FreeExplorationKey = "free_exploration";
constexpr std::string_view SmoothMotionKey = "smooth_motion";
constexpr std::string_view SaveNoticeKey = "save_notice";
constexpr std::string_view DigitalTimeKey = "digital_time";
constexpr std::string_view BulkTradesKey = "bulk_trades";
constexpr std::string_view EvolutionRequirementsKey = "evolution_requirements";

} // namespace

core::Result<UserSettings> parseUserSettings(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  assets::YamlReader reader;
  UserSettings settings;
  if (!tree->isMapping()) {
    reader.fail(*tree, messages::UserSettingsWhat);
  } else {
    if (const core::YamlNode* language = tree->find(LanguageKey)) {
      settings.language = reader.text(*language);
    }
    if (const core::YamlNode* textFont = tree->find(TextFontKey)) {
      settings.textFont = reader.text(*textFont);
    }
    if (const core::YamlNode* uiSkin = tree->find(ui::UiSkinSetting)) {
      settings.uiSkin = reader.text(*uiSkin);
    }
    if (const core::YamlNode* minimap = tree->find(MinimapKey)) {
      settings.minimap = reader.text(*minimap);
    }
    if (const core::YamlNode* explore = tree->find(FreeExplorationKey)) {
      settings.freeExploration = reader.boolean(*explore);
    }
    if (const core::YamlNode* smooth = tree->find(SmoothMotionKey)) {
      settings.smoothMotion = reader.boolean(*smooth);
    }
    if (const core::YamlNode* notice = tree->find(SaveNoticeKey)) {
      settings.saveNotice = reader.boolean(*notice);
    }
    if (const core::YamlNode* digits = tree->find(DigitalTimeKey)) {
      settings.digitalTime = reader.boolean(*digits);
    }
    if (const core::YamlNode* bulk = tree->find(BulkTradesKey)) {
      settings.bulkTrades = reader.boolean(*bulk);
    }
    if (const core::YamlNode* shown = tree->find(EvolutionRequirementsKey)) {
      settings.evolutionRequirements = reader.boolean(*shown);
    }
  }
  return reader.finish(std::move(settings), messages::UserSettingsContext);
}

core::Result<std::string> formatUserSettings(const UserSettings& settings) {
  core::YamlNode root = core::YamlNode::mapping();
  if (settings.language) {
    root.set(std::string{LanguageKey}, core::YamlNode::scalar(*settings.language));
  }
  if (settings.textFont) {
    root.set(std::string{TextFontKey}, core::YamlNode::scalar(*settings.textFont));
  }
  if (settings.uiSkin) {
    root.set(std::string{ui::UiSkinSetting}, core::YamlNode::scalar(*settings.uiSkin));
  }
  if (settings.minimap) {
    root.set(std::string{MinimapKey}, core::YamlNode::scalar(*settings.minimap));
  }
  if (settings.freeExploration) {
    root.set(std::string{FreeExplorationKey},
             core::YamlNode::scalar(*settings.freeExploration ? "true" : "false"));
  }
  if (settings.smoothMotion) {
    root.set(std::string{SmoothMotionKey},
             core::YamlNode::scalar(*settings.smoothMotion ? "true" : "false"));
  }
  if (settings.saveNotice) {
    root.set(std::string{SaveNoticeKey},
             core::YamlNode::scalar(*settings.saveNotice ? "true" : "false"));
  }
  if (settings.digitalTime) {
    root.set(std::string{DigitalTimeKey},
             core::YamlNode::scalar(*settings.digitalTime ? "true" : "false"));
  }
  if (settings.bulkTrades) {
    root.set(std::string{BulkTradesKey},
             core::YamlNode::scalar(*settings.bulkTrades ? "true" : "false"));
  }
  if (settings.evolutionRequirements) {
    root.set(std::string{EvolutionRequirementsKey},
             core::YamlNode::scalar(*settings.evolutionRequirements ? "true" : "false"));
  }
  return core::emitYaml(root);
}

std::optional<std::filesystem::path> userSettingsFile() {
  auto folder = platform::userDataFolder(UserDataOrganization, UserDataApplication);
  if (!folder) {
    core::logWarning(messages::NoUserSettingsFolder, folder.error().message);
    return std::nullopt;
  }
  return *folder / SettingsFileName;
}

UserSettings loadUserSettings(const std::filesystem::path& file) {
  std::error_code error;
  if (!std::filesystem::exists(file, error)) {
    return {};
  }
  std::ifstream stream{file, std::ios::binary};
  const std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
  if (!stream) {
    core::logWarning(messages::CannotReadUserSettings, assets::pathText(file),
                     messages::CannotOpenFile);
    return {};
  }
  auto settings = parseUserSettings(text);
  if (!settings) {
    core::logWarning(messages::CannotReadUserSettings, assets::pathText(file),
                     settings.error().message);
    return {};
  }
  return std::move(*settings);
}

void saveUserSettings(const std::filesystem::path& file, const UserSettings& settings) {
  auto text = formatUserSettings(settings);
  if (!text) {
    core::logWarning(messages::CannotWriteUserSettings, assets::pathText(file),
                     text.error().message);
    return;
  }
  std::ofstream stream{file, std::ios::binary | std::ios::trunc};
  stream << *text;
  if (!stream) {
    core::logWarning(messages::CannotWriteUserSettings, assets::pathText(file),
                     messages::CannotOpenFile);
  }
}

} // namespace dwe::app
