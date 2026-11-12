#include <dwe/ui/ui_addon.hpp>

#include <doctest/doctest.h>

#include <string>

namespace dwe::ui {

TEST_CASE("a UI addon's manifest names its screens' documents inside its folder") {
  const auto addon = parseUiAddon("id: big_letters\n"
                                  "name: Big letters\n"
                                  "version: 1.2.0\n"
                                  "ui_api: \"0.8\"\n"
                                  "type: ui\n"
                                  "screens:\n"
                                  "  pause_menu: pause/document\n"
                                  "  hud: hud/document\n",
                                  "ui/addons/big_letters");
  REQUIRE(addon);
  CHECK(addon->id == "big_letters");
  CHECK(addon->name == "Big letters");
  CHECK(addon->version == "1.2.0");
  CHECK(addon->type == UiAddonType::Ui);
  REQUIRE(addon->screens.size() == 2);
  CHECK(addon->screens.at("pause_menu").str() == "ui/addons/big_letters/pause/document");
  CHECK(addon->uiApi.contains({.major = 0, .minor = 8, .patch = 0}));
  CHECK(addon->uiApi.contains({.major = 0, .minor = 30, .patch = 2}));
  CHECK_FALSE(addon->uiApi.contains({.major = 0, .minor = 7, .patch = 9}));
  CHECK_FALSE(addon->uiApi.contains({.major = 1, .minor = 0, .patch = 0}));
}

TEST_CASE("a plugin's manifest lists the documents it adds") {
  const auto addon = parseUiAddon("id: clock\nname: Clock\nversion: '1'\nui_api: '>=0.8 <0.12'\n"
                                  "type: plugin\ndocuments: [clock/document]\n",
                                  "ui/addons/clock");
  REQUIRE(addon);
  CHECK(addon->type == UiAddonType::Plugin);
  REQUIRE(addon->documents.size() == 1);
  CHECK(addon->documents[0].str() == "ui/addons/clock/clock/document");
  CHECK(addon->uiApi.contains({.major = 0, .minor = 11, .patch = 9}));
  CHECK_FALSE(addon->uiApi.contains({.major = 0, .minor = 12, .patch = 0}));
}

TEST_CASE("a bad UI addon manifest is refused with what is wrong") {
  const std::string head = "id: odd\nname: Odd\nversion: '1'\nui_api: '0.8'\n";
  const auto refused = [](const std::string& yaml) {
    return !parseUiAddon(yaml, "ui/addons/odd").has_value();
  };
  CHECK(refused("id: odd\nname: Odd\n"));                                     // missing keys
  CHECK(refused(head + "type: ui\nscreens: {hud: hud/document}\nicon: x\n")); // unknown key
  CHECK(refused("id: Odd!\nname: Odd\nversion: '1'\nui_api: '0.8'\ntype: ui\n"
                "screens: {hud: hud/document}\n"));
  CHECK(refused(head + "type: skin\nscreens: {hud: hud/document}\n"));
  CHECK(refused(head + "type: ui\n")); // no screens
  CHECK(refused(head + "type: ui\nscreens: {hud: ../../game/ui/hud/document}\n"));
  CHECK(refused(head + "type: ui\nscreens: {hud: /ui/hud/document}\n"));
  CHECK(refused(head + "type: ui\nscreens: {Hud: hud/document}\n"));
  CHECK(refused(head + "type: ui\nscreens: [hud/document]\n"));
  CHECK(refused(head + "type: ui\nscreens: {hud: hud/document}\ndocuments: [a/document]\n"));
  CHECK(refused(head + "type: plugin\n")); // no documents
  CHECK(refused(head + "type: plugin\ndocuments: []\n"));
  CHECK(refused(head + "type: plugin\nscreens: {hud: hud/document}\n"));
  CHECK(refused("id: odd\nname: ''\nversion: '1'\nui_api: '0.8'\ntype: plugin\n"
                "documents: [a/document]\n"));
}

TEST_CASE("a range of UI API versions reads a version or its bounds") {
  CHECK(parseUiApiRange("0.8").has_value());
  CHECK(parseUiApiRange("0.8.2").has_value());
  CHECK(parseUiApiRange(">=0.8").has_value());
  CHECK(parseUiApiRange("<0.12").has_value());
  CHECK(parseUiApiRange(">=0.8 <0.12").has_value());
  CHECK_FALSE(parseUiApiRange("").has_value());
  CHECK_FALSE(parseUiApiRange("8").has_value());
  CHECK_FALSE(parseUiApiRange("^0.8").has_value());
  CHECK_FALSE(parseUiApiRange("<=0.8").has_value());
  CHECK_FALSE(parseUiApiRange(">=0.8 >=0.9").has_value());
  CHECK_FALSE(parseUiApiRange(">=0.9 <0.9").has_value());
  CHECK_FALSE(parseUiApiRange("0.x").has_value());
}

} // namespace dwe::ui
