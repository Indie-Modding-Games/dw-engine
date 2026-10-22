#include <dwe/screens/field_hud_view.hpp>
#include <dwe/screens/name_keys.hpp>

#include <string>
#include <utility>

namespace dwe::screens {
namespace {

using game::partner::Condition;
using game::partner::Stat;

// The original's menu counts the day of the year and the year from 1; the clock keeps them from 0.
constexpr std::int32_t FirstShownDay = 1;
constexpr std::int32_t FirstShownYear = 1;

constexpr std::array<std::string_view, 2> MeterLabels{"partner_menu.happiness", "partner_menu.discipline"};

// Each need with the condition that is it and its label.
constexpr std::array<std::pair<Condition, std::string_view>, NeedCount> NeedLabels{{
    {Condition::Hungry, "hud.need.hunger"},
    {Condition::Sleepy, "hud.need.sleep"},
    {Condition::NeedsToilet, "hud.need.toilet"},
}};

constexpr std::array<Stat, 2> PoolStats{Stat::Hp, Stat::Mp};

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

} // namespace

FieldHudView fieldHudViewOf(const ruleset::GameTime& time, const session::PartnerView& partner, bool shown) {
    FieldHudView view{.shown = shown,
                      .clock = {.hour = time.hour,
                                .minute = time.minute,
                                .day = time.day + FirstShownDay,
                                .year = time.year + FirstShownYear},
                      .meters = {meterOf(partner.happiness), meterOf(partner.discipline)},
                      .needs = {},
                      .pools = {}};
    for (std::size_t index = 0; index < NeedLabels.size(); ++index) {
        view.needs.at(index) = partner.conditions.has(NeedLabels.at(index).first);
    }
    view.pools = {FieldPoolView{.current = partner.currentHp, .max = partner.stats[Stat::Hp]},
                  FieldPoolView{.current = partner.currentMp, .max = partner.stats[Stat::Mp]}};
    return view;
}

ScreenSettings fieldHudLabelSettings() {
    ScreenSettings settings;
    for (std::size_t index = 0; index < MeterLabels.size(); ++index) {
        settings.push_back(
            {.name = listEntryName(FieldHudMeterList, index, LabelField), .value = std::string{MeterLabels.at(index)}});
        settings.push_back({.name = listEntryName(FieldHudMeterList, index, TwoToneField), .value = true});
    }
    for (std::size_t index = 0; index < NeedLabels.size(); ++index) {
        settings.push_back({.name = listEntryName(FieldHudNeedList, index, LabelField),
                            .value = std::string{NeedLabels.at(index).second}});
    }
    for (std::size_t index = 0; index < PoolStats.size(); ++index) {
        settings.push_back({.name = listEntryName(FieldHudPoolList, index, LabelField),
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
    };
    for (std::size_t index = 0; index < view.meters.size(); ++index) {
        const FieldMeterView& meter = view.meters.at(index);
        settings.push_back({.name = listEntryName(FieldHudMeterList, index, ValueField), .value = number(meter.value)});
        settings.push_back({.name = listEntryName(FieldHudMeterList, index, MinField), .value = number(meter.min)});
        settings.push_back({.name = listEntryName(FieldHudMeterList, index, MaxField), .value = number(meter.max)});
    }
    for (std::size_t index = 0; index < view.needs.size(); ++index) {
        settings.push_back(
            {.name = listEntryName(FieldHudNeedList, index, ActiveField), .value = view.needs.at(index)});
    }
    for (std::size_t index = 0; index < view.pools.size(); ++index) {
        const FieldPoolView& pool = view.pools.at(index);
        settings.push_back({.name = listEntryName(FieldHudPoolList, index, ValueField), .value = number(pool.current)});
        settings.push_back({.name = listEntryName(FieldHudPoolList, index, MaxField), .value = number(pool.max)});
    }
    return settings;
}

} // namespace dwe::screens
