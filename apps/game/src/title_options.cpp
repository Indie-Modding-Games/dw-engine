#include "title_options.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <variant>

#include "dev_tools.hpp"

namespace dwe::app {
namespace {

// The settings' names are those of their owners' constants (play_session.hpp, free_exploration.hpp,
// world_map.hpp, bulk_amount.hpp, chart_requirements.hpp, sprite_characters.hpp, recruit_bar.hpp,
// minimap_enemies.hpp, save_indicator.hpp, play_options.hpp) and of ui/common/game_options.lua; the
// other rows of the page (walk control, autosave, the graphics, the volumes, weather, seasons, the
// minimap, the display, skin, language and font) are the game's own handlers at the title too
// (game.cpp).
constexpr std::array Toggles{
    // Game tab.
    GameToggle{
        .setting = "free_exploration", .saved = &UserSettings::freeExploration, .fallback = false},
    GameToggle{.setting = "world_map", .saved = &UserSettings::worldMap, .fallback = true},
    GameToggle{.setting = "bulk_trades", .saved = &UserSettings::bulkTrades, .fallback = true},
    GameToggle{.setting = "long_names", .saved = &UserSettings::longNames, .fallback = true},
    GameToggle{.setting = "evolution_requirements",
               .saved = &UserSettings::evolutionRequirements,
               .fallback = true},
    GameToggle{
        .setting = "canonical_stages", .saved = &UserSettings::canonicalStages, .fallback = false},
    GameToggle{.setting = "status_extras", .saved = &UserSettings::statusExtras, .fallback = true},
    GameToggle{.setting = "rare_items", .saved = &UserSettings::rareItems, .fallback = false},
    GameToggle{.setting = "enemy_hp_bars", .saved = &UserSettings::enemyHpBars, .fallback = false},
    GameToggle{.setting = "quick_battle_start",
               .saved = &UserSettings::quickBattleStart,
               .fallback = false},
    GameToggle{
        .setting = "skip_evolution", .saved = &UserSettings::skipEvolution, .fallback = false},
    GameToggle{.setting = "choice_cancel_closes",
               .saved = &UserSettings::choiceCancelCloses,
               .fallback = false},
    GameToggle{.setting = "fair_sickness_chance",
               .saved = &UserSettings::fairSicknessChance,
               .fallback = false},
    GameToggle{.setting = "fair_evolution_average",
               .saved = &UserSettings::fairEvolutionAverage,
               .fallback = false},
    GameToggle{.setting = "fair_move_forgetting",
               .saved = &UserSettings::fairMoveForgetting,
               .fallback = false},
    GameToggle{
        .setting = "fair_card_stock", .saved = &UserSettings::fairCardStock, .fallback = false},
    GameToggle{.setting = "choose_egg_after_lives_lost",
               .saved = &UserSettings::chooseEggAfterLivesLost,
               .fallback = false},
    GameToggle{.setting = "generous_talk", .saved = &UserSettings::generousTalk, .fallback = false},
    GameToggle{.setting = "extra_toilets", .saved = &UserSettings::extraToilets, .fallback = false},
    // Graphics tab.
    GameToggle{.setting = "smooth_motion", .saved = &UserSettings::smoothMotion, .fallback = true},
    GameToggle{
        .setting = "model_characters", .saved = &UserSettings::modelCharacters, .fallback = true},
    // Sound tab.
    GameToggle{
        .setting = "continuous_music", .saved = &UserSettings::continuousMusic, .fallback = true},
    // Interface tab.
    GameToggle{
        .setting = "translated_signs", .saved = &UserSettings::translatedSigns, .fallback = true},
    GameToggle{
        .setting = "minimap_enemies", .saved = &UserSettings::minimapEnemies, .fallback = true},
    GameToggle{.setting = "digital_time", .saved = &UserSettings::digitalTime, .fallback = true},
    GameToggle{.setting = "save_notice", .saved = &UserSettings::saveNotice, .fallback = true},
    GameToggle{
        .setting = "need_bubble_stack", .saved = &UserSettings::needBubbleStack, .fallback = true},
    GameToggle{.setting = "shop_job_bar", .saved = &UserSettings::shopJobBar, .fallback = true},
    GameToggle{.setting = "name_spacing", .saved = &UserSettings::nameSpacing, .fallback = true},
};

// The options the UI applies by itself (the documents read their settings), at the title and in a
// game alike: shop extras, the art fixes and the developer tools.
constexpr std::array UiToggles{
    GameToggle{.setting = "shop_extras", .saved = &UserSettings::shopExtras, .fallback = false},
    GameToggle{.setting = "art_fixes", .saved = &UserSettings::artFixes, .fallback = false},
    GameToggle{
        .setting = DevToolsSetting, .saved = &UserSettings::devTools, .fallback = DevToolsDefault},
};

// Keeps a toggle's value in the settings, saves them and publishes it.
void changeToggle(const GameToggle& toggle, const ui::UiCommand& command, ui::Ui& ui,
                  UserSettings& settings,
                  const std::optional<std::filesystem::path>& settingsFile) {
  std::optional<bool>& saved = settings.*toggle.saved;
  if (const bool* on = std::get_if<bool>(&command.value)) {
    saved = *on;
    if (settingsFile) {
      saveUserSettings(*settingsFile, settings);
    }
    core::logInfo("game: {} = {}", command.name, *on);
  }
  ui.setSetting(toggle.setting, saved.value_or(toggle.fallback));
}

} // namespace

std::span<const GameToggle> gameToggles() {
  return Toggles;
}

std::span<const GameToggle> uiToggles() {
  return UiToggles;
}

void publishGameToggles(ui::Ui& ui, const UserSettings& settings) {
  for (const auto& toggles : {std::span<const GameToggle>{Toggles}, uiToggles()}) {
    for (const GameToggle& toggle : toggles) {
      ui.setSetting(toggle.setting, (settings.*toggle.saved).value_or(toggle.fallback));
    }
  }
}

bool changeUiToggle(const ui::UiCommand& command, ui::Ui& ui, UserSettings& settings,
                    const std::optional<std::filesystem::path>& settingsFile) {
  const auto toggle = std::ranges::find(UiToggles, command.name, &GameToggle::setting);
  if (toggle == UiToggles.end()) {
    return false;
  }
  changeToggle(*toggle, command, ui, settings, settingsFile);
  return true;
}

bool changeGameToggle(const ui::UiCommand& command, ui::Ui& ui, UserSettings& settings,
                      const std::optional<std::filesystem::path>& settingsFile) {
  const auto toggle = std::ranges::find(Toggles, command.name, &GameToggle::setting);
  if (toggle == Toggles.end()) {
    return false;
  }
  changeToggle(*toggle, command, ui, settings, settingsFile);
  return true;
}

} // namespace dwe::app
