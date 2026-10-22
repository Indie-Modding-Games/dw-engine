#include <dwe/screens/field_hud_view.hpp>
#include <dwe/screens/name_keys.hpp>

#include <cmath>
#include <string>
#include <utility>

namespace dwe::screens {
namespace {

using game::partner::Condition;
using game::partner::Stat;

// The original's menu counts the day of the year and the year from 1; the clock keeps them from 0.
constexpr std::int32_t FirstShownDay = 1;
constexpr std::int32_t FirstShownYear = 1;

constexpr std::array<std::string_view, MeterCount> MeterLabels{"partner_menu.happiness", "partner_menu.discipline"};

// Each need with the condition that is it and its label.
constexpr std::array<std::pair<Condition, std::string_view>, NeedCount> NeedLabels{{
    {Condition::Hungry, "hud.need.hunger"},
    {Condition::Sleepy, "hud.need.sleep"},
    {Condition::NeedsToilet, "hud.need.toilet"},
    {Condition::Tired, "hud.need.tired"},
}};

constexpr std::array<Stat, PoolCount> PoolStats{Stat::Hp, Stat::Mp};

// hud.partner.area publishes the original's numbers, which the response's values are.
static_assert(static_cast<int>(game::partner::AreaResponse::Neutral) == 0);
static_assert(static_cast<int>(game::partner::AreaResponse::Liked) == 1);
static_assert(static_cast<int>(game::partner::AreaResponse::Disliked) == 2);

// The lists by shorter names, which also keep the "Field" prefix away from listEntryName's `field` parameter.
constexpr std::string_view MeterList = FieldHudMeterList;
constexpr std::string_view NeedList = FieldHudNeedList;
constexpr std::string_view PoolList = FieldHudPoolList;

constexpr std::string_view LabelField = "label";
constexpr std::string_view TwoToneField = "two_tone";
constexpr std::string_view ValueField = "value";
constexpr std::string_view MinField = "min";
constexpr std::string_view MaxField = "max";
constexpr std::string_view ActiveField = "active";

FieldMeterView meterOf(const session::CareMeter& meter) {
    return {.value = meter.value, .min = meter.min, .max = meter.max};
}

double number(std::int32_t value) {
    return static_cast<double>(value);
}

constexpr double Percent = 100.0;
// The partner's place is published in tenths of a percent: finer than a pixel on a 1000-pixel window, and the
// published value does not change on every sub-pixel step of the partner's walk.
constexpr double PlaceSteps = 10.0;

double percentOf(double fraction) {
    return std::round(fraction * Percent * PlaceSteps) / PlaceSteps;
}

bool onScreen(const WindowFraction& point) {
    return point.x >= 0.0 && point.x <= 1.0 && point.y >= 0.0 && point.y <= 1.0;
}

} // namespace

FieldHudView fieldHudViewOf(const ruleset::GameTime& time, const session::PartnerView& partner, bool shown) {
    FieldHudView view{.shown = shown,
                      .clock = {.hour = time.hour,
                                .minute = time.minute,
                                .day = time.day + FirstShownDay,
                                .year = time.year + FirstShownYear},
                      .meters = {meterOf(partner.happiness), meterOf(partner.discipline)},
                      .needs = {},
                      .pools = {},
                      .area = partner.area};
    for (std::size_t index = 0; index < NeedLabels.size(); ++index) {
        view.needs.at(index) = partner.conditions.has(NeedLabels.at(index).first);
    }
    view.pools = {FieldPoolView{.current = partner.currentHp, .max = partner.stats[Stat::Hp]},
                  FieldPoolView{.current = partner.currentMp, .max = partner.stats[Stat::Mp]}};
    return view;
}

FieldPartnerView fieldPartnerViewOf(std::optional<WindowFraction> bubble, bool fieldRunning) {
    if (!fieldRunning || !bubble || !onScreen(*bubble)) {
        return {};
    }
    return {.visible = true, .x = percentOf(bubble->x), .y = percentOf(bubble->y)};
}

ScreenSettings settingsOf(const FieldPartnerView& view) {
    return {
        {.name = std::string{FieldPartnerVisibleSetting}, .value = view.visible},
        {.name = std::string{FieldPartnerXSetting}, .value = view.x},
        {.name = std::string{FieldPartnerYSetting}, .value = view.y},
    };
}

ScreenSettings fieldHudLabelSettings() {
    ScreenSettings settings;
    for (std::size_t index = 0; index < MeterLabels.size(); ++index) {
        settings.push_back(
            {.name = listEntryName(MeterList, index, LabelField), .value = std::string{MeterLabels.at(index)}});
        settings.push_back({.name = listEntryName(MeterList, index, TwoToneField), .value = true});
    }
    for (std::size_t index = 0; index < NeedLabels.size(); ++index) {
        settings.push_back(
            {.name = listEntryName(NeedList, index, LabelField), .value = std::string{NeedLabels.at(index).second}});
    }
    for (std::size_t index = 0; index < PoolStats.size(); ++index) {
        settings.push_back({.name = listEntryName(PoolList, index, LabelField),
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
        {.name = std::string{FieldHudPartnerAreaSetting}, .value = number(static_cast<std::int32_t>(view.area))},
    };
    for (std::size_t index = 0; index < view.meters.size(); ++index) {
        const FieldMeterView& meter = view.meters.at(index);
        settings.push_back({.name = listEntryName(MeterList, index, ValueField), .value = number(meter.value)});
        settings.push_back({.name = listEntryName(MeterList, index, MinField), .value = number(meter.min)});
        settings.push_back({.name = listEntryName(MeterList, index, MaxField), .value = number(meter.max)});
    }
    for (std::size_t index = 0; index < view.needs.size(); ++index) {
        settings.push_back({.name = listEntryName(NeedList, index, ActiveField), .value = view.needs.at(index)});
    }
    for (std::size_t index = 0; index < view.pools.size(); ++index) {
        const FieldPoolView& pool = view.pools.at(index);
        settings.push_back({.name = listEntryName(PoolList, index, ValueField), .value = number(pool.current)});
        settings.push_back({.name = listEntryName(PoolList, index, MaxField), .value = number(pool.max)});
    }
    return settings;
}

} // namespace dwe::screens
