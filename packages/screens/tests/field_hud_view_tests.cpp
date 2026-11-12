#include <dwe/screens/field_hud_view.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
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
  view.conditions.set(Condition::Tired);
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
  // Hunger, toilet, tiredness and sickness (partner_impl.c: handleConditionBubble shows a bubble
  // for each); no sleep nor injury.
  CHECK(view.needs.at(0));
  CHECK_FALSE(view.needs.at(1));
  CHECK(view.needs.at(2));
  CHECK(view.needs.at(3));
  CHECK_FALSE(view.needs.at(4));
  CHECK(view.needs.at(5));
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
  const auto settings =
      dwe::screens::settingsOf(dwe::screens::fieldHudViewOf(time, partner(), true));
  CHECK(*find(settings, "hud.shown") == dwe::ui::UiValue{true});
  CHECK(*find(settings, "hud.clock.hour") == dwe::ui::UiValue{23.0});
  CHECK(*find(settings, "hud.clock.minute") == dwe::ui::UiValue{59.0});
  CHECK(*find(settings, "hud.clock.day") == dwe::ui::UiValue{30.0});
  CHECK(*find(settings, "hud.clock.year") == dwe::ui::UiValue{3.0});
  CHECK(*find(settings, "hud.meter.0.value") == dwe::ui::UiValue{-30.0});
  CHECK(*find(settings, "hud.meter.0.min") == dwe::ui::UiValue{-100.0});
  CHECK(*find(settings, "hud.meter.1.max") == dwe::ui::UiValue{100.0});
  CHECK(*find(settings, "hud.clock.night") == dwe::ui::UiValue{true});
  CHECK(*find(settings, "hud.clock.phase") == dwe::ui::UiValue{std::string{"night"}});
  CHECK(*find(settings, "hud.meter.0.mood") == dwe::ui::UiValue{std::string{"low"}});
  CHECK(*find(settings, "hud.meter.1.mood") == dwe::ui::UiValue{std::string{"high"}});
  CHECK(*find(settings, "hud.need.0.active") == dwe::ui::UiValue{true});
  CHECK(*find(settings, "hud.need.1.active") == dwe::ui::UiValue{false});
  CHECK(*find(settings, "hud.need.3.active") == dwe::ui::UiValue{true});
  CHECK(*find(settings, "hud.pool.0.value") == dwe::ui::UiValue{650.0});
  CHECK(*find(settings, "hud.pool.1.max") == dwe::ui::UiValue{400.0});
  // The labels go apart, once: the values never carry text.
  CHECK(find(settings, "hud.meter.0.label") == nullptr);

  const auto labels = dwe::screens::fieldHudLabelSettings();
  CHECK(*find(labels, "hud.meter.0.label") ==
        dwe::ui::UiValue{std::string{"partner_menu.happiness"}});
  CHECK(*find(labels, "hud.meter.1.label") ==
        dwe::ui::UiValue{std::string{"partner_menu.discipline"}});
  CHECK(*find(labels, "hud.meter.0.two_tone") == dwe::ui::UiValue{true});
  CHECK(*find(labels, "hud.need.0.label") == dwe::ui::UiValue{std::string{"hud.need.hunger"}});
  CHECK(*find(labels, "hud.need.1.label") == dwe::ui::UiValue{std::string{"hud.need.sleep"}});
  CHECK(*find(labels, "hud.need.2.label") == dwe::ui::UiValue{std::string{"hud.need.toilet"}});
  CHECK(*find(labels, "hud.need.3.label") == dwe::ui::UiValue{std::string{"hud.need.tired"}});
  CHECK(*find(labels, "hud.need.4.label") == dwe::ui::UiValue{std::string{"hud.need.injured"}});
  CHECK(*find(labels, "hud.need.5.label") == dwe::ui::UiValue{std::string{"hud.need.sick"}});
  // The original's labels of HP and MP (main.stat_labels).
  CHECK(*find(labels, "hud.pool.0.label") ==
        dwe::ui::UiValue{std::string{"main.stat_labels.0000"}});
  CHECK(*find(labels, "hud.pool.1.label") ==
        dwe::ui::UiValue{std::string{"main.stat_labels.0001"}});
  for (const auto& label : labels) {
    CHECK(find(settings, label.name) == nullptr);
  }
}

TEST_CASE(
    "the field HUD ranks the partner's needs by how soon they do harm, ties in the bubble's order") {
  const dwe::ruleset::GameTime time{.year = 0, .day = 0, .hour = 8, .minute = 0};
  // Hunger (0), toilet (2), tiredness (3) and sickness (5); no deadlines known: the original
  // bubble's order (tiredness, hunger, toilet, sickness).
  auto view = dwe::screens::fieldHudViewOf(time, partner(), true);
  CHECK(view.urgency == std::array<std::int32_t, 6>{2, 0, 3, 1, 0, 4});
  // The toilet's urge runs out first, then the sickness costs a life, then the hunger window
  // closes; tiredness has no deadline, so it goes last.
  auto timed = partner();
  timed.needDeadlines.set(Condition::Hungry, 1800);
  timed.needDeadlines.set(Condition::NeedsToilet, 240);
  timed.needDeadlines.set(Condition::Sick, 1200);
  view = dwe::screens::fieldHudViewOf(time, timed, true);
  CHECK(view.urgency == std::array<std::int32_t, 6>{3, 0, 1, 4, 0, 2});
  // A tie keeps the bubble's order: hunger before the toilet.
  timed.needDeadlines.set(Condition::NeedsToilet, 1800);
  view = dwe::screens::fieldHudViewOf(time, timed, true);
  CHECK(view.urgency == std::array<std::int32_t, 6>{2, 0, 3, 4, 0, 1});
  // Every need at once, the five a stacked bubble shows and a sixth.
  auto all = timed;
  all.conditions.set(Condition::Sleepy);
  all.conditions.set(Condition::Injured);
  all.needDeadlines.set(Condition::Sleepy, 600);
  all.needDeadlines.set(Condition::Injured, 6000);
  view = dwe::screens::fieldHudViewOf(time, all, true);
  CHECK(view.urgency == std::array<std::int32_t, 6>{3, 1, 4, 6, 5, 2});
  const auto settings = dwe::screens::settingsOf(view);
  CHECK(*find(settings, "hud.need.1.urgency") == dwe::ui::UiValue{1.0});
  CHECK(*find(settings, "hud.need.3.urgency") == dwe::ui::UiValue{6.0});
  // A need the partner does not have has none, and a new order is a new view to publish.
  CHECK(*find(dwe::screens::settingsOf(dwe::screens::fieldHudViewOf(time, partner(), true)),
              "hud.need.1.urgency") == dwe::ui::UiValue{0.0});
  CHECK_FALSE(dwe::screens::fieldHudViewOf(time, timed, true) ==
              dwe::screens::fieldHudViewOf(time, partner(), true));
}

TEST_CASE(
    "the field HUD publishes how the partner feels about the map as the original's number, 0, 1 or 2") {
  using dwe::game::partner::AreaResponse;
  const dwe::ruleset::GameTime time{.year = 0, .day = 0, .hour = 8, .minute = 0};
  auto liked = partner();
  liked.area = AreaResponse::Liked;
  auto disliked = partner();
  disliked.area = AreaResponse::Disliked;
  const auto neutralView = dwe::screens::fieldHudViewOf(time, partner(), true);
  const auto likedView = dwe::screens::fieldHudViewOf(time, liked, true);
  const auto dislikedView = dwe::screens::fieldHudViewOf(time, disliked, true);
  CHECK(likedView.area == AreaResponse::Liked);
  // The face changes with it, so the view does too.
  CHECK_FALSE(likedView == neutralView);
  // map.c: MAP_TILE_DATA.partnerAreaResponse.
  CHECK(*find(dwe::screens::settingsOf(neutralView), "hud.partner.area") == dwe::ui::UiValue{0.0});
  CHECK(*find(dwe::screens::settingsOf(likedView), "hud.partner.area") == dwe::ui::UiValue{1.0});
  CHECK(*find(dwe::screens::settingsOf(dislikedView), "hud.partner.area") == dwe::ui::UiValue{2.0});
}

TEST_CASE(
    "the partner's bubble point goes to the field HUD in percent of the window, only while it is on the screen") {
  using dwe::screens::FieldPartnerView;
  using dwe::screens::WindowFraction;
  const auto view =
      dwe::screens::fieldPartnerViewOf(WindowFraction{.x = 0.25, .y = 0.123456}, true);
  CHECK(view == FieldPartnerView{.visible = true, .x = 25.0, .y = 12.3});
  // A move under a tenth of a percent is no change.
  CHECK(dwe::screens::fieldPartnerViewOf(WindowFraction{.x = 0.2504, .y = 0.1234}, true) == view);
  CHECK_FALSE(dwe::screens::fieldPartnerViewOf(WindowFraction{.x = 0.2506, .y = 0.123456}, true) ==
              view);
  CHECK(dwe::screens::fieldPartnerViewOf(WindowFraction{.x = 0.25, .y = 0.5}, false) ==
        FieldPartnerView{});
  CHECK(dwe::screens::fieldPartnerViewOf(WindowFraction{.x = 1.2, .y = 0.5}, true) ==
        FieldPartnerView{});
  CHECK(dwe::screens::fieldPartnerViewOf(std::nullopt, true) == FieldPartnerView{});

  const auto settings = dwe::screens::settingsOf(view);
  CHECK(*find(settings, "hud.partner.visible") == dwe::ui::UiValue{true});
  CHECK(*find(settings, "hud.partner.x") == dwe::ui::UiValue{25.0});
  CHECK(*find(settings, "hud.partner.y") == dwe::ui::UiValue{12.3});
}

TEST_CASE(
    "the evolving bubble's frame goes to the field HUD, its point shown while the field waits") {
  using dwe::screens::FieldPartnerView;
  using dwe::screens::WindowFraction;
  const WindowFraction head{.x = 0.5, .y = 0.25};
  // partner.c: partnerTickEvolving stops the game time, and the bubble still shows (main/evl.c:47).
  const auto evolving = dwe::screens::fieldPartnerViewOf(head, false, 12);
  CHECK(evolving == FieldPartnerView{.visible = true, .x = 50.0, .y = 25.0, .evolvingFrame = 12});
  CHECK(*find(dwe::screens::settingsOf(evolving), "hud.partner.evolving") ==
        dwe::ui::UiValue{12.0});
  // Each frame is a new view to publish.
  CHECK_FALSE(dwe::screens::fieldPartnerViewOf(head, false, 13) == evolving);
  // Without the bubble the stopped field shows no point, and the running one no evolving frame.
  CHECK(dwe::screens::fieldPartnerViewOf(head, false, 0) == FieldPartnerView{});
  CHECK(*find(dwe::screens::settingsOf(dwe::screens::fieldPartnerViewOf(head, true)),
              "hud.partner.evolving") == dwe::ui::UiValue{0.0});
  // Off the screen, nothing.
  CHECK(dwe::screens::fieldPartnerViewOf(std::nullopt, false, 12) == FieldPartnerView{});
}

TEST_CASE("the field HUD tells the night, the part of the day and a bar's low half itself") {
  // The dial's moon before 6:00 and from 17:00 (clock.c: renderGameClock).
  CHECK(dwe::screens::isNightHour(5));
  CHECK_FALSE(dwe::screens::isNightHour(6));
  CHECK_FALSE(dwe::screens::isNightHour(16));
  CHECK(dwe::screens::isNightHour(17));
  CHECK(dwe::screens::dayPhaseOf(0) == "night");
  CHECK(dwe::screens::dayPhaseOf(6) == "morning");
  CHECK(dwe::screens::dayPhaseOf(7) == "morning");
  CHECK(dwe::screens::dayPhaseOf(8) == "day");
  CHECK(dwe::screens::dayPhaseOf(14) == "day");
  CHECK(dwe::screens::dayPhaseOf(15) == "late");
  CHECK(dwe::screens::dayPhaseOf(16) == "late");
  CHECK(dwe::screens::dayPhaseOf(17) == "night");
  // Below the middle of the range, as the original swaps the bar's icon.
  CHECK(dwe::screens::meterMoodOf({.value = -1, .min = -100, .max = 100}) == "low");
  CHECK(dwe::screens::meterMoodOf({.value = 0, .min = -100, .max = 100}) == "high");
  CHECK(dwe::screens::meterMoodOf({.value = 49, .min = 0, .max = 100}) == "low");
  CHECK(dwe::screens::meterMoodOf({.value = 50, .min = 0, .max = 100}) == "high");
}
