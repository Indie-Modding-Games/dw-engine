#pragma once

#include <dwe/core/error.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::app {

// What the player chose in the menus and the game keeps between runs, in settings.yaml in the
// user's data folder.
struct UserSettings {
  // The language of the texts ("en", "es"); the system's when not chosen yet.
  std::optional<std::string> language;
  // The font of "original look" text (text_font: modern, original); the packs' default when not
  // chosen yet.
  std::optional<std::string> textFont;
  // The skin of the UI (ui_skin: the id of a skin the packs offer); the packs' default when not
  // chosen yet.
  std::optional<std::string> uiSkin;
  // Whether the HUD shows the minimap (minimap: auto, on, off); auto when not chosen yet.
  std::optional<std::string> minimap;
  // The minimap's size (minimap_size: small, medium, large; display_settings.hpp); medium when not
  // chosen yet.
  std::optional<std::string> minimapSize = std::nullopt;
  // The display rows of the options page (display_settings.hpp): the window mode (fullscreen:
  // true, false; a window when not chosen yet), vsync (vsync: true, false; on when not chosen yet)
  // and the UI's scale (ui_scale: a factor, 1 the default size, kept within ui::Ui's range; 1 when
  // not chosen yet). The command line's --fullscreen and --no-vsync win for their run.
  std::optional<bool> fullscreen = std::nullopt;
  std::optional<bool> vsync = std::nullopt;
  std::optional<float> uiScale = std::nullopt;
  // Free exploration (free_exploration: true, false): the scripts' map changes go anywhere they
  // can, whatever the story; off when not chosen yet.
  std::optional<bool> freeExploration = std::nullopt;
  // Smooth motion (smooth_motion: true, false): the characters and the camera are drawn gliding
  // between the original's 20 Hz frames; on when not chosen yet.
  std::optional<bool> smoothMotion = std::nullopt;
  // The save indicator (save_notice: true, false): a small figure and "Saving" in a corner while
  // the game saves; on when not chosen yet.
  std::optional<bool> saveNotice = std::nullopt;
  // The autosave (autosave: true, false): the game saves by itself to the playthrough's slot when
  // the ruleset says; on when not chosen yet. Off, only the player's own saves write.
  std::optional<bool> autosave = std::nullopt;
  // The time in digits (digital_time: true, false): a small box with the game time as HH:MM under
  // the field HUD's dial clock; on when not chosen yet.
  std::optional<bool> digitalTime = std::nullopt;
  // Bulk trades (bulk_trades: true, false): the item trader and the card shop's packs ask how many
  // at once; on when not chosen yet.
  std::optional<bool> bulkTrades = std::nullopt;
  // The evolution requirements on the chart (evolution_requirements: true, false); on when not
  // chosen yet.
  std::optional<bool> evolutionRequirements = std::nullopt;
  // Translated signs (translated_signs: true, false): the signs painted in the maps are written in
  // the player's language; on when not chosen yet.
  std::optional<bool> translatedSigns = std::nullopt;
  // 3D characters (model_characters: true, false): the characters some maps draw as sprites are
  // drawn as their species' models; on when not chosen yet.
  std::optional<bool> modelCharacters = std::nullopt;
  // The status page's extras (status_extras: true, false): the partner's tiredness bar and its care
  // mistakes, which the original keeps hidden; on when not chosen yet.
  std::optional<bool> statusExtras = std::nullopt;
  // The canonical stages (canonical_stages: true, false): the chart names the stages as the series
  // does (Digi-Egg to Ultra) and draws a Mega box; off when not chosen yet.
  std::optional<bool> canonicalStages = std::nullopt;
  // The rare items (rare_items: true, false): the field's random item spots may lay a rare
  // evolution item of the ruleset's rare_items section; off when not chosen yet.
  std::optional<bool> rareItems = std::nullopt;
  // What walks the player in the field (walk_control: keys, mouse; walk_control.hpp): the keyboard
  // and the pad, or the mouse, never both; the keys when not chosen yet.
  std::optional<std::string> walkControl = std::nullopt;
  // Long names (long_names: true, false): the naming box takes names of up to LongNameLength
  // letters (naming_box.hpp) instead of the ruleset's six; on when not chosen yet.
  std::optional<bool> longNames = std::nullopt;
  // The shop job's progress bar (shop_job_bar: true, false; shop_job_bar.hpp): the canyon shop's
  // tally against the join while its job runs; on when not chosen yet.
  std::optional<bool> shopJobBar = std::nullopt;
  // Space after names (name_spacing: true, false; story_text_rows.hpp): a name a text inserts gets
  // a space when the original's text runs it into the next word; on when not chosen yet.
  std::optional<bool> nameSpacing = std::nullopt;
  // Continuous music (continuous_music: true, false; session::SessionMusic::setContinuousMusic): a
  // map whose song is the one playing lets it go on instead of playing it again from its start; on
  // when not chosen yet.
  std::optional<bool> continuousMusic = std::nullopt;
  // The need bubble with several needs at once (need_bubble_stack: true, false; ui/hud): up to
  // five of the partner's needs side by side in one balloon, the one that does harm soonest first,
  // instead of the original's two taking turns; on when not chosen yet.
  std::optional<bool> needBubbleStack = std::nullopt;
  // Whether M opens the island map (world_map: true, false; world_map.hpp); on when not chosen.
  std::optional<bool> worldMap = std::nullopt;
  // The chibi style (chibi_style: off, external; an old all reads as external; chibi_style.hpp):
  // whether models from elsewhere that are not chibi are drawn with the original's proportions.
  std::optional<std::string> chibiStyle = std::nullopt;
  // The enemies on the minimap (minimap_enemies: true, false; minimap_enemies.hpp): a dot for each
  // NPC on the map that can start a battle; on when not chosen yet.
  std::optional<bool> minimapEnemies = std::nullopt;
  // The items on the minimap (minimap_items: true, false; minimap_items.hpp): a mark for each item
  // lying on the map's ground; on when not chosen yet.
  std::optional<bool> minimapItems = std::nullopt;
  // The characters' shadows (shadows: off, round, sun; graphics_options.hpp): none, the original's
  // round ones, or those with a soft one cast by the sun; sun when not chosen.
  std::optional<std::string> shadows = std::nullopt;
  // Enhanced lighting (enhanced_lighting: true, false): the time of day's tint and the maps' point
  // lights over the field; on when not chosen.
  std::optional<bool> enhancedLighting = std::nullopt;
  // Vivid colours (vivid_colors: true, false): vibrance, a gentle contrast and a soft bloom over
  // the field; on when not chosen.
  std::optional<bool> vividColors = std::nullopt;
  // HD scenery's quality (scenery_quality: smooth, upscaled), antialiasing (antialiasing: true,
  // false) and the models' textures (texture_filter: nearest, smooth, upscaled); the best when not
  // chosen (graphics_options.hpp).
  std::optional<std::string> sceneryQuality = std::nullopt;
  std::optional<bool> antialiasing = std::nullopt;
  std::optional<std::string> textureFilter = std::nullopt;
  // Whether the field shows the weather (weather: true, false; game::weather, an engine
  // improvement); on when not chosen.
  std::optional<bool> weather = std::nullopt;
  // Whether the field shows the seasons (seasons: true, false; game::weather, an engine
  // improvement); on when not chosen.
  std::optional<bool> seasons = std::nullopt;
  // HD scenery (hd_scenery: true, false; hd_scenery_setting.hpp): the field's background and depth
  // layers drawn smooth at the window's resolution instead of the original's pixels; on when not
  // chosen yet.
  std::optional<bool> hdScenery = std::nullopt;
  // The music's and the effects' volumes (music_volume, effects_volume: a step from 0, silent, to
  // VolumeSteps, the original's level; volume_setting.hpp), the full level when not chosen yet.
  std::optional<int> musicVolume = std::nullopt;
  std::optional<int> effectsVolume = std::nullopt;
  // The sound chip's reverb (reverb: true, false; volume_setting.hpp): on, as the original plays
  // every tone with its reverb bit through it; on when not chosen yet.
  std::optional<bool> reverb = std::nullopt;
  // Quick battles (battle_speed: normal, key, fast; battle_speed.hpp): fights at twice the speed,
  // always or switched with a key; normal, the original's, when not chosen yet.
  std::optional<std::string> battleSpeed = std::nullopt;
  // Enemy life bars (enemy_hp_bars: true, false; BattleScreen::setEnemyHpBars): a bar over every
  // enemy still standing in every fight, cups too, instead of the original's (a while after a wild
  // enemy is hit, never in a cup); off when not chosen yet.
  std::optional<bool> enemyHpBars = std::nullopt;
  // Batch training (gym_sessions: 1 to 5; gym_batch.hpp): the sessions a course the story starts
  // at a gym runs in a row; 1, the original's, when not chosen yet.
  std::optional<int> gymSessions = std::nullopt;
  // Quick battle start (quick_battle_start: true, false; BattleScreen::setQuickStart): a field
  // fight starts the moment it triggers, without the original's opening; off when not chosen yet.
  std::optional<bool> quickBattleStart = std::nullopt;
  // The difficulty (difficulty: normal, easy; rule_options.hpp): a level of the ruleset's
  // difficulty section; normal, the original's, when not chosen yet.
  std::optional<std::string> difficulty = std::nullopt;
  // Skip the evolution (skip_evolution: true, false; EvolutionScene::skip): confirm or cancel ends
  // the evolution's scene at once; off when not chosen yet.
  std::optional<bool> skipEvolution = std::nullopt;
  // A cancel closes a choice (choice_cancel_closes: true, false; story.cancel_closes_choice): a
  // cancel in a script's choice without its end marker ends the script instead of running the
  // first option; off when not chosen yet.
  std::optional<bool> choiceCancelCloses = std::nullopt;
  // Fair sickness chance (fair_sickness_chance: true, false; rule_options.hpp): the care quirk
  // sickness_chance_wraps off, so a negative chance is none; off when not chosen yet.
  std::optional<bool> fairSicknessChance = std::nullopt;
  // Fair evolution average (fair_evolution_average: true, false; rule_options.hpp): the evolution
  // quirk selection_average_carries_over off; off when not chosen yet.
  std::optional<bool> fairEvolutionAverage = std::nullopt;
  // Fair move forgetting (fair_move_forgetting: true, false; rule_options.hpp): the partner moves
  // quirk forget_mask_16_bits off, so forgetting a move clears only that one; off when not chosen.
  std::optional<bool> fairMoveForgetting = std::nullopt;
  // Fair card stock (fair_card_stock: true, false; rule_options.hpp): the card shop quirk
  // stock_repeats off, so a day's stock shows each card once; off when not chosen yet.
  std::optional<bool> fairCardStock = std::nullopt;
  // Choose the egg after losing all lives (choose_egg_after_lives_lost: true, false;
  // rule_options.hpp): life.rebirth.random_egg_without_lives off, so a partner reborn with no
  // lives left comes from the egg the player takes in the egg box; off when not chosen yet.
  std::optional<bool> chooseEggAfterLivesLost = std::nullopt;
  // Generous talk range (generous_talk: true, false; game::field::Field::setGenerousTalk): a
  // confirm reaches characters and signs a little further; off when not chosen yet.
  std::optional<bool> generousTalk = std::nullopt;
  // Shop extras (shop_extras: true, false; the shop document reads it): the focused item's
  // description under a shop's list, the bag's count of each item and the money a trade leaves; off
  // when not chosen yet.
  std::optional<bool> shopExtras = std::nullopt;
  // Art fixes (art_fixes: true, false): the original skin draws the pictures the original reads
  // from the wrong part of its textures (the partner card's happiness label) where they are; off,
  // as the original, when not chosen yet.
  std::optional<bool> artFixes = std::nullopt;
  // Developer tools (dev_tools: true, false; dev_tools.hpp): the debug console, the developer menu
  // and the map overlay; when not chosen yet, on in a development build and off in a package.
  std::optional<bool> devTools = std::nullopt;
};

// Reads the settings. Keys it does not know are left out, so an older game reads a newer file.
core::Result<UserSettings> parseUserSettings(std::string_view yaml);

// Writes the settings as YAML.
core::Result<std::string> formatUserSettings(const UserSettings& settings);

// The file the settings are kept in, or nothing (logged) when the system gives no data folder.
std::optional<std::filesystem::path> userSettingsFile();

// Reads the settings from a file: the defaults when it does not exist, and (logged) when it cannot
// be read.
UserSettings loadUserSettings(const std::filesystem::path& file);

// Writes the settings to a file, logging a failure.
void saveUserSettings(const std::filesystem::path& file, const UserSettings& settings);

} // namespace dwe::app
