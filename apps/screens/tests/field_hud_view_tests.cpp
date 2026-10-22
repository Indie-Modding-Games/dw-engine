#include <dwe/screens/field_hud_view.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>
#include <string>
#include <string_view>

namespace {

using dwe::game::partner::Condition;
using dwe::game::partner::Stat;

dwe::session::PartnerView partner() {
    dwe::session::PartnerView view;
    view.stats[Stat::Hp] = 900;
    view.stats[Stat::Mp] = 400;
    view.currentHp = 650;
    view.currentMp = 120;
    view.happiness = {.value = -30, .min = -100, .max = 100};
    view.discipline = {.value = 70, .min = 0, .max = 100};
    view.conditions.set(Condition::Hungry);
    view.conditions.set(Condition::NeedsToilet);
    view.conditions.set(Condition::Sick);
    return view;
}

const dwe::ui::UiValue* find(const dwe::screens::ScreenSettings& settings, std::string_view name) {
    const auto found = std::ranges::find(settings, name, &dwe::screens::ScreenSetting::name);
    return found == settings.end() ? nullptr : &found->value;
}

} // namespace

TEST_CASE("the field HUD shows the clock, the two bars, the needs and HP and MP from the session") {
    const dwe::ruleset::GameTime time{.year = 0, .day = 4, .hour = 17, .minute = 5};
    const auto view = dwe::screens::fieldHudViewOf(time, partner(), true);
    CHECK(view.shown);
    CHECK(view.clock.hour == 17);
    CHECK(view.clock.minute == 5);
    // game_menu.c: renderDateDigits shows the day and the year counted from 1.
    CHECK(view.clock.day == 5);
    CHECK(view.clock.year == 1);
    CHECK(view.meters.at(0) == dwe::screens::FieldMeterView{.value = -30, .min = -100, .max = 100});
    CHECK(view.meters.at(1) == dwe::screens::FieldMeterView{.value = 70, .min = 0, .max = 100});
    // Hunger and toilet; sickness is not one of the needs.
    CHECK(view.needs.at(0));
    CHECK_FALSE(view.needs.at(1));
    CHECK(view.needs.at(2));
    CHECK(view.pools.at(0) == dwe::screens::FieldPoolView{.current = 650, .max = 900});
    CHECK(view.pools.at(1) == dwe::screens::FieldPoolView{.current = 120, .max = 400});
}

TEST_CASE("the field HUD's view changes only when something it shows changes") {
    const dwe::ruleset::GameTime time{.year = 0, .day = 0, .hour = 8, .minute = 0};
    const auto before = dwe::screens::fieldHudViewOf(time, partner(), true);
    auto aged = partner();
    aged.ageDays = 3;
    aged.weight.value = 40;
    CHECK(dwe::screens::fieldHudViewOf(time, aged, true) == before);
    CHECK_FALSE(dwe::screens::fieldHudViewOf(time, partner(), false) == before);
    const dwe::ruleset::GameTime later{.year = 0, .day = 0, .hour = 8, .minute = 1};
    CHECK_FALSE(dwe::screens::fieldHudViewOf(later, partner(), true) == before);
}

TEST_CASE("the field HUD's settings name each value and each label once") {
    const dwe::ruleset::GameTime time{.year = 2, .day = 29, .hour = 23, .minute = 59};
    const auto settings = dwe::screens::settingsOf(dwe::screens::fieldHudViewOf(time, partner(), true));
    CHECK(*find(settings, "hud.shown") == dwe::ui::UiValue{true});
    CHECK(*find(settings, "hud.clock.hour") == dwe::ui::UiValue{23.0});
    CHECK(*find(settings, "hud.clock.minute") == dwe::ui::UiValue{59.0});
    CHECK(*find(settings, "hud.clock.day") == dwe::ui::UiValue{30.0});
    CHECK(*find(settings, "hud.clock.year") == dwe::ui::UiValue{3.0});
    CHECK(*find(settings, "hud.meter.0.value") == dwe::ui::UiValue{-30.0});
    CHECK(*find(settings, "hud.meter.0.min") == dwe::ui::UiValue{-100.0});
    CHECK(*find(settings, "hud.meter.1.max") == dwe::ui::UiValue{100.0});
    CHECK(*find(settings, "hud.need.0.active") == dwe::ui::UiValue{true});
    CHECK(*find(settings, "hud.need.1.active") == dwe::ui::UiValue{false});
    CHECK(*find(settings, "hud.pool.0.value") == dwe::ui::UiValue{650.0});
    CHECK(*find(settings, "hud.pool.1.max") == dwe::ui::UiValue{400.0});
    // The labels go apart, once: the values never carry text.
    CHECK(find(settings, "hud.meter.0.label") == nullptr);

    const auto labels = dwe::screens::fieldHudLabelSettings();
    CHECK(*find(labels, "hud.meter.0.label") == dwe::ui::UiValue{std::string{"partner_menu.happiness"}});
    CHECK(*find(labels, "hud.meter.1.label") == dwe::ui::UiValue{std::string{"partner_menu.discipline"}});
    CHECK(*find(labels, "hud.meter.0.two_tone") == dwe::ui::UiValue{true});
    CHECK(*find(labels, "hud.need.0.label") == dwe::ui::UiValue{std::string{"hud.need.hunger"}});
    CHECK(*find(labels, "hud.need.1.label") == dwe::ui::UiValue{std::string{"hud.need.sleep"}});
    CHECK(*find(labels, "hud.need.2.label") == dwe::ui::UiValue{std::string{"hud.need.toilet"}});
    // The original's labels of HP and MP (main.stat_labels).
    CHECK(*find(labels, "hud.pool.0.label") == dwe::ui::UiValue{std::string{"main.stat_labels.0000"}});
    CHECK(*find(labels, "hud.pool.1.label") == dwe::ui::UiValue{std::string{"main.stat_labels.0001"}});
    for (const auto& label : labels) {
        CHECK(find(settings, label.name) == nullptr);
    }
}

TEST_CASE("the partner's bubble point goes to the field HUD in percent of the window, only while it is on the screen") {
    using dwe::screens::FieldPartnerView;
    using dwe::screens::WindowFraction;
    const auto view = dwe::screens::fieldPartnerViewOf(WindowFraction{.x = 0.25, .y = 0.123456}, true);
    CHECK(view == FieldPartnerView{.visible = true, .x = 25.0, .y = 12.35});
    // A move under a hundredth of a percent is no change.
    CHECK(dwe::screens::fieldPartnerViewOf(WindowFraction{.x = 0.250001, .y = 0.123457}, true) == view);
    CHECK(dwe::screens::fieldPartnerViewOf(WindowFraction{.x = 0.25, .y = 0.5}, false) == FieldPartnerView{});
    CHECK(dwe::screens::fieldPartnerViewOf(WindowFraction{.x = 1.2, .y = 0.5}, true) == FieldPartnerView{});
    CHECK(dwe::screens::fieldPartnerViewOf(std::nullopt, true) == FieldPartnerView{});

    const auto settings = dwe::screens::settingsOf(view);
    CHECK(*find(settings, "hud.partner.visible") == dwe::ui::UiValue{true});
    CHECK(*find(settings, "hud.partner.x") == dwe::ui::UiValue{25.0});
    CHECK(*find(settings, "hud.partner.y") == dwe::ui::UiValue{12.35});
}
