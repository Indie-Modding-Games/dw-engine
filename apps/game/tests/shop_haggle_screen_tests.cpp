#include <doctest/doctest.h>

#include <array>
#include <string>
#include <string_view>

#include "pause_menu_ui.hpp"
#include "shop_haggle_screen.hpp"
#include "shop_job_rules_panel.hpp"

namespace {

namespace shop_job = dwe::game::shop_job;

// One spendthrift customer buying meat in a normal mood, with the game pack's reactions.
shop_job::ShopJobRules meatRules() {
  shop_job::ShopJobRules rules;
  rules.species.push_back({.script = 199,
                           .species = 80,
                           .count = 1,
                           .patience = 4,
                           .character = "spendthrift",
                           .acceptance = {45, 55, 65, 75, 85, 95, 100, 100, 100}});
  rules.items.push_back({.id = "meat", .count = 1, .prices = {75, 70, 65, 60, 55, 50, 45, 40, 35}});
  rules.moods.push_back({.name = "normal", .weight = 1, .shift = 0});
  const std::array<std::string_view, shop_job::Reactions> names{"delighted", "pleased", "doubtful",
                                                                "annoyed", "furious"};
  const std::array<std::int32_t, shop_job::Reactions> gaps{-2, 0, 1, 2, 10};
  const std::array<std::int32_t, shop_job::Reactions> costs{0, 0, 1, 1, 2};
  for (std::size_t index = 0; index < shop_job::Reactions; ++index) {
    rules.reactions.push_back({.reaction = static_cast<shop_job::Reaction>(index),
                               .name = std::string{names.at(index)},
                               .face = static_cast<std::int32_t>(index),
                               .gapUpTo = gaps.at(index),
                               .patienceCost = costs.at(index)});
  }
  rules.regularLevel = 5;
  rules.talkShift = 1;
  rules.talkCost = 1;
  rules.talkNeeds = 2;
  return rules;
}

const dwe::ui::UiValue* find(const dwe::screens::ScreenSettings& settings, std::string_view name) {
  for (const auto& setting : settings) {
    if (setting.name == name) {
      return &setting.value;
    }
  }
  return nullptr;
}

double number(const dwe::screens::ScreenSettings& settings, std::string_view name) {
  const dwe::ui::UiValue* value = find(settings, name);
  REQUIRE_MESSAGE(value != nullptr, name);
  return std::get<double>(*value);
}

std::string text(const dwe::screens::ScreenSettings& settings, std::string_view name) {
  const dwe::ui::UiValue* value = find(settings, name);
  REQUIRE_MESSAGE(value != nullptr, name);
  return std::get<std::string>(*value);
}

} // namespace

TEST_CASE("the haggle screen's settings carry the customer, the nine rows and the offers") {
  const shop_job::ShopJobRules rules = meatRules();
  shop_job::Haggle haggle{rules, {.species = 0, .item = 0, .mood = 0, .budget = 3}};
  const dwe::app::ShopHaggleView view{
      .rules = &rules,
      .haggle = &haggle,
      .names = {.species = "main.digimon_names.80",
                .speciesIcon = "/texture/stddat/stdtim/001?palette=1",
                .speciesIconRect = "224 32 16 16",
                .item = "main.item_names.3",
                .itemIcon = "/texture/item",
                .itemIconRect = "0 0 16 16"},
      .tiers = {.betterPay = 1792, .joins = 3072, .bonus = 3584},
      .tally = 400,
      .perfectScore = 4150,
      .customer = 3,
      .customers = 20};
  auto settings = dwe::app::shopHaggleSettings(view);
  CHECK(std::get<bool>(*find(settings, "shop_haggle.shown")));
  CHECK(text(settings, "shop_haggle.species") == "main.digimon_names.80");
  CHECK(text(settings, "shop_haggle.species_icon") == "/texture/stddat/stdtim/001?palette=1");
  CHECK(text(settings, "shop_haggle.species_icon_rect") == "224 32 16 16");
  CHECK(text(settings, "shop_haggle.character") == "shop_job.character.spendthrift");
  CHECK(text(settings, "shop_haggle.mood") == "shop_job.mood.normal");
  CHECK(number(settings, "shop_haggle.patience") == 4.0);
  CHECK(number(settings, "shop_haggle.regular_price") == 50.0);
  CHECK(number(settings, "shop_haggle.face") == -1.0);
  CHECK(number(settings, "shop_haggle.goal") == 4150.0);
  CHECK(number(settings, "shop_haggle.rows") == 9.0);
  CHECK(number(settings, "shop_haggle.row.0.price") == 75.0);
  CHECK(number(settings, "shop_haggle.row.0.profit") == 40.0);
  CHECK(number(settings, "shop_haggle.row.0.chance") == 45.0);
  CHECK(number(settings, "shop_haggle.row.8.chance") == 100.0);
  CHECK(number(settings, "shop_haggle.offers") == 0.0);

  // A refused offer at the dearest price: its row is marked and the history has it.
  const shop_job::OfferResult refused = haggle.offer(0);
  REQUIRE_FALSE(refused.sold);
  settings = dwe::app::shopHaggleSettings(view);
  CHECK(number(settings, "shop_haggle.offers") == 1.0);
  CHECK(number(settings, "shop_haggle.offer.0.price") == 75.0);
  CHECK(text(settings, "shop_haggle.offer.0.reaction") == text(settings, "shop_haggle.reaction"));
  CHECK(number(settings, "shop_haggle.face") >= 2.0);
  CHECK(std::get<bool>(*find(settings, "shop_haggle.row.0.offered")));
  CHECK_FALSE(std::get<bool>(*find(settings, "shop_haggle.row.1.offered")));
  CHECK(number(settings, "shop_haggle.row.0.chance") == 0.0);
}

TEST_CASE("the haggle screen without a customer says so") {
  const auto settings = dwe::app::shopHaggleSettings({});
  CHECK_FALSE(std::get<bool>(*find(settings, "shop_haggle.shown")));
}

TEST_CASE("the rules panel's settings carry the tiers and the bonus item") {
  const auto settings =
      dwe::app::shopJobRulesSettings({.tiers = {.betterPay = 1792, .joins = 3072, .bonus = 3584},
                                      .bonusItem = "game.item.steel_heart",
                                      .bonusIcon = "/texture/item",
                                      .bonusIconRect = "0 0 16 16",
                                      .customers = 20,
                                      .talkNeeds = 2,
                                      .improved = true});
  CHECK(number(settings, "shop_job_rules.joins") == 3072.0);
  CHECK(number(settings, "shop_job_rules.bonus") == 3584.0);
  CHECK(text(settings, "shop_job_rules.bonus_item") == "game.item.steel_heart");
  CHECK(std::get<bool>(*find(settings, "shop_job_rules.improved")));
}

TEST_CASE("help opens the rules panel while the job runs; help, Escape or Back close it") {
  dwe::app::test::MenuUi menu;
  const auto document = dwe::assets::AssetId::parse("ui/shop_job_rules/document");
  REQUIRE(document.has_value());
  REQUIRE(menu.ui->loadDocument(dwe::app::ShopJobRulesDocument, *document).has_value());
  dwe::app::ShopJobRulesPanel panel{{.ui = menu.ui.get()}};
  bool closed = false;
  panel.setClosed([&closed] { closed = true; });

  panel.press(true, false, false);
  CHECK_FALSE(panel.shown());
  panel.press(true, false, true);
  CHECK(panel.shown());
  CHECK(menu.ui->isVisible(dwe::app::ShopJobRulesDocument));
  panel.press(false, true, true);
  CHECK_FALSE(panel.shown());
  CHECK_FALSE(menu.ui->isVisible(dwe::app::ShopJobRulesDocument));
  CHECK(closed);

  panel.show();
  panel.press(true, false, true);
  CHECK_FALSE(panel.shown());
}
