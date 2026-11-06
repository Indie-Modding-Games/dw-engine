#include <dwe/screens/enemy_hp_bars.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <string>
#include <utility>

namespace dwe::screens {
namespace {

// The original's screen is 240 lines tall; getEntityScreenPos counts from its middle.
constexpr double ScreenLines = 240.0;
constexpr double Middle = 0.5;

// BTL_renderEnemyHPBars: the bar's corner from the node, and where it stops.
constexpr std::int32_t RightOfNode = 20;
constexpr std::int32_t OverNode = 26;
constexpr std::int32_t RightmostNode = 66;
constexpr std::int32_t RightmostBar = 85;
constexpr std::int32_t HighestNode = -74;
constexpr std::int32_t HighestBar = -100;

// The fill sprite: 50 pixels for full HP; its tint, 128 a channel as it is (GsSPRITE r, g, b).
constexpr std::int32_t FillPixels = 50;
constexpr std::int32_t Plain = 0x80;
constexpr std::int32_t Bright = 0xff;
constexpr std::int32_t Yellow = 15;
constexpr std::int32_t Orange = 5;
constexpr std::int32_t TintSteps = 127;
constexpr std::int32_t GreenSpan = 35;
constexpr std::int32_t YellowSpan = 10;

constexpr double Percent = 100.0;
constexpr double Tenths = 10.0;

double rounded(double value) {
  return std::round(value * Tenths) / Tenths;
}

// The fill's red and green for its width (BTL_renderEnemyHPBars).
std::pair<std::int32_t, std::int32_t> tintOf(std::int32_t fill) {
  if (fill >= Yellow) {
    return {((FillPixels - fill) * TintSteps / GreenSpan) + Plain, Bright};
  }
  if (fill >= Orange) {
    return {Bright, Bright - ((Yellow - fill) * TintSteps / YellowSpan)};
  }
  return {Bright, Plain};
}

} // namespace

EnemyHpBar enemyHpBarOf(const EnemyOnScreen& enemy) {
  // getEntityScreenPos: the node in the original's pixels from the screen's middle.
  const auto nodeX =
      static_cast<std::int32_t>(std::lround((enemy.node.x - Middle) * ScreenLines * enemy.aspect));
  const auto nodeY = static_cast<std::int32_t>(std::lround((enemy.node.y - Middle) * ScreenLines));
  const std::int32_t x = nodeX >= RightmostNode ? RightmostBar : nodeX + RightOfNode;
  const std::int32_t y = nodeY < HighestNode ? HighestBar : nodeY - OverNode;
  const std::int32_t hp = std::max(enemy.hp, 0);
  const std::int32_t fill = enemy.maxHp > 0 ? hp * FillPixels / enemy.maxHp : 0;
  const auto [red, green] = tintOf(fill);
  return {.enemy = enemy.enemy,
          .anchor = enemy.node,
          .x = x - nodeX,
          .y = y - nodeY,
          .fill = fill,
          .red = red,
          .green = green,
          .hp = hp};
}

ScreenSettings settingsOf(const std::vector<EnemyHpBar>& bars) {
  std::string text;
  for (const EnemyHpBar& bar : bars) {
    std::format_to(std::back_inserter(text), "{} {:.1f} {:.1f} {} {} {} {} {} {} ", bar.enemy,
                   rounded(bar.anchor.x * Percent), rounded(bar.anchor.y * Percent), bar.x, bar.y,
                   bar.fill, bar.red, bar.green, bar.hp);
  }
  return {{.name = std::string{HpBarsSetting}, .value = std::move(text)}};
}

bool marksShown(bool match, std::string_view partnerSpecies, std::string_view rivalSpecies) {
  return match && !partnerSpecies.empty() && partnerSpecies == rivalSpecies;
}

ScreenSettings settingsOf(const std::vector<PlayerMarker>& markers) {
  std::string text;
  for (const PlayerMarker& marker : markers) {
    std::format_to(std::back_inserter(text), "{} {:.1f} {:.1f} ", marker.fighter,
                   rounded(marker.anchor.x * Percent), rounded(marker.anchor.y * Percent));
  }
  return {{.name = std::string{MarkersSetting}, .value = std::move(text)}};
}

} // namespace dwe::screens
