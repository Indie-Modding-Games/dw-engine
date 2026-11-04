// The button glyphs in the game pack's documents (docs/architecture/ui.md, «Glifos de botones»):
// the story box's texts and its waiting button follow the device in use.

#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

// A speech box with one row of the original's text, waiting for the player.
void publishWaitingRow(Ui& ui, std::string_view text) {
  ui.setSetting("story_textbox.open", true);
  ui.setSetting("story_textbox.kind", std::string{"speech"});
  ui.setSetting("story_textbox.opening", 1.0);
  ui.setSetting("story_textbox.prompt", true);
  ui.setSetting("story_textbox.row_count", 1.0);
  ui.setSetting("story_textbox.row.0.runs", 2.0);
  ui.setSetting("story_textbox.row.0.run.0.text", std::string{text});
  ui.setSetting("story_textbox.row.0.run.0.color", 6.0);
  ui.setSetting("story_textbox.row.0.run.1.text", std::string{"to match."});
  ui.setSetting("story_textbox.row.0.run.1.color", 1.0);
  ui.setSetting("story_textbox.revision", 1.0);
}

// Plugs in a gamepad of a family (one per family) and touches it.
void touchPad(Screen& screen, platform::GamepadFamily family) {
  const auto Pad = static_cast<platform::GamepadId>(10 + static_cast<int>(family));
  screen.input.handle(platform::GamepadConnected{.id = Pad, .family = family, .name = "pad"});
  screen.input.handle(platform::GamepadButtonChanged{
      .id = Pad, .button = platform::GamepadButton::LeftStick, .down = true});
  screen.frames(1);
  screen.input.handle(platform::GamepadButtonChanged{
      .id = Pad, .button = platform::GamepadButton::LeftStick, .down = false});
  screen.frames(2);
}

} // namespace

TEST_CASE("the story box's button characters and its waiting button follow the device in use") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"story_textbox"};
    screen.skin(skin);
    screen.ui->show("story_textbox");
    // "Use △ " is the original's slot game help: △ is the cross's cell of its font.
    publishWaitingRow(*screen.ui, "Use \xE2\x96\xB3 ");
    screen.frames();

    // The keyboard: confirm's key, the space bar.
    std::string run = screen.ui->elementText("story_textbox", "row-0-run-0");
    CHECK(run.contains(R"(class="key glyph glyph-key")"));
    CHECK(run.contains(">Space<"));
    CHECK_FALSE(run.contains("\xE2\x96\xB3"));
    CHECK(screen.ui->elementHasClass("story_textbox", "prompt", "glyph-key"));
    CHECK_FALSE(screen.ui->drawnRectsOf("story_textbox", "prompt").empty());

    // The original's pad: the cross, as the original font's character (classic) or the drawn
    // shape (modern).
    touchPad(screen, platform::GamepadFamily::PlayStation);
    run = screen.ui->elementText("story_textbox", "row-0-run-0");
    CHECK(run.contains("shape-cross"));
    CHECK(run.contains("glyph-art"));
    CHECK(screen.ui->elementHasClass("story_textbox", "prompt", "shape-cross"));

    // A family with letters, live: A.
    touchPad(screen, platform::GamepadFamily::Xbox);
    run = screen.ui->elementText("story_textbox", "row-0-run-0");
    CHECK(run.contains(R"(glyph-label">A<)"));
    CHECK(screen.ui->elementHasClass("story_textbox", "prompt", "family-letters"));
    CHECK_FALSE(screen.ui->elementHasClass("story_textbox", "prompt", "shape-cross"));
  }
}

TEST_CASE("the menus' prompts and hints name the device's control as glyphs") {
  Screen screen{"pause_box"};
  screen.ui->show("pause_box");
  screen.frames();
  // pause_menu.settings_hint: "{key} Options" with the settings key, F10.
  CHECK(screen.ui->elementText("pause_box", "hint").contains(">F10<"));
  touchPad(screen, platform::GamepadFamily::Generic);
  // A gamepad the game does not know: its Back button by name.
  CHECK(screen.ui->elementText("pause_box", "hint").contains("glyph-button"));
}

} // namespace dwe::ui
