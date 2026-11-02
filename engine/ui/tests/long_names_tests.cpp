// The engine's long names (docs/modulos/apps/game.md, «Nombres largos»): the documents that show a
// name of up to twelve letters keep it inside their boxes, and leave the original's texts as they
// are.

#include <doctest/doctest.h>

#include <format>
#include <string>
#include <string_view>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

constexpr std::string_view LongName = "WWWWWWWWWWWW";

// A speech box with the speaker's row and one row of text, as the game publishes it.
void publishSpeech(Ui& ui, std::string_view text, double revision) {
  ui.setSetting("story_textbox.open", true);
  ui.setSetting("story_textbox.kind", std::string{"speech"});
  ui.setSetting("story_textbox.opening", 1.0);
  ui.setSetting("story_textbox.row_count", 2.0);
  ui.setSetting("story_textbox.row.0.runs", 1.0);
  ui.setSetting("story_textbox.row.0.run.0.text", std::string{"Jijimon"});
  ui.setSetting("story_textbox.row.0.run.0.color", 7.0);
  ui.setSetting("story_textbox.row.1.runs", 1.0);
  ui.setSetting("story_textbox.row.1.run.0.text", std::string{text});
  ui.setSetting("story_textbox.row.1.run.0.color", 1.0);
  ui.setSetting("story_textbox.revision", revision);
}

} // namespace

TEST_CASE(
    "a speech row a long name makes too wide is squeezed into the box, a row that fits is not") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"story_textbox"};
    screen.skin(skin);
    screen.ui->show("story_textbox");
    // Two long names on one row: wider than the box in either font.
    publishSpeech(*screen.ui, std::format("{} and {}, look!", LongName, LongName), 1.0);
    screen.frames();
    const std::string squeezed = screen.ui->styleProperty("story_textbox", "row-1", "transform");
    MESSAGE(squeezed);
    CHECK(squeezed.find("scale") != std::string::npos);
    CHECK(screen.ui->styleProperty("story_textbox", "row-0", "transform").find("scale") ==
          std::string::npos);

    // The same row with the original's six letters fits: nothing is squeezed.
    publishSpeech(*screen.ui, "WWWWWW, this isn't", 2.0);
    screen.frames();
    CHECK(screen.ui->styleProperty("story_textbox", "row-1", "transform").find("scale") ==
          std::string::npos);
  }
}

TEST_CASE("the naming box shows twelve letters inside the name's box in both skins") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"naming_box"};
    screen.skin(skin);
    screen.ui->setSetting("naming_box.length", 12.0);
    for (int letter = 0; letter < 12; ++letter) {
      screen.ui->setSetting(std::format("naming_box.name.{}", letter), std::string{"W"});
    }
    screen.ui->setSetting("naming_box.revision", 1.0);
    screen.ui->show("naming_box");
    screen.frames();
    CHECK(screen.ui->elementText("naming_box", "letter-11") == "W");
    CHECK_FALSE(screen.ui->elementHasClass("naming_box", "letter-11", "hidden"));
    const auto box = screen.ui->elementRect("naming_box", "name-box");
    const auto last = screen.ui->elementRect("naming_box", "letter-11");
    REQUIRE(box.has_value());
    REQUIRE(last.has_value());
    CHECK(last->right <= box->right);

    // The original's six letters hide the rest.
    screen.ui->setSetting("naming_box.length", 6.0);
    screen.ui->setSetting("naming_box.revision", 2.0);
    screen.frames();
    CHECK(screen.ui->elementHasClass("naming_box", "letter-6", "hidden"));
  }
}

TEST_CASE("the original look's player page widens the name's box for a name past six letters") {
  Screen screen{"pause_menu"};
  screen.skin("original");
  screen.ui->setSetting("player.name", std::string{LongName});
  screen.ui->show("pause_menu", true);
  screen.frames();
  CHECK(screen.ui->elementHasClass("pause_menu", "panel", "long-player-name"));
  screen.ui->setSetting("player.name", std::string{"WWWWWW"});
  screen.frames();
  CHECK_FALSE(screen.ui->elementHasClass("pause_menu", "panel", "long-player-name"));
}

} // namespace dwe::ui
