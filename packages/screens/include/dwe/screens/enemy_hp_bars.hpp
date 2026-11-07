#pragma once

#include <dwe/screens/field_hud_view.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dwe::screens {

// The marks over the two fighters of a cup match between two of the same species (std_main.c:
// STD_initializePlayerMarker, STD_renderPlayerMarker): MarkersSetting packs three values each, the
// fighter (0 the partner's «1P», 1 the rival's «COM») and the anchor's x and y in percent of the
// window: where the fighter's model node 1 shows, moved right by half the species' radius at its
// depth. The mark's top-left corner is 32 of the original's pixels over the anchor. They go with
// the HP bars' revision.
inline constexpr std::string_view MarkersSetting = "battle.markers";

// The settings of the enemies' HP bars on the battle document (ui/battle/document), which redraws
// them on HpBarsRevisionSetting. HpBarsSetting packs the bars drawn on the last battle frame, nine
// values each: the enemy (1 to 3), the anchor's x and y in percent of the window, the bar's
// top-left corner from it in the original's pixels (x, y), the fill's width in pixels, the fill's
// tint (red and green, 128 for as it is, 255 for twice as bright; blue is always 128) and the HP
// shown.
inline constexpr std::string_view HpBarsRevisionSetting = "battle.hp_bars.revision";
inline constexpr std::string_view HpBarsSetting = "battle.hp_bars";
// Whether the bars are the option's (enemyHpBarShown: always), which the modern look draws too.
inline constexpr std::string_view EnemyHpBarsAlwaysSetting = "battle.hp_bars.always";

// One enemy's HP bar as a battle frame draws it (battle_main.c: BTL_renderEnemyHPBars). The anchor
// is where the enemy's model node 1 shows on the window; the leader line runs from the bar's
// bottom-left corner (x, y + 6) to it.
struct EnemyHpBar {
  std::size_t enemy = 0;
  WindowFraction anchor;
  std::int32_t x = 0;
  std::int32_t y = 0;
  // The fill sprite's width, 0 to 50 pixels, and its red and green tint (BTL_initializeEnemyHPBar-
  // Sprites: 50 wide, tinted 0x80, 0xff, 0x80).
  std::int32_t fill = 0;
  std::int32_t red = 0;
  std::int32_t green = 0;
  std::int32_t hp = 0;

  friend bool operator==(const EnemyHpBar&, const EnemyHpBar&) = default;
};

// What a bar needs from its enemy: the model node the bar points at (getEntityScreenPos(entity, 1))
// as a fraction of the window, the window's width over its height, and the HP the battle shows now.
struct EnemyOnScreen {
  std::size_t enemy = 0;
  WindowFraction node;
  double aspect = 0.0;
  std::int32_t hp = 0;
  std::int32_t maxHp = 0;
};

// What decides whether an enemy's bar shows this frame.
struct EnemyHpBarCase {
  // Enemy life bars, an engine improvement (apps/game: EnemyHpBarsSetting).
  bool always = false;
  // A cup match (Battle's match clock): std_main.c: STD_initializeCombat adds no bars
  // (BTL_addEnemyHPBars is only BTL_initializeCombat's).
  bool match = false;
  // The original's rule in a wild fight: a while after the enemy is hit (Battle::hpBarShown).
  bool recentlyHit = false;
  // The enemy still stands: HP left and not fallen.
  bool standing = false;
};

// Whether the enemy's bar shows: with the option, every enemy still standing, cups too; without it
// the original's, a wild enemy's bar a while after it is hit and none in a cup.
[[nodiscard]] bool enemyHpBarShown(const EnemyHpBarCase& enemy);

// The bar of an enemy whose bar shows (enemyHpBarShown). The original's screen is 240 lines
// tall, its middle (0, 0): the bar's top-left corner is 20 pixels right and 26 up from the node,
// held at x 85 once the node is 66 or more right of the middle and at y -100 once it is more than
// 74 over it. The fill is HP * 50 / max HP pixels wide and tinted by its width: green when full,
// yellower as it shrinks to 15 pixels, then orange, and red under 5.
[[nodiscard]] EnemyHpBar enemyHpBarOf(const EnemyOnScreen& enemy);

// The bars as the settings the battle document reads.
[[nodiscard]] ScreenSettings settingsOf(const std::vector<EnemyHpBar>& bars);

// One fighter's mark in a match between two of the same species.
struct PlayerMarker {
  std::size_t fighter = 0;
  WindowFraction anchor;
};

// std_main.c: STD_initializeCombat adds the marks only when both fighters are of one species.
[[nodiscard]] bool marksShown(bool match, std::string_view partnerSpecies,
                              std::string_view rivalSpecies);

// The marks as the settings the battle document reads.
[[nodiscard]] ScreenSettings settingsOf(const std::vector<PlayerMarker>& markers);

} // namespace dwe::screens
