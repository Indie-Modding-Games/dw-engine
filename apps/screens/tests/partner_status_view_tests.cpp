#include <dwe/assets/digimon_table.hpp>
#include <dwe/screens/partner_status_view.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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
