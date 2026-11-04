// The fishing's notice (fish.c: FISH_tickTextBox): Confirm closes it only once it has been open for
// 10 of the original's frames at 20 a second, Cancel closes it at once, and closing sounds the
// confirm cue (playSound(0, 3)); an early Confirm is silent.

#include <dwe/input/keys.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

constexpr std::string_view Notice = "fishing_notice";

bool answered(Screen& screen) {
  const std::vector<UiCommand> commands = screen.ui->takeCommands();
  return std::ranges::any_of(
      commands, [](const UiCommand& command) { return command.name == "fishing_notice_ok"; });
}

bool sounded(Screen& screen, UiCue what) {
  const std::vector<UiCue> cues = screen.ui->takeCues();
  return std::ranges::find(cues, what) != cues.end();
}

void open(Screen& screen) {
  screen.ui->setSetting("fishing_notice.text", std::string{"fishing.too_soon"});
  screen.ui->show(Notice, true);
  screen.frames(1);
  std::ignore = screen.ui->takeCommands();
  std::ignore = screen.ui->takeCues();
}

} // namespace

TEST_CASE("the fishing notice ignores Confirm for its first 10 frames, then closes with it") {
  Screen screen{Notice};
  open(screen);
  // Two frames at 60 a second: well inside the half second.
  screen.press(input::keys::Return);
  CHECK_FALSE(answered(screen));
  CHECK_FALSE(sounded(screen, UiCue::Confirm));
  screen.frames(30);
  screen.press(input::keys::Return);
  CHECK(answered(screen));
  CHECK(sounded(screen, UiCue::Confirm));
}

TEST_CASE("the fishing notice closes with Cancel at once, with the confirm sound") {
  Screen screen{Notice};
  open(screen);
  screen.press(input::keys::Escape);
  CHECK(answered(screen));
  const std::vector<UiCue> cues = screen.ui->takeCues();
  CHECK(std::ranges::find(cues, UiCue::Confirm) != cues.end());
  CHECK(std::ranges::find(cues, UiCue::Cancel) == cues.end());
}

} // namespace dwe::ui
