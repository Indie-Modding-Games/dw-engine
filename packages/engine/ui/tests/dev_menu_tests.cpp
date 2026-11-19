// The developer menu (docs/modules/apps/game.md, «Developer tools»): its first page offers the
// Techniques entry, which runs learn_all and says what it did, in every skin and language, and
// the whole page stays inside the window.

#include <doctest/doctest.h>

#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

constexpr std::string_view Menu = "dev_menu";
constexpr float Width = 1280.0F;
constexpr float Height = 720.0F;

std::vector<std::string> runLines(const std::vector<UiCommand>& commands) {
  std::vector<std::string> lines;
  for (const UiCommand& command : commands) {
    if (command.name == "dev_menu_run") {
      lines.push_back(std::get<std::string>(command.value));
    }
  }
  return lines;
}

} // namespace

TEST_CASE("the developer menu's Techniques entry runs learn_all and says so, in every skin") {
  for (const std::string_view skin : {"original", "modern", "pixel", "devvago"}) {
    for (const std::string_view language : {"en", "es"}) {
      CAPTURE(skin);
      CAPTURE(language);
      Screen screen{Menu, language};
      screen.skin(skin);
      screen.ui->show(Menu, true);
      screen.frames();
      std::ignore = screen.ui->takeCommands();
      const bool english = language == "en";
      CHECK(screen.ui->elementText(Menu, "entry-7") == (english ? "Techniques" : "Técnicas"));
      CHECK(screen.ui->elementText(Menu, "entry-9") == (english ? "Save now" : "Guardar ahora"));
      // Every entry of the first page, the last one and the way back in the window.
      for (const std::string_view id : {"entry-1", "entry-9", "back", "done"}) {
        CAPTURE(id);
        const auto rect = screen.ui->elementRect(Menu, id);
        REQUIRE(rect.has_value());
        CHECK(rect->top >= 0.0F);
        CHECK(rect->bottom <= Height);
        CHECK(rect->right <= Width);
      }
      CHECK(screen.ui->elementShown(Menu, "entry-9"));
      // Down six times from the first entry reaches Techniques.
      for (int step = 0; step < 6; ++step) {
        screen.press(input::keys::Down);
      }
      screen.frames();
      CHECK(screen.ui->focusedElementId(Menu) == "entry-7");
      screen.press(input::keys::Return);
      screen.frames();
      CHECK(runLines(screen.ui->takeCommands()) == std::vector<std::string>{R"(learn_all(""))"});
      CHECK(screen.ui->elementText(Menu, "done") ==
            (english ? "Learned every technique it can" : "Aprendió todas las técnicas que puede"));
      // The menu stays open, on the same entry.
      CHECK(screen.ui->focusedElementId(Menu) == "entry-7");
    }
  }
}

TEST_CASE("a list page of the developer menu leaves the first page's ninth slot empty") {
  Screen screen{Menu};
  screen.ui->setSetting("dev_menu.species.count", 12.0);
  for (int index = 0; index < 12; ++index) {
    screen.ui->setSetting(std::format("dev_menu.species.{}.id", index),
                          std::format("form_{}", index));
    screen.ui->setSetting(std::format("dev_menu.species.{}.name", index), std::string{});
  }
  screen.ui->show(Menu, true);
  screen.frames();
  // Evolve, the sixth entry.
  for (int step = 0; step < 5; ++step) {
    screen.press(input::keys::Down);
  }
  screen.press(input::keys::Return);
  screen.frames();
  CHECK(screen.ui->elementText(Menu, "entry-8") == "form_7");
  CHECK(screen.ui->elementShown(Menu, "entry-8"));
  CHECK_FALSE(screen.ui->elementShown(Menu, "entry-9"));
}

} // namespace dwe::ui
