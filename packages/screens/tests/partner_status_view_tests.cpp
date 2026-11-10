#include <dwe/assets/digimon_table.hpp>
#include <dwe/screens/partner_status_view.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "packages/game/inventory/tests/fixture.hpp"

namespace {

using dwe::game::partner::Condition;
using dwe::game::partner::Stat;

dwe::session::PartnerView partner() {
  dwe::session::PartnerView view;
  view.species = "hero";
  view.ageDays = 4;
  view.lives = 3;
  view.stats[Stat::Hp] = 1200;
  view.stats[Stat::Mp] = 800;
  view.stats[Stat::Offense] = 120;
  view.stats[Stat::Defense] = 90;
  view.stats[Stat::Speed] = 70;
  view.stats[Stat::Brain] = 60;
  view.currentHp = 1000;
  view.currentMp = 750;
  view.weight = {.value = 21, .min = 5, .max = 99};
  view.happiness = {.value = -10, .min = -100, .max = 100};
  view.discipline = {.value = 40, .min = 0, .max = 100};
  view.virusBar = {.value = 2, .min = 0, .max = 16};
  view.tiredness = {.value = 35, .min = 0, .max = 100};
  view.careMistakes = 4;
  view.conditions.set(Condition::Hungry);
  view.conditions.set(Condition::Sick);
  return view;
}

} // namespace

TEST_CASE("the status page shows what the original's does, in its order") {
  dwe::assets::DigimonTable species;
  dwe::assets::DigimonRow hero;
  hero.id = "hero";
  hero.index = 3;
  species.digimon.push_back(hero);
  const dwe::screens::NameKeys names{&species, nullptr, nullptr};

  const auto view = dwe::screens::partnerStatusViewOf(partner(), names);
  CHECK(view.nameKey == "main.digimon_names.0003");
  CHECK(view.ageDays == 4);
  CHECK(view.weight == 21);
  CHECK(view.lives == 3);
  CHECK(view.hp == 1000);
  CHECK(view.maxHp == 1200);
  CHECK(view.mp == 750);
  CHECK(view.maxMp == 800);
  // game_menu.c: renderDigimonStatsView: the HP and MP bars show the maximums over 9999, the stat
  // bars over 999.
  CHECK(view.pools.at(0) == dwe::screens::MeterView{.labelKey = "main.stat_labels.0000",
                                                    .value = 1200,
                                                    .min = 0,
                                                    .max = 9999,
                                                    .twoTone = false});
  CHECK(view.pools.at(1).labelKey == "main.stat_labels.0001");
  CHECK(view.pools.at(1).value == 800);
  CHECK(view.stats.at(0) == dwe::screens::MeterView{.labelKey = "main.stat_labels.0002",
                                                    .value = 120,
                                                    .min = 0,
                                                    .max = 999,
                                                    .twoTone = false});
  CHECK(view.stats.at(3).labelKey == "main.stat_labels.0005");
  REQUIRE(view.meters.size() == 3);
  CHECK(view.meters[0] == dwe::screens::MeterView{.labelKey = "partner_menu.happiness",
                                                  .value = -10,
                                                  .min = -100,
                                                  .max = 100,
                                                  .twoTone = true});
  // game_menu.c: renderDigimonStatsView: happiness and discipline are two-tone bars, the virus bar
  // a plain one.
  CHECK(view.meters[1].twoTone);
  CHECK(view.meters[2].labelKey == "partner_menu.virus");
  CHECK_FALSE(view.meters[2].twoTone);
  CHECK(view.conditionKeys ==
        std::vector<std::string>{"partner_menu.condition.hungry", "partner_menu.condition.sick"});

  const auto settings = dwe::screens::settingsOf(view);
  CHECK(std::ranges::any_of(settings, [](const dwe::screens::ScreenSetting& setting) {
    return setting.name == "partner.condition.1.key";
  }));
  CHECK(std::ranges::any_of(settings, [](const dwe::screens::ScreenSetting& setting) {
    return setting.name == "partner.pool.1.max";
  }));
}

TEST_CASE("the status page names the species' favourite food, empty for a species without one") {
  const auto& items = dwe::game::inventory::testing::fixtureItems();
  const dwe::screens::NameKeys names{nullptr, &items, nullptr};
  auto view = dwe::screens::partnerStatusViewOf(partner(), names);
  CHECK(view.favoriteFoodKey.empty());
  dwe::screens::setFavoriteFood(view, dwe::game::inventory::testing::Bun, names);
  CHECK(view.favoriteFoodKey == names.item(dwe::game::inventory::testing::Bun));
  CHECK_FALSE(view.favoriteFoodKey.empty());
  const auto settings = dwe::screens::settingsOf(view);
  CHECK(std::ranges::any_of(settings, [&](const dwe::screens::ScreenSetting& setting) {
    return setting.name == "partner.favorite_food" &&
           setting.value == dwe::ui::UiValue{view.favoriteFoodKey};
  }));
  dwe::screens::setFavoriteFood(view, std::nullopt, names);
  CHECK(view.favoriteFoodKey.empty());
  // An id the item table does not know names nothing either.
  dwe::screens::setFavoriteFood(view, "no_such_item", names);
  CHECK(view.favoriteFoodKey.empty());
}

TEST_CASE("the status page's extras carry the tiredness and the care mistakes the original hides") {
  const dwe::screens::NameKeys names{nullptr, nullptr, nullptr};
  const auto view = dwe::screens::partnerStatusViewOf(partner(), names);
  CHECK(view.tiredness == dwe::screens::MeterView{.labelKey = "partner_menu.tiredness",
                                                  .value = 35,
                                                  .min = 0,
                                                  .max = 100,
                                                  .twoTone = false});
  CHECK(view.careMistakes == 4);
  // The original's three bars stay as they are; the tiredness is not one of them.
  CHECK(view.meters.size() == 3);

  const auto settings = dwe::screens::settingsOf(view);
  const auto valueOf = [&settings](std::string_view name) -> const dwe::screens::ScreenSetting* {
    const auto found =
        std::ranges::find_if(settings, [name](const dwe::screens::ScreenSetting& setting) {
          return setting.name == name;
        });
    return found == settings.end() ? nullptr : &*found;
  };
  const auto* tired = valueOf("partner.tiredness.value");
  REQUIRE(tired != nullptr);
  CHECK(std::get<double>(tired->value) == doctest::Approx(35.0));
  const auto* top = valueOf("partner.tiredness.max");
  REQUIRE(top != nullptr);
  CHECK(std::get<double>(top->value) == doctest::Approx(100.0));
  const auto* label = valueOf("partner.tiredness.label");
  REQUIRE(label != nullptr);
  CHECK(std::get<std::string>(label->value) == "partner_menu.tiredness");
  const auto* mistakes = valueOf("partner.care_mistakes");
  REQUIRE(mistakes != nullptr);
  CHECK(std::get<double>(mistakes->value) == doctest::Approx(4.0));
}

TEST_CASE("the status page's extras carry the battles, the lifetime, the evolution counter and the "
          "waking hours") {
  auto partnerView = partner();
  partnerView.battles = 7;
  partnerView.lifetimeHoursLeft = 353;
  partnerView.evolutionHours = 5;
  partnerView.wakeHour = 8;
  partnerView.wakeMinute = 0;
  partnerView.sleepyHour = 20;
  partnerView.sleepyMinute = 30;
  const dwe::screens::NameKeys names{nullptr, nullptr, nullptr};
  auto view = dwe::screens::partnerStatusViewOf(partnerView, names);
  CHECK(view.battles == 7);
  CHECK(view.lifetimeHours == 353);
  CHECK(view.evolutionHours == 5);
  // The check's hours come from the evolution rules, which the menu adds; the view alone has none.
  CHECK_FALSE(view.evolutionCheckHours.has_value());
  CHECK(view.wakeHour == 8);
  CHECK(view.sleepyHour == 20);
  CHECK(view.sleepyMinute == 30);

  const auto numberOf = [](const dwe::screens::ScreenSettings& settings, std::string_view name) {
    const auto found =
        std::ranges::find_if(settings, [name](const dwe::screens::ScreenSetting& setting) {
          return setting.name == name;
        });
    REQUIRE(found != settings.end());
    return std::get<double>(found->value);
  };
  const auto settings = dwe::screens::settingsOf(view);
  CHECK(numberOf(settings, "partner.battles") == doctest::Approx(7.0));
  CHECK(numberOf(settings, "partner.lifetime_hours") == doctest::Approx(353.0));
  CHECK(numberOf(settings, "partner.evolution.hours") == doctest::Approx(5.0));
  // A level that never evolves on its own publishes -1, which the page shows as the counter alone.
  CHECK(numberOf(settings, "partner.evolution.check_hours") == doctest::Approx(-1.0));
  CHECK(numberOf(settings, "partner.awake.wake_hour") == doctest::Approx(8.0));
  CHECK(numberOf(settings, "partner.awake.wake_minute") == doctest::Approx(0.0));
  CHECK(numberOf(settings, "partner.awake.sleepy_hour") == doctest::Approx(20.0));
  CHECK(numberOf(settings, "partner.awake.sleepy_minute") == doctest::Approx(30.0));

  view.evolutionCheckHours = 72;
  CHECK(numberOf(dwe::screens::settingsOf(view), "partner.evolution.check_hours") ==
        doctest::Approx(72.0));
}
