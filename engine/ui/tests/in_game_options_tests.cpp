// The game's options while playing (docs/modulos/apps/game.md, «Opciones en partida»): the pad's
// Select opens the pause menu on its options page. The page has four tabs (Game, Graphics, Sound,
// Interface); each lists its display rows and the options of ui/common/game_options.lua that name
// it, without a row of its own in the document. Q and E (the pad's shoulders, a click) switch tabs,
// left and right change a row's value both ways, a panel under the list says what the focused row
// does and «Reset to defaults» puts a tab's options back after a question.

#include <dwe/input/keys.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

constexpr std::string_view Menu = "pause_menu";

// The rows of each tab, in order: the display rows first, then the game's options.
struct Tab {
  std::string_view id;
  std::span<const std::string_view> rows;
};
constexpr std::array<std::string_view, 9> GameRows{
    "free-exploration", "walk-control",           "autosave",         "world-map",    "bulk-trades",
    "long-names",       "evolution-requirements", "canonical-stages", "status-extras"};
constexpr std::array<std::string_view, 10> GraphicsRows{
    "display-mode", "vsync",   "smooth-motion", "hd-scenery", "enhanced-lighting",
    "vivid-colors", "shadows", "chibi-style",   "weather",    "seasons"};
constexpr std::array<std::string_view, 3> SoundRows{"music-volume", "effects-volume",
                                                    "continuous-music"};
constexpr std::array<std::string_view, 11> InterfaceRows{
    "ui-skin",          "ui-scale",          "language",        "text-font",
    "translated-signs", "show-minimap",      "minimap-enemies", "digital-time",
    "save-notice",      "need-bubble-stack", "shop-job-bar"};
const std::array<Tab, 4> Tabs{{{.id = "game", .rows = GameRows},
                               {.id = "graphics", .rows = GraphicsRows},
                               {.id = "sound", .rows = SoundRows},
                               {.id = "interface", .rows = InterfaceRows}}};

// In the original look the menu's box grows in for 5 frames at 20 a second and ignores the keys
// until it is in place (ui/common/original_box.lua): wait those 0.25 s before pressing anything.
void openOnOptions(Screen& screen) {
  screen.ui->setSetting("pause_menu.start_page", std::string{"options"});
  screen.ui->show(Menu, true);
  screen.ui->setSetting("pause_menu.start_page", std::string{});
  screen.frames(20);
}

void showTab(Screen& screen, std::string_view tab) {
  REQUIRE(screen.ui->callScript(Menu, "show_tab", std::vector<std::string>{"", std::string{tab}}));
  screen.frames(2);
}

// A row of the shown tab, by its option, confirmed (step 1) or moved left and right.
void adjust(Screen& screen, std::string_view id, int step) {
  REQUIRE(screen.ui->callScript(
      Menu, "adjust", std::vector<std::string>{"", std::string{id}, std::to_string(step)}));
}

bool sent(const std::vector<UiCommand>& commands, std::string_view name, const UiValue& value) {
  return std::ranges::any_of(commands, [&](const UiCommand& command) {
    return command.name == name && command.value == value;
  });
}

bool sentAny(const std::vector<UiCommand>& commands, std::string_view name) {
  return std::ranges::any_of(commands,
                             [&](const UiCommand& command) { return command.name == name; });
}

// Down from the tab's first row until the row is focused.
void focusRow(Screen& screen, std::string_view id) {
  for (int step = 0; step < 16 && screen.ui->focusedElementId(Menu) != id; ++step) {
    screen.press(input::keys::Down);
  }
  REQUIRE(screen.ui->focusedElementId(Menu) == id);
}

// The pointer moved onto an element and clicked, as the platform reports it.
void click(Screen& screen, std::string_view id) {
  const auto center = screen.ui->elementCenter(Menu, id);
  REQUIRE_MESSAGE(center.has_value(), id);
  const Ui::Point at = center.value_or(Ui::Point{});
  screen.ui->handleEvent(platform::MouseMoved{.x = at.x, .y = at.y});
  screen.ui->handleEvent(
      platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = true});
  screen.ui->handleEvent(
      platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = false});
  screen.frames(2);
}

void pad(Screen& screen, platform::GamepadButton button) {
  screen.input.handle(platform::GamepadButtonChanged{.id = 1, .button = button, .down = true});
  screen.frames(1);
  screen.input.handle(platform::GamepadButtonChanged{.id = 1, .button = button, .down = false});
  screen.frames(1);
}

} // namespace

TEST_CASE("the options key's start page opens the pause menu on its options, and back closes it") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"pause_menu"};
    screen.skin(skin);
    openOnOptions(screen);
    CHECK(screen.ui->isVisible(Menu));
    // The box opens with its page (217f77d0): a closed box would hide the options.
    CHECK_FALSE(screen.ui->elementHasClass(Menu, "panel", "box-closed"));
    CHECK_FALSE(screen.ui->elementHasClass(Menu, "page-options", "hidden"));
    CHECK(screen.ui->elementHasClass(Menu, "page-main", "hidden"));
    // On its first tab, its first row.
    CHECK(screen.ui->elementHasClass(Menu, "tab-game", "selected"));
    CHECK(screen.ui->focusedElementId(Menu) == "free-exploration");
    CHECK(screen.ui->elementShown(Menu, "menu-tabs"));
    screen.press(input::keys::LeftCtrl);
    screen.frames();
    CHECK_FALSE(screen.ui->isVisible(Menu));

    // Opened as usual, it starts on its main page.
    screen.ui->show(Menu, true);
    screen.frames();
    CHECK(screen.ui->elementHasClass(Menu, "page-options", "hidden"));
    // Its tabs are hidden with the page, though they keep the box they had (the autotest asks).
    CHECK_FALSE(screen.ui->elementShown(Menu, "menu-tabs"));
    CHECK_FALSE(screen.ui->elementShown(Menu, "no-such-element"));
  }
}

TEST_CASE("Q and E do nothing on a page without tabs: the menu stays open") {
  Screen screen{"pause_menu"};
  screen.skin("modern");
  screen.ui->show(Menu, true);
  screen.frames(20);
  const std::string focused = screen.ui->focusedElementId(Menu);
  screen.press(input::keys::Q);
  screen.press(input::keys::E);
  CHECK(screen.ui->isVisible(Menu));
  CHECK(screen.ui->focusedElementId(Menu) == focused);
  CHECK(screen.ui->elementHasClass(Menu, "page-options", "hidden"));
}

TEST_CASE("each tab of the options page lists its rows, and only those, fitting the window") {
  for (const std::string_view language : {"es", "en"}) {
    for (const std::string_view skin : {"original", "modern"}) {
      CAPTURE(language);
      CAPTURE(skin);
      Screen screen{"pause_menu", language};
      screen.skin(skin);
      openOnOptions(screen);
      for (const Tab& tab : Tabs) {
        CAPTURE(tab.id);
        showTab(screen, tab.id);
        CHECK(screen.ui->elementHasClass(Menu, "tab-" + std::string{tab.id}, "selected"));
        CHECK(screen.ui->focusedElementId(Menu) == tab.rows.front());
        for (const std::string_view id : tab.rows) {
          CAPTURE(id);
          CHECK_FALSE(screen.ui->elementHasClass(Menu, id, "hidden"));
          CHECK_FALSE(screen.ui->elementText(Menu, std::string{id} + "-label").empty());
          CHECK_FALSE(screen.ui->elementText(Menu, std::string{id} + "-value").empty());
        }
        // The rows the tab does not fill stay hidden, and the other tabs' rows are not there.
        const std::string spare = "row-" + std::to_string(tab.rows.size() + 1);
        CHECK(screen.ui->elementHasClass(Menu, spare, "hidden"));
        CHECK_FALSE(screen.ui->elementShown(Menu, spare));
        CHECK(screen.ui->elementShown(Menu, tab.rows.back()));
        for (const Tab& other : Tabs) {
          if (other.id != tab.id) {
            CHECK_FALSE(screen.ui->elementCenter(Menu, other.rows.front()).has_value());
          }
        }
        // Every row, the description and the keys fit the box at 1280x720, without scrolling:
        // the actions under the list sit below its last row.
        for (const Ui::TextOverflow& overflow : screen.ui->overflowingTexts()) {
          MESSAGE(overflow.element << " '" << overflow.text << "' " << overflow.box << " by "
                                   << overflow.overflow << " dp");
        }
        CHECK(screen.ui->overflowingTexts().empty());
        const auto last = screen.ui->elementRect(Menu, tab.rows.back());
        const auto list = screen.ui->elementRect(Menu, "option-list");
        REQUIRE(last.has_value());
        REQUIRE(list.has_value());
        CHECK(last.value_or(Ui::Rect{}).bottom <= list.value_or(Ui::Rect{}).bottom + 0.5F);
      }
    }
  }
}

TEST_CASE("the options page shows each row's value and sends its setting") {
  Screen screen{"pause_menu", "es"};
  screen.skin("modern");
  screen.ui->setSetting("free_exploration", false);
  screen.ui->setSetting("long_names", true);
  openOnOptions(screen);
  CHECK(screen.ui->elementText(Menu, "free-exploration-label") == "Exploración libre");
  CHECK(screen.ui->elementText(Menu, "free-exploration-value") == "No");
  CHECK(screen.ui->elementText(Menu, "long-names-value") == "Sí");
  // A switch is a Sí/No pill, lit while on.
  CHECK(screen.ui->elementHasClass(Menu, "free-exploration", "kind-toggle"));
  CHECK_FALSE(screen.ui->elementHasClass(Menu, "free-exploration-value", "on"));
  CHECK(screen.ui->elementHasClass(Menu, "long-names-value", "on"));
  // A choice has its arrows.
  CHECK(screen.ui->elementHasClass(Menu, "walk-control", "kind-list"));
  showTab(screen, "graphics");
  // The chibi style has two values: by default the models from outside the original game.
  CHECK(screen.ui->elementText(Menu, "chibi-style-value") == "Externos");
  showTab(screen, "interface");
  // The language and the original-looking text, as on the title.
  CHECK(screen.ui->elementText(Menu, "language-value") == "Español");
  CHECK_FALSE(screen.ui->elementText(Menu, "text-font-value").empty());

  // A row sends its setting; the menu never changes it itself.
  std::ignore = screen.ui->takeCommands();
  adjust(screen, "free-exploration", 1);
  adjust(screen, "chibi-style", 1);
  adjust(screen, "text-font", 1);
  const std::vector<UiCommand> commands = screen.ui->takeCommands();
  CHECK(sent(commands, "free_exploration", UiValue{true}));
  CHECK(sent(commands, "chibi_style", UiValue{std::string{"off"}}));
  CHECK(sent(commands, "text_font", UiValue{std::string{"original"}}));
}

TEST_CASE("Q and E, the pad's shoulders and a click switch the options' tabs, wrapping round") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"pause_menu", "es"};
    screen.skin(skin);
    openOnOptions(screen);
    screen.press(input::keys::E);
    CHECK(screen.ui->elementHasClass(Menu, "tab-graphics", "selected"));
    CHECK_FALSE(screen.ui->elementHasClass(Menu, "tab-game", "selected"));
    CHECK(screen.ui->focusedElementId(Menu) == "display-mode");
    screen.press(input::keys::E);
    screen.press(input::keys::E);
    CHECK(screen.ui->focusedElementId(Menu) == "ui-skin");
    screen.press(input::keys::E);
    CHECK(screen.ui->elementHasClass(Menu, "tab-game", "selected"));
    screen.press(input::keys::Q);
    CHECK(screen.ui->elementHasClass(Menu, "tab-interface", "selected"));
    // The menu stays open and no setting changes.
    CHECK(screen.ui->isVisible(Menu));
    CHECK(screen.ui->takeCommands().empty());

    screen.input.handle(platform::GamepadConnected{
        .id = 1, .family = platform::GamepadFamily::Xbox, .name = "pad"});
    pad(screen, platform::GamepadButton::RightShoulder);
    CHECK(screen.ui->elementHasClass(Menu, "tab-game", "selected"));
    pad(screen, platform::GamepadButton::LeftShoulder);
    CHECK(screen.ui->elementHasClass(Menu, "tab-interface", "selected"));

    click(screen, "tab-sound");
    CHECK(screen.ui->elementHasClass(Menu, "tab-sound", "selected"));
    CHECK(screen.ui->focusedElementId(Menu) == "music-volume");
  }
}

TEST_CASE("the description panel says what the focused row does, tagged improvement or original") {
  Screen screen{"pause_menu", "es"};
  screen.skin("modern");
  screen.ui->setSetting("free_exploration", false);
  openOnOptions(screen);
  CHECK(screen.ui->elementText(Menu, "option-help").contains("caminos"));
  // Off, free exploration plays as the original.
  CHECK(screen.ui->elementText(Menu, "option-tag") == "Como el original");
  CHECK(screen.ui->elementHasClass(Menu, "option-tag", "original"));
  screen.ui->setSetting("free_exploration", true);
  CHECK(screen.ui->elementText(Menu, "option-tag") == "Mejora");
  CHECK(screen.ui->elementHasClass(Menu, "option-tag", "improvement"));
  screen.press(input::keys::Down);
  CHECK(screen.ui->elementText(Menu, "option-help").contains("ratón"));
  // The volumes have no counterpart in the original: always an improvement.
  showTab(screen, "sound");
  CHECK(screen.ui->elementText(Menu, "option-tag") == "Mejora");
  // The display rows change no game: no tag.
  showTab(screen, "graphics");
  CHECK(screen.ui->elementHasClass(Menu, "option-tag", "hidden"));
  CHECK_FALSE(screen.ui->elementText(Menu, "option-help").empty());
}

TEST_CASE("left and right change a row both ways; a number stops at its ends") {
  Screen screen{"pause_menu", "es"};
  screen.skin("modern");
  openOnOptions(screen);
  showTab(screen, "graphics");
  focusRow(screen, "shadows");
  std::ignore = screen.ui->takeCommands();
  // The sun's shadows by default: right goes on to round, left back round to none.
  screen.press(input::keys::Right);
  CHECK(sent(screen.ui->takeCommands(), "shadows", UiValue{std::string{"round"}}));
  screen.press(input::keys::Left);
  CHECK(sent(screen.ui->takeCommands(), "shadows", UiValue{std::string{"off"}}));
  // A switch flips either way.
  screen.press(input::keys::Up);
  screen.press(input::keys::Left);
  CHECK(sent(screen.ui->takeCommands(), "vivid_colors", UiValue{false}));

  showTab(screen, "sound");
  screen.ui->setSetting("music_volume", 10.0);
  CHECK(screen.ui->elementHasClass(Menu, "music-volume", "kind-range"));
  CHECK(screen.ui->elementHasClass(Menu, "music-volume-more", "at-end"));
  screen.press(input::keys::Right);
  CHECK_FALSE(sentAny(screen.ui->takeCommands(), "music_volume"));
  screen.press(input::keys::Left);
  CHECK(sent(screen.ui->takeCommands(), "music_volume", UiValue{9.0}));
  // Confirming (or clicking the row) goes up and from 10 starts again from 0.
  screen.press(input::keys::Space);
  CHECK(sent(screen.ui->takeCommands(), "music_volume", UiValue{0.0}));
  // The small arrows step one way each and stop at the ends too.
  screen.ui->setSetting("music_volume", 4.0);
  screen.frames(2);
  click(screen, "music-volume-more");
  click(screen, "music-volume-less");
  std::vector<UiCommand> commands = screen.ui->takeCommands();
  CHECK(sent(commands, "music_volume", UiValue{5.0}));
  CHECK(sent(commands, "music_volume", UiValue{3.0}));
  screen.ui->setSetting("music_volume", 0.0);
  screen.frames(2);
  CHECK(screen.ui->elementHasClass(Menu, "music-volume-less", "at-end"));
  click(screen, "music-volume-less");
  CHECK_FALSE(sentAny(screen.ui->takeCommands(), "music_volume"));
}

TEST_CASE("down goes through a tab's rows, Reset, Back and the game's actions, and round") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"pause_menu", "es"};
    screen.skin(skin);
    openOnOptions(screen);
    for (const Tab& tab : Tabs) {
      CAPTURE(tab.id);
      showTab(screen, tab.id);
      for (std::size_t row = 1; row < tab.rows.size(); ++row) {
        screen.press(input::keys::Down);
      }
      CHECK(screen.ui->focusedElementId(Menu) == tab.rows.back());
      screen.press(input::keys::Down);
      CHECK(screen.ui->focusedElementId(Menu) == "options-reset");
      screen.press(input::keys::Down);
      CHECK(screen.ui->focusedElementId(Menu) == "options-back");
      screen.press(input::keys::Down);
      CHECK(screen.ui->focusedElementId(Menu) == "options-save");
      // Left and right step through the game's actions, stopping at their ends.
      screen.press(input::keys::Left);
      CHECK(screen.ui->focusedElementId(Menu) == "options-save");
      screen.press(input::keys::Right);
      CHECK(screen.ui->focusedElementId(Menu) == "options-to-title");
      screen.press(input::keys::Right);
      CHECK(screen.ui->focusedElementId(Menu) == "options-quit");
      screen.press(input::keys::Right);
      CHECK(screen.ui->focusedElementId(Menu) == "options-quit");
      CHECK(screen.ui->elementText(Menu, "option-help").contains("guardar"));
      screen.press(input::keys::Down);
      CHECK(screen.ui->focusedElementId(Menu) == tab.rows.front());
      // And back up: Save, Back, Reset, then the tab's last row.
      screen.press(input::keys::Up);
      CHECK(screen.ui->focusedElementId(Menu) == "options-save");
      screen.press(input::keys::Up);
      CHECK(screen.ui->focusedElementId(Menu) == "options-back");
      screen.press(input::keys::Up);
      CHECK(screen.ui->focusedElementId(Menu) == "options-reset");
      screen.press(input::keys::Up);
      CHECK(screen.ui->focusedElementId(Menu) == tab.rows.back());
    }
  }
}

TEST_CASE("Reset to defaults asks first and puts back only the shown tab's options") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"pause_menu", "es"};
    screen.skin(skin);
    screen.ui->setSetting("shadows", std::string{"off"});
    screen.ui->setSetting("enhanced_lighting", false);
    screen.ui->setSetting("vsync", false);
    screen.ui->setSetting("fullscreen", true);
    screen.ui->setSetting("music_volume", 3.0);
    openOnOptions(screen);
    screen.press(input::keys::E);
    // Up from the first row reaches the game's actions over the tabs, and on round to the actions
    // under the list.
    screen.press(input::keys::Up);
    CHECK(screen.ui->focusedElementId(Menu) == "options-save");
    screen.press(input::keys::Up);
    CHECK(screen.ui->focusedElementId(Menu) == "options-back");
    screen.press(input::keys::Up);
    CHECK(screen.ui->focusedElementId(Menu) == "options-reset");
    CHECK(screen.ui->elementText(Menu, "option-help").contains("Pregunta"));
    std::ignore = screen.ui->takeCommands();
    screen.press(input::keys::Space);
    CHECK_FALSE(screen.ui->elementHasClass(Menu, "reset-confirm", "hidden"));
    CHECK(screen.ui->elementText(Menu, "reset-question").contains("Gráficos"));
    CHECK(screen.ui->focusedElementId(Menu) == "reset-no");
    // While it asks, the tabs and the rows wait.
    screen.press(input::keys::E);
    CHECK(screen.ui->elementHasClass(Menu, "tab-graphics", "selected"));
    // Cancel (Ctrl) answers no: nothing is sent and the menu stays.
    screen.press(input::keys::LeftCtrl);
    CHECK(screen.ui->elementHasClass(Menu, "reset-confirm", "hidden"));
    CHECK(screen.ui->isVisible(Menu));
    CHECK(screen.ui->focusedElementId(Menu) == "options-reset");
    CHECK(screen.ui->takeCommands().empty());

    screen.press(input::keys::Space);
    screen.press(input::keys::Left);
    CHECK(screen.ui->focusedElementId(Menu) == "reset-yes");
    screen.press(input::keys::Space);
    CHECK(screen.ui->elementHasClass(Menu, "reset-confirm", "hidden"));
    const std::vector<UiCommand> commands = screen.ui->takeCommands();
    CHECK(sent(commands, "shadows", UiValue{std::string{"sun"}}));
    CHECK(sent(commands, "enhanced_lighting", UiValue{true}));
    CHECK(sent(commands, "vsync", UiValue{true}));
    // The window mode is the player's own choice, and the other tabs are left alone.
    CHECK_FALSE(sentAny(commands, "fullscreen"));
    CHECK_FALSE(sentAny(commands, "music_volume"));
    // What already has its default is not sent again.
    CHECK_FALSE(sentAny(commands, "vivid_colors"));
  }
}

TEST_CASE("an option added to the list appears on its tab without a row of its own") {
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
  // Without a tab it joins the Game tab, after its rows.
  CHECK_FALSE(screen.ui->elementHasClass(Menu, "movement", "hidden"));
  CHECK(screen.ui->elementText(Menu, "movement-value") == "pause_menu.movement.keys");
  std::ignore = screen.ui->takeCommands();
  adjust(screen, "movement", 1);
  CHECK(sent(screen.ui->takeCommands(), "movement", UiValue{std::string{"mouse"}}));
}

TEST_CASE("the options page cycles the shadows and switches the light and the colours") {
  Screen screen{"pause_menu", "es"};
  openOnOptions(screen);
  showTab(screen, "graphics");
  // Before the game publishes them: the defaults, sun shadows and both passes on.
  CHECK(screen.ui->elementText(Menu, "shadows-value") == "Del sol");
  CHECK(screen.ui->elementText(Menu, "enhanced-lighting-value") == "Sí");
  CHECK(screen.ui->elementText(Menu, "vivid-colors-value") == "Sí");
  std::ignore = screen.ui->takeCommands();
  adjust(screen, "shadows", 1);
  adjust(screen, "enhanced-lighting", 1);
  adjust(screen, "vivid-colors", 1);
  const std::vector<UiCommand> commands = screen.ui->takeCommands();
  CHECK(sent(commands, "shadows", UiValue{std::string{"round"}}));
  CHECK(sent(commands, "enhanced_lighting", UiValue{false}));
  CHECK(sent(commands, "vivid_colors", UiValue{false}));
  screen.ui->setSetting("shadows", std::string{"off"});
  CHECK(screen.ui->elementText(Menu, "shadows-value") == "No");
}

TEST_CASE("the options page shows and hides the minimap with either look") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"pause_menu", "es"};
    screen.skin(skin);
    // The old "as the skin decides" reads as shown.
    screen.ui->setSetting("minimap", std::string{"auto"});
    openOnOptions(screen);
    showTab(screen, "interface");
    CHECK_FALSE(screen.ui->elementHasClass(Menu, "show-minimap", "hidden"));
    CHECK(screen.ui->elementText(Menu, "show-minimap-label") == "Mostrar minimapa");
    CHECK(screen.ui->elementText(Menu, "show-minimap-value") == "Sí");
    std::ignore = screen.ui->takeCommands();
    adjust(screen, "show-minimap", 1);
    CHECK(sent(screen.ui->takeCommands(), "minimap", UiValue{std::string{"off"}}));
    // The row shows what the game publishes back.
    screen.ui->setSetting("minimap", std::string{"off"});
    CHECK(screen.ui->elementText(Menu, "show-minimap-value") == "No");
    adjust(screen, "show-minimap", 1);
    CHECK(sent(screen.ui->takeCommands(), "minimap", UiValue{std::string{"on"}}));
  }
}

TEST_CASE("the options page turns the enemies on the minimap on and off, next to the minimap") {
  Screen screen{"pause_menu", "es"};
  openOnOptions(screen);
  showTab(screen, "interface");
  CHECK_FALSE(screen.ui->elementHasClass(Menu, "minimap-enemies", "hidden"));
  CHECK(screen.ui->elementText(Menu, "minimap-enemies-label") == "Enemigos en el minimapa");
  // On until the game says otherwise.
  CHECK(screen.ui->elementText(Menu, "minimap-enemies-value") == "Sí");
  std::ignore = screen.ui->takeCommands();
  adjust(screen, "minimap-enemies", 1);
  CHECK(sent(screen.ui->takeCommands(), "minimap_enemies", UiValue{false}));
  screen.ui->setSetting("minimap_enemies", false);
  CHECK(screen.ui->elementText(Menu, "minimap-enemies-value") == "No");
}

TEST_CASE("the options page steps the music's and the effects' volumes from 0 to 10") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"pause_menu", "es"};
    screen.skin(skin);
    openOnOptions(screen);
    showTab(screen, "sound");
    CHECK(screen.ui->elementText(Menu, "music-volume-label") == "Volumen de música");
    CHECK(screen.ui->elementText(Menu, "effects-volume-label") == "Volumen de efectos");
    // The original's level until the game publishes another.
    CHECK(screen.ui->elementText(Menu, "music-volume-value") == "10");
    CHECK(screen.ui->elementText(Menu, "effects-volume-value") == "10");
    // The number has its own cell between the bar and the right arrow, "10" included.
    const auto number = screen.ui->elementRect(Menu, "effects-volume-value");
    const auto bar = screen.ui->elementRect(Menu, "effects-volume-bar");
    const auto more = screen.ui->elementRect(Menu, "effects-volume-more");
    REQUIRE(number.has_value());
    REQUIRE(bar.has_value());
    REQUIRE(more.has_value());
    CHECK(bar.value_or(Ui::Rect{}).right <= number.value_or(Ui::Rect{}).left + 0.5F);
    CHECK(number.value_or(Ui::Rect{}).right <= more.value_or(Ui::Rect{}).left + 0.5F);
    screen.ui->setSetting("music_volume", 7.0);
    CHECK(screen.ui->elementText(Menu, "music-volume-value") == "7");

    std::ignore = screen.ui->takeCommands();
    adjust(screen, "music-volume", -1);
    adjust(screen, "music-volume", 1);
    // Confirming from the highest starts again from silence, so one button reaches every step.
    adjust(screen, "effects-volume", 1);
    std::vector<UiCommand> commands = screen.ui->takeCommands();
    CHECK(sent(commands, "music_volume", UiValue{6.0}));
    CHECK(sent(commands, "music_volume", UiValue{8.0}));
    CHECK(sent(commands, "effects_volume", UiValue{0.0}));

    // Down stops at silence: it never jumps to the loudest.
    screen.ui->setSetting("effects_volume", 0.0);
    CHECK(screen.ui->elementText(Menu, "effects-volume-value") == "0");
    adjust(screen, "effects-volume", -1);
    commands = screen.ui->takeCommands();
    CHECK_FALSE(sentAny(commands, "effects_volume"));
  }
}

} // namespace dwe::ui
