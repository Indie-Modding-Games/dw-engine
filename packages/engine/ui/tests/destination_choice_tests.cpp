// Free exploration's chooser (docs/architecture/warps-de-script.md): it opens on the page of the
// story's place with the focus on staying, so confirming without looking never changes the map.

#include <dwe/input/keys.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

constexpr std::string_view Chooser = "destination_choice";

bool sent(const std::vector<UiCommand>& commands, std::string_view name) {
  return std::ranges::any_of(commands,
                             [name](const UiCommand& command) { return command.name == name; });
}

// Two places, the story's the first.
void open(Screen& screen) {
  screen.ui->setSetting("exploration_places", 2.0);
  screen.ui->setSetting("exploration_story", 0.0);
  for (const int place : {0, 1}) {
    screen.ui->setSetting(std::format("exploration_place_name.{}", place), std::string{});
    screen.ui->setSetting(std::format("exploration_place_map.{}", place),
                          std::format("map{}", place));
  }
  screen.ui->show(Chooser, true);
  screen.frames();
  std::ignore = screen.ui->takeCommands();
}

} // namespace

TEST_CASE("the destination chooser starts on staying: confirming at once stays on the map") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{Chooser};
    screen.skin(skin);
    open(screen);
    CHECK(screen.ui->focusedElementId(Chooser) == "stay");
    screen.press(input::keys::Return);
    screen.frames();
    const std::vector<UiCommand> commands = screen.ui->takeCommands();
    CHECK(sent(commands, "stay_here"));
    CHECK_FALSE(sent(commands, "choose_destination"));
  }
}

TEST_CASE("from staying, down reaches the story's place and confirming takes it") {
  Screen screen{Chooser};
  open(screen);
  screen.press(input::keys::Down);
  screen.frames();
  CHECK(screen.ui->focusedElementId(Chooser) == "place-1");
  screen.press(input::keys::Return);
  screen.frames();
  const std::vector<UiCommand> commands = screen.ui->takeCommands();
  CHECK(std::ranges::any_of(commands, [](const UiCommand& command) {
    return command.name == "choose_destination" && command.value == UiValue{0.0};
  }));
  CHECK_FALSE(sent(commands, "stay_here"));
}

} // namespace dwe::ui
