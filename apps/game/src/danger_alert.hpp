#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/screens/field_hud_view.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/partner_view.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string_view>

namespace dwe::app {

// The danger alert (an engine improvement; it changes no rule): a mark over each wild monster on
// the field that would crush the partner. A UI setting and the command of the same name, saved
// with the user's settings (danger_alert: true, false), on when not chosen yet.
inline constexpr std::string_view DangerAlertSetting = "danger_alert";
// The marks the HUD draws: hud.danger.<n>.{visible,x,y}, one per NPC slot of the map, and the
// revision that goes up when any of them changes.
inline constexpr std::string_view DangerList = "hud.danger";
inline constexpr std::string_view DangerRevisionSetting = "hud.danger.revision";
// A map holds at most eight NPC records.
inline constexpr std::size_t DangerMarkCount = 8;

// What a fighter enters a battle with: its HP and its two battle stats.
struct FightStrength {
  std::int32_t hp = 0;
  std::int32_t offense = 0;
  std::int32_t defense = 0;
};

// Whether the wild one would crush the partner: its HP × (offense + defense) is at least 2.5 times
// the partner's.
[[nodiscard]] bool crushes(const FightStrength& wild, const FightStrength& partner);

// What a map NPC fights with (game::battle::enemyFromMap: its record's stats, the HP it has left
// when the record keeps some).
[[nodiscard]] FightStrength strengthOf(const assets::MapNpc& npc);
// What the partner fights with (session::BattleAdapter::visitorOf: its HP now and its stats).
[[nodiscard]] FightStrength strengthOf(const session::PartnerView& partner);

// One mark: where it hangs in the window, in percent from the top-left corner rounded to tenths.
struct DangerMark {
  bool visible = false;
  double x = 0.0;
  double y = 0.0;

  friend bool operator==(const DangerMark&, const DangerMark&) = default;
};

using DangerMarks = std::array<DangerMark, DangerMarkCount>;

// Where a map NPC's mark hangs in the window, by its slot; nothing when it is not on the map or off
// the camera.
using DangerAnchor =
    std::function<std::optional<screens::WindowFraction>(std::size_t slot, const assets::MapNpc&)>;

// The marks: none while `shown` is false (the option off, or the field stopped); otherwise one on
// each NPC slot whose section can start a wild battle (wildFightingSections), that stands on the
// field and that crushes the partner.
[[nodiscard]] DangerMarks dangerMarksOf(bool shown, std::span<const assets::MapNpc> records,
                                        const std::set<std::int32_t>& wild,
                                        const FightStrength& partner, const DangerAnchor& anchor);

[[nodiscard]] screens::ScreenSettings settingsOf(const DangerMarks& marks);

} // namespace dwe::app
