#include <dwe/screens/field_hud_view.hpp>
#include <dwe/screens/name_keys.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace dwe::screens {
namespace {

using game::partner::Condition;
using game::partner::Stat;

// The original's menu counts the day of the year and the year from 1; the clock keeps them from 0.
constexpr std::int32_t FirstShownDay = 1;
constexpr std::int32_t FirstShownYear = 1;

constexpr std::array<std::string_view, MeterCount> MeterLabels{"partner_menu.happiness",
                                                               "partner_menu.discipline"};

// Each need with the condition that is it and its label.
constexpr std::array<std::pair<Condition, std::string_view>, NeedCount> NeedLabels{{
    {Condition::Hungry, "hud.need.hunger"},
    {Condition::Sleepy, "hud.need.sleep"},
    {Condition::NeedsToilet, "hud.need.toilet"},
    {Condition::Tired, "hud.need.tired"},
    {Condition::Injured, "hud.need.injured"},
    {Condition::Sick, "hud.need.sick"},
}};

// The needs by the original bubble's order, highest first (partner_impl.c: handleConditionBubble
// checks them from the lowest and the last match wins), as places in NeedLabels: what breaks a tie
// of urgency.
constexpr std::array<std::size_t, NeedCount> BubbleOrder{1, 3, 0, 2, 4, 5};

constexpr std::array<Stat, PoolCount> PoolStats{Stat::Hp, Stat::Mp};

// hud.partner.area publishes the original's numbers, which the response's values are.
static_assert(static_cast<int>(game::partner::AreaResponse::Neutral) == 0);
static_assert(static_cast<int>(game::partner::AreaResponse::Liked) == 1);
static_assert(static_cast<int>(game::partner::AreaResponse::Disliked) == 2);

// The lists by shorter names, which also keep the "Field" prefix away from listEntryName's `field`
// parameter.
constexpr std::string_view MeterList = FieldHudMeterList;
constexpr std::string_view NeedList = FieldHudNeedList;
constexpr std::string_view PoolList = FieldHudPoolList;

constexpr std::string_view LabelField = "label";
constexpr std::string_view TwoToneField = "two_tone";
constexpr std::string_view ValueField = "value";
constexpr std::string_view MinField = "min";
constexpr std::string_view MaxField = "max";
constexpr std::string_view ActiveField = "active";
constexpr std::string_view UrgencyField = "urgency";
constexpr std::string_view MoodField = "mood";

// The dial's night (clock.c: renderGameClock) and the parts of the day flanking its boundaries.
constexpr std::int32_t NightEndsHour = 6;
constexpr std::int32_t NightStartsHour = 17;
constexpr std::int32_t MorningEndsHour = 8;
constexpr std::int32_t LateStartsHour = 15;

FieldMeterView meterOf(const session::CareMeter& meter) {
  return {.value = meter.value, .min = meter.min, .max = meter.max};
}

double number(std::int32_t value) {
  return static_cast<double>(value);
}

constexpr double Percent = 100.0;
// The partner's place is published in tenths of a percent: finer than a pixel on a 1000-pixel
// window, and the published value does not change on every sub-pixel step of the partner's walk.
constexpr double PlaceSteps = 10.0;

double percentOf(double fraction) {
  return std::round(fraction * Percent * PlaceSteps) / PlaceSteps;
}

// Ranks the needs the partner has by their deadlines, the soonest first; a need with none after
// every need with one; ties in the original bubble's order.
std::array<std::int32_t, NeedCount> urgencyOf(const std::array<bool, NeedCount>& needs,
                                              const game::partner::NeedDeadlines& deadlines) {
  constexpr std::int64_t NoDeadline = std::numeric_limits<std::int64_t>::max();
  std::vector<std::size_t> ranked;
  for (const std::size_t index : BubbleOrder) {
    if (needs.at(index)) {
      ranked.push_back(index);
    }
  }
  const auto deadline = [&](std::size_t index) {
    return deadlines.framesToHarm(NeedLabels.at(index).first).value_or(NoDeadline);
  };
  std::ranges::stable_sort(ranked, {}, deadline);
  std::array<std::int32_t, NeedCount> urgency{};
  for (std::size_t place = 0; place < ranked.size(); ++place) {
    urgency.at(ranked.at(place)) = static_cast<std::int32_t>(place + 1);
  }
  return urgency;
}

bool onScreen(const WindowFraction& point) {
  return point.x >= 0.0 && point.x <= 1.0 && point.y >= 0.0 && point.y <= 1.0;
}

} // namespace

FieldHudView fieldHudViewOf(const ruleset::GameTime& time, const session::PartnerView& partner,
                            bool shown) {
  FieldHudView view{.shown = shown,
                    .clock = {.hour = time.hour,
                              .minute = time.minute,
                              .day = time.day + FirstShownDay,
                              .year = time.year + FirstShownYear},
                    .meters = {meterOf(partner.happiness), meterOf(partner.discipline)},
                    .needs = {},
                    .urgency = {},
                    .pools = {},
                    .area = partner.area};
  for (std::size_t index = 0; index < NeedLabels.size(); ++index) {
    view.needs.at(index) = partner.conditions.has(NeedLabels.at(index).first);
  }
  view.urgency = urgencyOf(view.needs, partner.needDeadlines);
  view.pools = {FieldPoolView{.current = partner.currentHp, .max = partner.stats[Stat::Hp]},
                FieldPoolView{.current = partner.currentMp, .max = partner.stats[Stat::Mp]}};
  return view;
}

FieldPartnerView fieldPartnerViewOf(std::optional<WindowFraction> bubble, bool fieldRunning,
                                    std::int32_t evolvingFrame) {
  const bool evolving = evolvingFrame > 0;
  if ((!fieldRunning && !evolving) || !bubble || !onScreen(*bubble)) {
    return {};
  }
  return {.visible = true,
          .x = percentOf(bubble->x),
          .y = percentOf(bubble->y),
          .evolvingFrame = evolving ? evolvingFrame : 0};
}

ScreenSettings settingsOf(const FieldPartnerView& view) {
  return {
      {.name = std::string{FieldPartnerVisibleSetting}, .value = view.visible},
      {.name = std::string{FieldPartnerXSetting}, .value = view.x},
      {.name = std::string{FieldPartnerYSetting}, .value = view.y},
      {.name = std::string{FieldPartnerEvolvingSetting},
       .value = static_cast<double>(view.evolvingFrame)},
  };
}

bool isNightHour(std::int32_t hour) {
  return hour < NightEndsHour || hour >= NightStartsHour;
}

std::string_view dayPhaseOf(std::int32_t hour) {
  if (isNightHour(hour)) {
    return "night";
  }
  if (hour < MorningEndsHour) {
    return "morning";
  }
  if (hour >= LateStartsHour) {
    return "late";
  }
  return "day";
}

std::string_view meterMoodOf(const FieldMeterView& meter) {
  // value < (min + max) / 2, in whole numbers.
  return 2 * static_cast<std::int64_t>(meter.value) <
                 static_cast<std::int64_t>(meter.min) + static_cast<std::int64_t>(meter.max)
             ? "low"
             : "high";
}

ScreenSettings fieldHudLabelSettings() {
  ScreenSettings settings;
  for (std::size_t index = 0; index < MeterLabels.size(); ++index) {
    settings.push_back({.name = listEntryName(MeterList, index, LabelField),
                        .value = std::string{MeterLabels.at(index)}});
    settings.push_back({.name = listEntryName(MeterList, index, TwoToneField), .value = true});
  }
  for (std::size_t index = 0; index < NeedLabels.size(); ++index) {
    settings.push_back({.name = listEntryName(NeedList, index, LabelField),
                        .value = std::string{NeedLabels.at(index).second}});
  }
  for (std::size_t index = 0; index < PoolStats.size(); ++index) {
    settings.push_back(
        {.name = listEntryName(PoolList, index, LabelField),
         .value = mainLabelKey(StatLabelsBlock, static_cast<std::size_t>(PoolStats.at(index)))});
  }
  return settings;
}

ScreenSettings settingsOf(const FieldHudView& view) {
  ScreenSettings settings{
      {.name = std::string{FieldHudShownSetting}, .value = view.shown},
      {.name = std::string{FieldHudHourSetting}, .value = number(view.clock.hour)},
      {.name = std::string{FieldHudMinuteSetting}, .value = number(view.clock.minute)},
      {.name = std::string{FieldHudDaySetting}, .value = number(view.clock.day)},
      {.name = std::string{FieldHudYearSetting}, .value = number(view.clock.year)},
      {.name = std::string{FieldHudNightSetting}, .value = isNightHour(view.clock.hour)},
      {.name = std::string{FieldHudPhaseSetting},
       .value = std::string{dayPhaseOf(view.clock.hour)}},
      {.name = std::string{FieldHudPartnerAreaSetting},
       .value = number(static_cast<std::int32_t>(view.area))},
  };
  for (std::size_t index = 0; index < view.meters.size(); ++index) {
    const FieldMeterView& meter = view.meters.at(index);
    settings.push_back(
        {.name = listEntryName(MeterList, index, ValueField), .value = number(meter.value)});
    settings.push_back(
        {.name = listEntryName(MeterList, index, MinField), .value = number(meter.min)});
    settings.push_back(
        {.name = listEntryName(MeterList, index, MaxField), .value = number(meter.max)});
    settings.push_back({.name = listEntryName(MeterList, index, MoodField),
                        .value = std::string{meterMoodOf(meter)}});
  }
  for (std::size_t index = 0; index < view.needs.size(); ++index) {
    settings.push_back(
        {.name = listEntryName(NeedList, index, ActiveField), .value = view.needs.at(index)});
    settings.push_back({.name = listEntryName(NeedList, index, UrgencyField),
                        .value = number(view.urgency.at(index))});
  }
  for (std::size_t index = 0; index < view.pools.size(); ++index) {
    const FieldPoolView& pool = view.pools.at(index);
    settings.push_back(
        {.name = listEntryName(PoolList, index, ValueField), .value = number(pool.current)});
    settings.push_back(
        {.name = listEntryName(PoolList, index, MaxField), .value = number(pool.max)});
  }
  return settings;
}

} // namespace dwe::screens
