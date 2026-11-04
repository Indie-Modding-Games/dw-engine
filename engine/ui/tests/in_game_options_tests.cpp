// The game's options while playing (docs/modulos/apps/game.md, «Opciones en partida»): The
// pad's Select opens the pause menu on its options page, which lists the language, the
// original-looking text and every option of ui/common/game_options.lua without a row of its
// own in the document.

#include <dwe/input/keys.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

// The game's options as ui/common/game_options.lua lists them, in order.
constexpr std::array<std::string_view, 24> GameOptions{
    "free-exploration",  "smooth-motion", "save-notice",
    "digital-time",      "bulk-trades",   "evolution-requirements",
    "translated-signs",  "status-extras", "canonical-stages",
    "long-names",        "shop-job-bar",  "continuous-music",
    "need-bubble-stack", "show-minimap",  "minimap-enemies",
    "world-map",         "chibi-style",   "shadows",
    "enhanced-lighting", "vivid-colors",  "hd-scenery",
    "weather",           "seasons",       "autosave"};

// In the original look the menu's box grows in for 5 frames at 20 a second and ignores the keys
// until it is in place (ui/common/original_box.lua): wait those 0.25 s before pressing anything.
void openOnOptions(Screen& screen) {
  screen.ui->setSetting("pause_menu.start_page", std::string{"options"});
  screen.ui->show("pause_menu", true);
  screen.ui->setSetting("pause_menu.start_page", std::string{});
  screen.frames(20);
}

bool sent(const std::vector<UiCommand>& commands, std::string_view name, const UiValue& value) {
  return std::ranges::any_of(commands, [&](const UiCommand& command) {
    return command.name == name && command.value == value;
  });
}

} // namespace

TEST_CASE("the options key's start page opens the pause menu on its options, and back closes it") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"pause_menu"};
    screen.skin(skin);
    openOnOptions(screen);
    CHECK(screen.ui->isVisible("pause_menu"));
    // The box opens with its page (217f77d0): a closed box would hide the options.
    CHECK_FALSE(screen.ui->elementHasClass("pause_menu", "panel", "box-closed"));
    CHECK_FALSE(screen.ui->elementHasClass("pause_menu", "page-options", "hidden"));
    CHECK(screen.ui->elementHasClass("pause_menu", "page-main", "hidden"));
    CHECK(screen.ui->focusedElementId("pause_menu") == "display-mode");
    screen.press(input::keys::LeftCtrl);
    screen.frames();
    CHECK_FALSE(screen.ui->isVisible("pause_menu"));

    // Opened as usual, it starts on its main page.
    screen.ui->show("pause_menu", true);
    screen.frames();
    CHECK(screen.ui->elementHasClass("pause_menu", "page-options", "hidden"));
  }
}

TEST_CASE("the options page lists every game option in a row of its own, and only those") {
  Screen screen{"pause_menu", "es"};
  screen.skin("modern");
  screen.ui->setSetting("free_exploration", false);
  screen.ui->setSetting("long_names", true);
  openOnOptions(screen);
  for (const std::string_view id : GameOptions) {
    CAPTURE(id);
    CHECK_FALSE(screen.ui->elementHasClass("pause_menu", id, "hidden"));
    CHECK_FALSE(screen.ui->elementText("pause_menu", std::string{id} + "-value").empty());
  }
  CHECK(screen.ui->elementText("pause_menu", "option-slot-1-label") == "Exploración libre");
  CHECK(screen.ui->elementText("pause_menu", "free-exploration-value") == "No");
  CHECK(screen.ui->elementText("pause_menu", "long-names-value") == "Sí");
  // The chibi style has three values: by default only the models from outside the original game.
  CHECK(screen.ui->elementText("pause_menu", "chibi-style-value") == "Externos");
  // The rows the list does not fill stay hidden.
  CHECK(screen.ui->elementHasClass("pause_menu", "option-slot-26", "hidden"));
  // The language and the original-looking text, as on the title.
  CHECK(screen.ui->elementText("pause_menu", "language-value") == "Español");
  CHECK_FALSE(screen.ui->elementText("pause_menu", "text-font-value").empty());

  // A row sends its setting; the menu never changes it itself.
  std::ignore = screen.ui->takeCommands();
  REQUIRE(
      screen.ui->callScript("pause_menu", "adjust_slot", std::vector<std::string>{"", "1", "1"}));
  REQUIRE(
      screen.ui->callScript("pause_menu", "adjust_slot", std::vector<std::string>{"", "18", "1"}));
  REQUIRE(screen.ui->callScript("pause_menu", "adjust",
                                std::vector<std::string>{"", "text-font", "1"}));
  const std::vector<UiCommand> commands = screen.ui->takeCommands();
  CHECK(sent(commands, "free_exploration", UiValue{true}));
  CHECK(sent(commands, "chibi_style", UiValue{std::string{"all"}}));
  CHECK(sent(commands, "text_font", UiValue{std::string{"original"}}));
}

TEST_CASE("the options page cycles the languages the packs offer") {
  Screen screen{"pause_menu"};
  openOnOptions(screen);
  std::ignore = screen.ui->takeCommands();
  REQUIRE(
      screen.ui->callScript("pause_menu", "adjust", std::vector<std::string>{"", "language", "1"}));
  const std::vector<UiCommand> commands = screen.ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands.front().name == "language");
  CHECK(std::holds_alternative<std::string>(commands.front().value));
}

TEST_CASE("an option added to the list appears on the options page without a row of its own") {
  Screen screen{"pause_menu", "en", [](assets::MemoryFileSystem& memory) {
                  std::ifstream stream{std::filesystem::path{DWE_GAME_PACK} /
                                       "ui/common/game_options.lua"};
                  std::string script{std::istreambuf_iterator<char>{stream},
                                     std::istreambuf_iterator<char>{}};
                  script += R"lua(
game_options[#game_options + 1] = {
  id = "movement", setting = "movement", label = "pause_menu.movement",
  values = { "keys", "mouse" }, texts = "pause_menu.movement", default = "keys",
}
)lua";
                  memory.addFile("packs/game/ui/common/game_options.lua", script);
                }};
  openOnOptions(screen);
  CHECK_FALSE(screen.ui->elementHasClass("pause_menu", "movement", "hidden"));
  CHECK(screen.ui->elementText("pause_menu", "movement-value") == "pause_menu.movement.keys");
  std::ignore = screen.ui->takeCommands();
  REQUIRE(
      screen.ui->callScript("pause_menu", "adjust_slot", std::vector<std::string>{"", "26", "1"}));
  CHECK(sent(screen.ui->takeCommands(), "movement", UiValue{std::string{"mouse"}}));
}

TEST_CASE("the options page cycles the shadows and switches the light and the colours") {
  Screen screen{"pause_menu", "es"};
  openOnOptions(screen);
  // Before the game publishes them: the defaults, sun shadows and both passes on.
  CHECK(screen.ui->elementText("pause_menu", "shadows-value") == "Del sol");
  CHECK(screen.ui->elementText("pause_menu", "enhanced-lighting-value") == "Sí");
  CHECK(screen.ui->elementText("pause_menu", "vivid-colors-value") == "Sí");
  std::ignore = screen.ui->takeCommands();
  REQUIRE(
      screen.ui->callScript("pause_menu", "adjust_slot", std::vector<std::string>{"", "19", "1"}));
  REQUIRE(
      screen.ui->callScript("pause_menu", "adjust_slot", std::vector<std::string>{"", "20", "1"}));
  REQUIRE(
      screen.ui->callScript("pause_menu", "adjust_slot", std::vector<std::string>{"", "21", "1"}));
  const std::vector<UiCommand> commands = screen.ui->takeCommands();
  CHECK(sent(commands, "shadows", UiValue{std::string{"round"}}));
  CHECK(sent(commands, "enhanced_lighting", UiValue{false}));
  CHECK(sent(commands, "vivid_colors", UiValue{false}));
  screen.ui->setSetting("shadows", std::string{"off"});
  CHECK(screen.ui->elementText("pause_menu", "shadows-value") == "No");
}

TEST_CASE("the options page shows and hides the minimap with either look") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"pause_menu", "es"};
    screen.skin(skin);
    // The old "as the skin decides" reads as shown.
    screen.ui->setSetting("minimap", std::string{"auto"});
    openOnOptions(screen);
    CHECK_FALSE(screen.ui->elementHasClass("pause_menu", "show-minimap", "hidden"));
    CHECK(screen.ui->elementText("pause_menu", "option-slot-15-label") == "Mostrar minimapa");
    CHECK(screen.ui->elementText("pause_menu", "show-minimap-value") == "Sí");
    std::ignore = screen.ui->takeCommands();
    REQUIRE(screen.ui->callScript("pause_menu", "adjust_slot",
                                  std::vector<std::string>{"", "15", "1"}));
    CHECK(sent(screen.ui->takeCommands(), "minimap", UiValue{std::string{"off"}}));
    // The row shows what the game publishes back.
    screen.ui->setSetting("minimap", std::string{"off"});
    CHECK(screen.ui->elementText("pause_menu", "show-minimap-value") == "No");
    REQUIRE(screen.ui->callScript("pause_menu", "adjust_slot",
                                  std::vector<std::string>{"", "15", "1"}));
    CHECK(sent(screen.ui->takeCommands(), "minimap", UiValue{std::string{"on"}}));
  }
}

TEST_CASE("the options page turns the enemies on the minimap on and off, next to the minimap") {
  Screen screen{"pause_menu", "es"};
  openOnOptions(screen);
  CHECK_FALSE(screen.ui->elementHasClass("pause_menu", "minimap-enemies", "hidden"));
  CHECK(screen.ui->elementText("pause_menu", "option-slot-16-label") == "Enemigos en el minimapa");
  // On until the game says otherwise.
  CHECK(screen.ui->elementText("pause_menu", "minimap-enemies-value") == "Sí");
  std::ignore = screen.ui->takeCommands();
  REQUIRE(
      screen.ui->callScript("pause_menu", "adjust_slot", std::vector<std::string>{"", "16", "1"}));
  CHECK(sent(screen.ui->takeCommands(), "minimap_enemies", UiValue{false}));
  screen.ui->setSetting("minimap_enemies", false);
  CHECK(screen.ui->elementText("pause_menu", "minimap-enemies-value") == "No");
}

} // namespace dwe::ui
