#pragma once

#include <dwe/game/partner/effects.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/partner_view.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::screens {

// The settings of the field HUD (ui/hud/document), which redraws its field status on
// FieldHudRevisionSetting. The labels never change and go once (fieldHudLabelSettings); the values
// go on every change (settingsOf).
inline constexpr std::string_view FieldHudRevisionSetting = "hud.revision";
inline constexpr std::string_view FieldHudShownSetting = "hud.shown";
inline constexpr std::string_view FieldHudHourSetting = "hud.clock.hour";
inline constexpr std::string_view FieldHudMinuteSetting = "hud.clock.minute";
inline constexpr std::string_view FieldHudDaySetting = "hud.clock.day";
inline constexpr std::string_view FieldHudYearSetting = "hud.clock.year";
// The game's rules on the clock, so no UI decides them: whether it is night (the original's dial
// shows the moon: clock.c: renderGameClock) and the part of the day (dayPhaseOf).
inline constexpr std::string_view FieldHudNightSetting = "hud.clock.night";
inline constexpr std::string_view FieldHudPhaseSetting = "hud.clock.phase";
inline constexpr std::string_view FieldHudMeterList = "hud.meter";
inline constexpr std::string_view FieldHudNeedList = "hud.need";
inline constexpr std::string_view FieldHudPoolList = "hud.pool";
// How the partner feels about the map, as the original's numbers (map.c:
// MAP_TILE_DATA.partnerAreaResponse): 0 neutral, 1 it likes the area, 2 it dislikes it. It goes
// with the field status, on FieldHudRevisionSetting.
inline constexpr std::string_view FieldHudPartnerAreaSetting = "hud.partner.area";

// The bars of the field HUD, in the order of the hud.meter list: happiness and discipline.
inline constexpr std::size_t MeterCount = 2;
// The pools of the field HUD, in the order of the hud.pool list: HP and MP.
inline constexpr std::size_t PoolCount = 2;

// The needs the field HUD shows, in the order of the hud.need list: hunger, sleep, toilet,
// tiredness, injury and sickness.
inline constexpr std::size_t NeedCount = 6;

// The game time as the player reads it: the hour (0-23) and minute, and the day of the year and the
// year counted from 1, as the original's menu shows them (game_menu.c: renderDateDigits writes DAY
// + 1 and YEAR + 1).
struct FieldClockView {
  std::int32_t hour = 0;
  std::int32_t minute = 0;
  std::int32_t day = 0;
  std::int32_t year = 0;

  friend bool operator==(const FieldClockView&, const FieldClockView&) = default;
};

// A two-tone bar of the field: its value in the ruleset's range. It fills once from the minimum to
// the middle of the range and again, in a second colour, from the middle to the maximum
// (partner_impl.c: renderStatusBars).
struct FieldMeterView {
  std::int32_t value = 0;
  std::int32_t min = 0;
  std::int32_t max = 0;

  friend bool operator==(const FieldMeterView&, const FieldMeterView&) = default;
};

// Whether it is night at an hour: the original's dial swaps the sun for the moon before 6:00 and
// from 17:00 (clock.c: renderGameClock).
[[nodiscard]] bool isNightHour(std::int32_t hour);

// The part of the day at an hour: "night" as isNightHour, "morning" the two hours after the dawn,
// "late" the two before the dusk and "day" between them. The parts flank the dial's own boundaries,
// so they change only where the sun and the moon do or two hours inside them.
[[nodiscard]] std::string_view dayPhaseOf(std::int32_t hour);

// How a bar of the field reads: "low" below the middle of its range, where the original swaps its
// icon (the happiness face turns sad, the discipline icon changes: partner_impl.c:
// renderStatusBars), else "high".
[[nodiscard]] std::string_view meterMoodOf(const FieldMeterView& meter);

// HP or MP: what is left and the maximum.
struct FieldPoolView {
  std::int32_t current = 0;
  std::int32_t max = 0;

  friend bool operator==(const FieldPoolView&, const FieldPoolView&) = default;
};

// What the field HUD shows of the world and the partner, as plain numbers, so the game compares it
// each frame without building any text. `shown` is false while game time stands still (menus,
// battles, no game), when the original slides its clock and status bars off the screen (clock.c:
// renderGameClock, stopGameTime).
struct FieldHudView {
  bool shown = false;
  FieldClockView clock;
  // Happiness (0) and discipline (1), the two bars the original draws on the field.
  std::array<FieldMeterView, MeterCount> meters;
  // Hunger, sleep, toilet, tiredness, injury and sickness: whether the partner has that need now
  // (its condition, which the original shows as a bubble over the partner: partner_impl.c:
  // handleConditionBubble).
  std::array<bool, NeedCount> needs{};
  // How urgent each need the partner has is, among the ones it has: 1 for the one that does its
  // lasting harm first if left alone (game::partner::NeedDeadlines), then 2 and on; 0 for a need it
  // does not have. Needs with no deadline (tiredness, an injury on a sick partner) come after the
  // ones with one, and needs that tie keep the order of the original's bubble (sleep, tiredness,
  // hunger, toilet, injury, sickness: partner_impl.c: handleConditionBubble). For the stacked need
  // bubble, an engine improvement; the original's bubble keeps its own order.
  std::array<std::int32_t, NeedCount> urgency{};
  // HP (0) and MP (1).
  std::array<FieldPoolView, PoolCount> pools;
  // How the partner feels about the map: the original's happiness icon alternates with another face
  // in an area the partner likes or dislikes (partner_impl.c: renderStatusBars).
  game::partner::AreaResponse area = game::partner::AreaResponse::Neutral;

  friend bool operator==(const FieldHudView&, const FieldHudView&) = default;
};

// Where the partner's need bubble settles on the screen, for what a skin draws over the partner
// (the original's need bubbles, bubble.c), with its own revision: it moves with the partner, apart
// from the rest of the field status.
inline constexpr std::string_view FieldPartnerRevisionSetting = "hud.partner.revision";
inline constexpr std::string_view FieldPartnerXSetting = "hud.partner.x";
inline constexpr std::string_view FieldPartnerYSetting = "hud.partner.y";
inline constexpr std::string_view FieldPartnerVisibleSetting = "hud.partner.visible";
// The evolving bubble's frame (bubble.c: CONDITION_ICON_EVOLVING; apps/game: evolving_bubble.hpp),
// 1 to 51, or 0 while the partner shows none.
inline constexpr std::string_view FieldPartnerEvolvingSetting = "hud.partner.evolving";

// A point of the window as fractions of its width and height from the top-left corner, y down.
struct WindowFraction {
  double x = 0.0;
  double y = 0.0;

  friend bool operator==(const WindowFraction&, const WindowFraction&) = default;
};

// The point over the partner where the original's need bubble settles, on the screen: x and y in
// percent of the window from its top-left corner, rounded to tenths so that it changes only when
// the point moves; `visible` is false while the field is stopped or the point is off the screen or
// behind the camera.
struct FieldPartnerView {
  bool visible = false;
  double x = 0.0;
  double y = 0.0;
  // The evolving bubble's frame while the partner starts to evolve, else 0.
  std::int32_t evolvingFrame = 0;

  friend bool operator==(const FieldPartnerView&, const FieldPartnerView&) = default;
};

// The partner's view from where its bubble's point projects (nothing when it does not), whether
// the field runs and the evolving bubble's frame (0 for none). The evolving bubble shows while the
// field waits for the evolution (partner.c: partnerTickEvolving stops the game time), so the point
// is visible while it does too.
[[nodiscard]] FieldPartnerView fieldPartnerViewOf(std::optional<WindowFraction> bubble,
                                                  bool fieldRunning,
                                                  std::int32_t evolvingFrame = 0);

[[nodiscard]] ScreenSettings settingsOf(const FieldPartnerView& view);

// The field HUD's view of the game time and the partner; `shown` says whether the field's game time
// runs.
[[nodiscard]] FieldHudView fieldHudViewOf(const ruleset::GameTime& time,
                                          const session::PartnerView& partner, bool shown);

// The labels of the field HUD's bars, needs and pools, as text keys, and which bars are two-tone.
// They never change.
[[nodiscard]] ScreenSettings fieldHudLabelSettings();

[[nodiscard]] ScreenSettings settingsOf(const FieldHudView& view);

} // namespace dwe::screens
