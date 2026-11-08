#include <doctest/doctest.h>

#include <string>
#include <variant>

#include "dev_menu_screen.hpp"
#include "dev_tools.hpp"

namespace {

std::string text(const dwe::screens::ScreenSettings& settings, std::string_view name) {
  for (const auto& setting : settings) {
    if (setting.name == name) {
      return std::get<std::string>(setting.value);
    }
  }
  return {};
}

double number(const dwe::screens::ScreenSettings& settings, std::string_view name) {
  for (const auto& setting : settings) {
    if (setting.name == name) {
      return std::get<double>(setting.value);
    }
  }
  return -1.0;
}

} // namespace

TEST_CASE("developer tools: the menu runs only the console lines its pages write") {
  CHECK(dwe::app::devMenuLineAllowed(R"(give_item("item_a 10"))"));
  CHECK(dwe::app::devMenuLineAllowed(R"(story_set("money 1000"))"));
  CHECK(dwe::app::devMenuLineAllowed(R"(partner_set("happiness 100"))"));
  CHECK(dwe::app::devMenuLineAllowed(R"(warp("map_a 2"))"));
  CHECK(dwe::app::devMenuLineAllowed(R"(sleep(""))"));
  // Commands the menu never writes.
  CHECK_FALSE(dwe::app::devMenuLineAllowed(R"(save("1"))"));
  CHECK_FALSE(dwe::app::devMenuLineAllowed(R"(os.exit(""))"));
  // An argument that would close the string and go on with more code.
  CHECK_FALSE(dwe::app::devMenuLineAllowed(R"(give_item("a") os.exit("x"))"));
  CHECK_FALSE(dwe::app::devMenuLineAllowed(R"(give_item("a\" .. x .. \""))"));
  CHECK_FALSE(dwe::app::devMenuLineAllowed("give_item(\"a\nb\")"));
  CHECK_FALSE(dwe::app::devMenuLineAllowed(R"(give_item("a]]"))"));
  CHECK_FALSE(dwe::app::devMenuLineAllowed("give_item"));
}

TEST_CASE("developer tools: the menu's lists are published with their ids and name keys") {
  const dwe::app::DevMenuLists lists{
      .items = {{.id = "item_a", .nameKey = "items.a"}, {.id = "item_b", .nameKey = "items.b"}},
      .species = {{.id = "form_a", .nameKey = "species.a"}},
      .maps = {}};
  const dwe::screens::ScreenSettings settings = dwe::app::devMenuSettings(lists);
  CHECK(number(settings, "dev_menu.items.count") == doctest::Approx(2.0));
  CHECK(text(settings, "dev_menu.items.1.id") == "item_b");
  CHECK(text(settings, "dev_menu.items.1.name") == "items.b");
  CHECK(number(settings, "dev_menu.species.count") == doctest::Approx(1.0));
  CHECK(text(settings, "dev_menu.species.0.id") == "form_a");
  CHECK(number(settings, "dev_menu.maps.count") == doctest::Approx(0.0));
}
