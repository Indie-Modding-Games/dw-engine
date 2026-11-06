#include "user_settings.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/platform/platform.hpp>
#include <dwe/ui/skin.hpp>

#include <fstream>
#include <iterator>
#include <string>
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
constexpr std::string_view AutosaveKey = "autosave";
constexpr std::string_view DigitalTimeKey = "digital_time";
constexpr std::string_view BulkTradesKey = "bulk_trades";
constexpr std::string_view EvolutionRequirementsKey = "evolution_requirements";
constexpr std::string_view TranslatedSignsKey = "translated_signs";
constexpr std::string_view ModelCharactersKey = "model_characters";
constexpr std::string_view StatusExtrasKey = "status_extras";
constexpr std::string_view CanonicalStagesKey = "canonical_stages";
constexpr std::string_view WalkControlKey = "walk_control";
constexpr std::string_view LongNamesKey = "long_names";
constexpr std::string_view ShopJobBarKey = "shop_job_bar";
constexpr std::string_view ContinuousMusicKey = "continuous_music";
constexpr std::string_view NeedBubbleStackKey = "need_bubble_stack";
constexpr std::string_view WorldMapKey = "world_map";
constexpr std::string_view ChibiStyleKey = "chibi_style";
constexpr std::string_view MinimapEnemiesKey = "minimap_enemies";
constexpr std::string_view ShadowsKey = "shadows";
constexpr std::string_view EnhancedLightingKey = "enhanced_lighting";
constexpr std::string_view VividColorsKey = "vivid_colors";
constexpr std::string_view SceneryQualityKey = "scenery_quality";
constexpr std::string_view AntialiasingKey = "antialiasing";
constexpr std::string_view TextureFilterKey = "texture_filter";
constexpr std::string_view WeatherKey = "weather";
constexpr std::string_view SeasonsKey = "seasons";
constexpr std::string_view HdSceneryKey = "hd_scenery";
constexpr std::string_view MusicVolumeKey = "music_volume";
constexpr std::string_view EffectsVolumeKey = "effects_volume";

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
    if (const core::YamlNode* autosave = tree->find(AutosaveKey)) {
      settings.autosave = reader.boolean(*autosave);
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
    if (const core::YamlNode* signs = tree->find(TranslatedSignsKey)) {
      settings.translatedSigns = reader.boolean(*signs);
    }
    if (const core::YamlNode* models = tree->find(ModelCharactersKey)) {
      settings.modelCharacters = reader.boolean(*models);
    }
    if (const core::YamlNode* extras = tree->find(StatusExtrasKey)) {
      settings.statusExtras = reader.boolean(*extras);
    }
    if (const core::YamlNode* stages = tree->find(CanonicalStagesKey)) {
      settings.canonicalStages = reader.boolean(*stages);
    }
    if (const core::YamlNode* walk = tree->find(WalkControlKey)) {
      settings.walkControl = reader.text(*walk);
    }
    if (const core::YamlNode* names = tree->find(LongNamesKey)) {
      settings.longNames = reader.boolean(*names);
    }
    if (const core::YamlNode* bar = tree->find(ShopJobBarKey)) {
      settings.shopJobBar = reader.boolean(*bar);
    }
    if (const core::YamlNode* music = tree->find(ContinuousMusicKey)) {
      settings.continuousMusic = reader.boolean(*music);
    }
    if (const core::YamlNode* stack = tree->find(NeedBubbleStackKey)) {
      settings.needBubbleStack = reader.boolean(*stack);
    }
    if (const core::YamlNode* worldMap = tree->find(WorldMapKey)) {
      settings.worldMap = reader.boolean(*worldMap);
    }
    if (const core::YamlNode* chibi = tree->find(ChibiStyleKey)) {
      settings.chibiStyle = reader.text(*chibi);
    }
    if (const core::YamlNode* enemies = tree->find(MinimapEnemiesKey)) {
      settings.minimapEnemies = reader.boolean(*enemies);
    }
    if (const core::YamlNode* shadows = tree->find(ShadowsKey)) {
      settings.shadows = reader.text(*shadows);
    }
    if (const core::YamlNode* lighting = tree->find(EnhancedLightingKey)) {
      settings.enhancedLighting = reader.boolean(*lighting);
    }
    if (const core::YamlNode* vivid = tree->find(VividColorsKey)) {
      settings.vividColors = reader.boolean(*vivid);
    }
    if (const core::YamlNode* quality = tree->find(SceneryQualityKey)) {
      settings.sceneryQuality = reader.text(*quality);
    }
    if (const core::YamlNode* antialiasing = tree->find(AntialiasingKey)) {
      settings.antialiasing = reader.boolean(*antialiasing);
    }
    if (const core::YamlNode* filter = tree->find(TextureFilterKey)) {
      settings.textureFilter = reader.text(*filter);
    }
    if (const core::YamlNode* weather = tree->find(WeatherKey)) {
      settings.weather = reader.boolean(*weather);
    }
    if (const core::YamlNode* seasons = tree->find(SeasonsKey)) {
      settings.seasons = reader.boolean(*seasons);
    }
    if (const core::YamlNode* hd = tree->find(HdSceneryKey)) {
      settings.hdScenery = reader.boolean(*hd);
    }
    if (const core::YamlNode* music = tree->find(MusicVolumeKey)) {
      settings.musicVolume = reader.integer<int>(*music);
    }
    if (const core::YamlNode* effects = tree->find(EffectsVolumeKey)) {
      settings.effectsVolume = reader.integer<int>(*effects);
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
  if (settings.autosave) {
    root.set(std::string{AutosaveKey},
             core::YamlNode::scalar(*settings.autosave ? "true" : "false"));
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
  if (settings.translatedSigns) {
    root.set(std::string{TranslatedSignsKey},
             core::YamlNode::scalar(*settings.translatedSigns ? "true" : "false"));
  }
  if (settings.modelCharacters) {
    root.set(std::string{ModelCharactersKey},
             core::YamlNode::scalar(*settings.modelCharacters ? "true" : "false"));
  }
  if (settings.statusExtras) {
    root.set(std::string{StatusExtrasKey},
             core::YamlNode::scalar(*settings.statusExtras ? "true" : "false"));
  }
  if (settings.canonicalStages) {
    root.set(std::string{CanonicalStagesKey},
             core::YamlNode::scalar(*settings.canonicalStages ? "true" : "false"));
  }
  if (settings.walkControl) {
    root.set(std::string{WalkControlKey}, core::YamlNode::scalar(*settings.walkControl));
  }
  if (settings.longNames) {
    root.set(std::string{LongNamesKey},
             core::YamlNode::scalar(*settings.longNames ? "true" : "false"));
  }
  if (settings.shopJobBar) {
    root.set(std::string{ShopJobBarKey},
             core::YamlNode::scalar(*settings.shopJobBar ? "true" : "false"));
  }
  if (settings.continuousMusic) {
    root.set(std::string{ContinuousMusicKey},
             core::YamlNode::scalar(*settings.continuousMusic ? "true" : "false"));
  }
  if (settings.needBubbleStack) {
    root.set(std::string{NeedBubbleStackKey},
             core::YamlNode::scalar(*settings.needBubbleStack ? "true" : "false"));
  }
  if (settings.worldMap) {
    root.set(std::string{WorldMapKey},
             core::YamlNode::scalar(*settings.worldMap ? "true" : "false"));
  }
  if (settings.chibiStyle) {
    root.set(std::string{ChibiStyleKey}, core::YamlNode::scalar(*settings.chibiStyle));
  }
  if (settings.minimapEnemies) {
    root.set(std::string{MinimapEnemiesKey},
             core::YamlNode::scalar(*settings.minimapEnemies ? "true" : "false"));
  }
  if (settings.shadows) {
    root.set(std::string{ShadowsKey}, core::YamlNode::scalar(*settings.shadows));
  }
  if (settings.enhancedLighting) {
    root.set(std::string{EnhancedLightingKey},
             core::YamlNode::scalar(*settings.enhancedLighting ? "true" : "false"));
  }
  if (settings.vividColors) {
    root.set(std::string{VividColorsKey},
             core::YamlNode::scalar(*settings.vividColors ? "true" : "false"));
  }
  if (settings.sceneryQuality) {
    root.set(std::string{SceneryQualityKey}, core::YamlNode::scalar(*settings.sceneryQuality));
  }
  if (settings.antialiasing) {
    root.set(std::string{AntialiasingKey},
             core::YamlNode::scalar(*settings.antialiasing ? "true" : "false"));
  }
  if (settings.textureFilter) {
    root.set(std::string{TextureFilterKey}, core::YamlNode::scalar(*settings.textureFilter));
  }
  if (settings.weather) {
    root.set(std::string{WeatherKey}, core::YamlNode::scalar(*settings.weather ? "true" : "false"));
  }
  if (settings.seasons) {
    root.set(std::string{SeasonsKey}, core::YamlNode::scalar(*settings.seasons ? "true" : "false"));
  }
  if (settings.hdScenery) {
    root.set(std::string{HdSceneryKey},
             core::YamlNode::scalar(*settings.hdScenery ? "true" : "false"));
  }
  if (settings.musicVolume) {
    root.set(std::string{MusicVolumeKey},
             core::YamlNode::scalar(std::to_string(*settings.musicVolume)));
  }
  if (settings.effectsVolume) {
    root.set(std::string{EffectsVolumeKey},
             core::YamlNode::scalar(std::to_string(*settings.effectsVolume)));
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
