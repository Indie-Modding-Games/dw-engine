#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <tuple>
#include <variant>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

constexpr std::string_view Battle = "battle";
constexpr double FinisherCommand = 11.0;
constexpr double FleeCommand = 1.0;
constexpr double YourCallCommand = 3.0;

void setEntry(Ui& ui, int index, double command, std::string_view label, std::string_view icon,
              bool current) {
  const std::string prefix = "battle.menu." + std::to_string(index) + ".";
  ui.setSetting(prefix + "command", command);
  ui.setSetting(prefix + "label", std::string{label});
  ui.setSetting(prefix + "icon", std::string{icon});
  ui.setSetting(prefix + "available", command != FinisherCommand);
  ui.setSetting(prefix + "current", current);
  ui.setSetting(prefix + "mp_cost", 0.0);
}

bool gaveFlee(Ui& ui) {
  for (const UiCommand& command : ui.takeCommands()) {
    const auto* number = std::get_if<double>(&command.value);
    if (command.name == "battle_command" && number != nullptr && *number == FleeCommand) {
      return true;
    }
  }
  return false;
}

} // namespace

TEST_CASE("the keys reach the flee command of a wild battle's strip in every skin") {
  for (const std::string_view skin : {"original", "modern", "pixel", "devvago"}) {
    CAPTURE(skin);
    fixture::Screen screen{Battle, "es"};
    screen.skin(skin);
    Ui& ui = *screen.ui;
    ui.setSetting("battle.phase", std::string{"fight"});
    ui.setSetting("battle.menu.count", 3.0);
    setEntry(ui, 0, FinisherCommand, "battle.command.finisher", "", false);
    setEntry(ui, 1, FleeCommand, "battle.command.flee", "command-1", false);
    setEntry(ui, 2, YourCallCommand, "battle.command.your_call", "command-3", true);
    ui.show(Battle, true);
    screen.frames();
    std::ignore = ui.takeCommands();
    REQUIRE(ui.focusedElementId(Battle) == "command-2");

    const auto flee = ui.elementRect(Battle, "command-1");
    const auto yourCall = ui.elementRect(Battle, "command-2");
    REQUIRE(flee.has_value());
    REQUIRE(yourCall.has_value());
    const bool fleeOnTheRight = flee->left + flee->right > yourCall->left + yourCall->right;
    screen.press(fleeOnTheRight ? input::keys::Right : input::keys::Left);
    screen.frames();
    CHECK(ui.focusedElementId(Battle) == "command-1");
    screen.press(input::keys::Return);
    screen.frames();
    CHECK(gaveFlee(ui));

    screen.press(fleeOnTheRight ? input::keys::Left : input::keys::Right);
    screen.frames();
    CHECK(ui.focusedElementId(Battle) == "command-2");
  }
}

} // namespace dwe::ui
