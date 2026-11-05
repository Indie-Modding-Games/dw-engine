#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/test_support/temp_folder.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "minimap_setting.hpp"
#include "user_settings.hpp"

namespace {

// The warnings written to the log since it was made.
class Warnings {
public:
  Warnings() {
    static const bool added = [] {
      dwe::core::addLogSink([](dwe::core::LogLevel level, std::string_view message) {
        if (level == dwe::core::LogLevel::Warning) {
          lines().emplace_back(message);
        }
      });
      return true;
    }();
    static_cast<void>(added);
    lines().clear();
  }

  [[nodiscard]] static bool any(std::string_view text) {
    return std::ranges::any_of(lines(),
                               [text](const std::string& line) { return line.contains(text); });
  }

  [[nodiscard]] static bool none() { return lines().empty(); }

private:
  static std::vector<std::string>& lines() {
    static std::vector<std::string> written;
    return written;
  }
};

} // namespace

TEST_CASE("the minimap mode keeps the old boolean setting's meaning and shows an unset one") {
  using dwe::app::minimapModeOf;
  using dwe::ui::UiValue;
  CHECK(minimapModeOf(UiValue{true}) == dwe::app::MinimapOn);
  CHECK(minimapModeOf(UiValue{false}) == dwe::app::MinimapOff);
  CHECK(minimapModeOf(UiValue{}) == dwe::app::MinimapOn);
  CHECK(minimapModeOf(UiValue{std::string{"on"}}) == dwe::app::MinimapOn);
  CHECK(minimapModeOf(UiValue{std::string{"off"}}) == dwe::app::MinimapOff);
  // The old "as the skin decides": both skins draw it now.
  CHECK(minimapModeOf(UiValue{std::string{"auto"}}) == dwe::app::MinimapOn);
  CHECK(minimapModeOf(UiValue{std::string{"sometimes"}}) == dwe::app::MinimapOn);
  CHECK(minimapModeOf(UiValue{1.0}) == dwe::app::MinimapOn);
}

TEST_CASE("the run starts on the saved minimap mode, or on when none or an unknown one is saved") {
  using dwe::app::startMinimapMode;
  const Warnings warnings;
  CHECK(startMinimapMode(std::nullopt) == dwe::app::MinimapOn);
  CHECK(startMinimapMode(std::string{"off"}) == dwe::app::MinimapOff);
  CHECK(startMinimapMode(std::string{"on"}) == dwe::app::MinimapOn);
  CHECK(startMinimapMode(std::string{"auto"}) == dwe::app::MinimapOn);
  CHECK(Warnings::none());
  CHECK(startMinimapMode(std::string{"sometimes"}) == dwe::app::MinimapOn);
  CHECK(Warnings::any("sometimes"));
}

TEST_CASE("the minimap shows only while the field runs, never in a battle or a minigame") {
  using dwe::app::minimapAllowed;
  using dwe::app::MinimapScene;
  CHECK(minimapAllowed(MinimapScene{.fieldRunning = true, .battle = false, .minigame = false}));
  // A menu, a scene, a talk, a fade or a map change stops the field's time.
  CHECK_FALSE(
      minimapAllowed(MinimapScene{.fieldRunning = false, .battle = false, .minigame = false}));
  CHECK_FALSE(
      minimapAllowed(MinimapScene{.fieldRunning = true, .battle = true, .minigame = false}));
  CHECK_FALSE(
      minimapAllowed(MinimapScene{.fieldRunning = true, .battle = false, .minigame = true}));
}

TEST_CASE("a minimap mode the player picks is saved and the next run starts on it") {
  const dwe::test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  dwe::app::UserSettings settings{.language = std::string{"es"},
                                  .textFont = std::nullopt,
                                  .uiSkin = std::nullopt,
                                  .minimap = std::nullopt};
  dwe::app::MinimapChanger changer{settings, file};
  CHECK(changer.change(dwe::ui::UiValue{std::string{"off"}}) == dwe::app::MinimapOff);
  const dwe::app::UserSettings read = dwe::app::loadUserSettings(file);
  CHECK(read.minimap == std::optional<std::string>{"off"});
  // The other choices stay as they were.
  CHECK(read.language == std::optional<std::string>{"es"});
  CHECK(dwe::app::startMinimapMode(read.minimap) == dwe::app::MinimapOff);
  // The old boolean command saves the mode it means.
  CHECK(changer.change(dwe::ui::UiValue{true}) == dwe::app::MinimapOn);
  CHECK(dwe::app::loadUserSettings(file).minimap == std::optional<std::string>{"on"});

  // A run that must not touch the user's settings changes the mode without saving it.
  dwe::app::UserSettings unsaved;
  dwe::app::MinimapChanger autotest{unsaved, std::nullopt};
  CHECK(autotest.change(dwe::ui::UiValue{std::string{"off"}}) == dwe::app::MinimapOff);
  CHECK(dwe::app::loadUserSettings(file).minimap == std::optional<std::string>{"on"});
}

namespace {

using Rect = dwe::ui::Ui::Rect;

constexpr Rect Minimap{.left = 1000.0F, .top = 20.0F, .right = 1260.0F, .bottom = 280.0F};
constexpr double Frame = 1.0 / 60.0;

// The game pack from the source tree with a test window document, whose style the test gives, and
// their UI at 1280x720, on the modern skin (the packs have no disc art).
struct HudUi {
  dwe::assets::MemoryFileSystem memory;
  std::unique_ptr<dwe::assets::VirtualFileSystem> files;
  std::unique_ptr<dwe::localization::Catalog> texts;
  std::unique_ptr<dwe::ui::Ui> ui;

  explicit HudUi(std::string_view windowStyle) {
    const std::filesystem::path pack{DWE_GAME_PACK};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{pack}) {
      if (entry.is_regular_file()) {
        std::ifstream stream{entry.path(), std::ios::binary};
        memory.addFile(
            std::filesystem::path{"packs/game"} / entry.path().lexically_relative(pack),
            std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}});
      }
    }
    memory.addFile("packs/base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
    memory.addFile("packs/tests/pack.yaml",
                   "id: tests\nversion: 0.1.0\nlayer: base\npriority: 2\n");
    memory.addFile("packs/tests/ui/window/document.rml",
                   std::string{R"(<rml><head><link type="text/rcss" href="../common/base.rcss"/>
<style>#window { position: absolute; background-color: #000080; )"} +
                       std::string{windowStyle} +
                       R"( }</style></head><body><div id="window"></div></body></rml>)");
    const auto location = [this](std::string_view root) {
      return dwe::assets::PackLocation{.fileSystem = &memory,
                                       .root = std::string{root},
                                       .origin = dwe::assets::PackOrigin::Engine};
    };
    const std::array locations{location("packs/base"), location("packs/game"),
                               location("packs/tests")};
    auto mounted = dwe::assets::VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<dwe::assets::VirtualFileSystem>(std::move(*mounted));
    auto catalog = dwe::localization::Catalog::load(*files, "en");
    REQUIRE_MESSAGE(catalog.has_value(), catalog.error().message);
    texts = std::make_unique<dwe::localization::Catalog>(std::move(*catalog));
    auto created = dwe::ui::Ui::create(dwe::ui::UiDesc{.files = files.get(),
                                                       .texts = texts.get(),
                                                       .width = 1280,
                                                       .height = 720,
                                                       .clock = {},
                                                       .scripts = {}});
    REQUIRE_MESSAGE(created.has_value(), created.error().message);
    ui = std::move(*created);
    for (const auto [name, asset] : std::array{
             std::pair{dwe::app::HudDocument, std::string_view{"ui/hud/document"}},
             std::pair{std::string_view{"window"}, std::string_view{"ui/window/document"}}}) {
      auto document = dwe::assets::AssetId::parse(asset);
      REQUIRE_MESSAGE(document.has_value(), document.error().message);
      auto loaded = ui->loadDocument(name, *document);
      REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
    }
    ui->setSetting(dwe::app::MinimapSetting, std::string{dwe::app::MinimapOn});
    ui->show(dwe::app::HudDocument);
    ui->update();
  }

  // One frame as the game runs it: the cover from the last layout, then the layout.
  void frame(dwe::app::MinimapCover& cover, double seconds = Frame) const {
    cover.update(*ui, seconds);
    ui->update();
  }

  [[nodiscard]] bool minimapDrawn() const {
    return !ui->drawnRectsOf(dwe::app::HudDocument, dwe::app::MinimapPanel).empty();
  }
};

} // namespace

TEST_CASE("the minimap hides while a window lies over it and shows a moment after it leaves") {
  dwe::app::MinimapCover cover;
  const std::array window{Rect{.left = 900.0F, .top = 100.0F, .right = 1100.0F, .bottom = 400.0F}};
  const std::array elsewhere{Rect{.left = 0.0F, .top = 400.0F, .right = 600.0F, .bottom = 700.0F}};
  CHECK_FALSE(cover.update(Minimap, elsewhere, Frame));
  CHECK(cover.update(Minimap, window, Frame));
  // Gone: it stays hidden for HoldSeconds, then shows.
  CHECK(cover.update(Minimap, elsewhere, dwe::app::MinimapCover::HoldSeconds / 2.0));
  CHECK_FALSE(cover.update(Minimap, elsewhere, dwe::app::MinimapCover::HoldSeconds / 2.0));
  // A window back within the hold starts it again.
  CHECK(cover.update(Minimap, window, Frame));
  CHECK(cover.update(Minimap, {}, Frame));
  // Touching edges are not over it, and a minimap the HUD does not lay out is never covered.
  const std::array touching{Rect{.left = 0.0F, .top = 0.0F, .right = 1000.0F, .bottom = 720.0F}};
  dwe::app::MinimapCover fresh;
  CHECK_FALSE(fresh.update(Minimap, touching, Frame));
  CHECK_FALSE(fresh.update(std::nullopt, window, Frame));
  CHECK_FALSE(fresh.update(Rect{}, window, Frame));
}

TEST_CASE("the HUD draws no minimap under a window, whatever the window") {
  // A window over the top right corner, as the pause menu at 1920x1080 or a shop's summary box.
  {
    const HudUi over{"right: 0px; top: 0px; width: 400px; height: 300px;"};
    over.ui->show("window");
    over.ui->update();
    dwe::app::MinimapCover cover;
    REQUIRE(over.minimapDrawn());
    over.frame(cover);
    CHECK(cover.covered());
    CHECK(over.ui->elementHasClass(dwe::app::HudDocument, dwe::app::MinimapPanel, "covered"));
    CHECK_FALSE(over.minimapDrawn());
    // It keeps its place while hidden, so it stays hidden as long as the window is there.
    for (int frame = 0; frame < 30; ++frame) {
      over.frame(cover);
    }
    CHECK_FALSE(over.minimapDrawn());
    // The window closes: the minimap shows once the hold is over.
    over.ui->hide("window");
    over.ui->update();
    over.frame(cover);
    CHECK_FALSE(over.minimapDrawn());
    over.frame(cover, dwe::app::MinimapCover::HoldSeconds);
    CHECK(over.minimapDrawn());
  }

  // A window elsewhere leaves it alone (one UI at a time: the first is gone).
  const HudUi aside{"left: 0px; bottom: 0px; width: 400px; height: 200px;"};
  aside.ui->show("window");
  aside.ui->update();
  dwe::app::MinimapCover untouched;
  aside.frame(untouched);
  CHECK_FALSE(untouched.covered());
  CHECK(aside.minimapDrawn());
}

TEST_CASE("the options page's minimap row hides the HUD's minimap at once and saves the choice") {
  const dwe::test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  dwe::app::UserSettings settings{.language = std::string{"es"},
                                  .textFont = std::nullopt,
                                  .uiSkin = std::nullopt,
                                  .minimap = std::nullopt};
  dwe::app::MinimapChanger changer{settings, file};
  const HudUi hud{"left: 0px; top: 0px; width: 1px; height: 1px;"};
  auto menu = dwe::assets::AssetId::parse("ui/pause_menu/document");
  REQUIRE_MESSAGE(menu.has_value(), menu.error().message);
  auto loaded = hud.ui->loadDocument("pause_menu", *menu);
  REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
  hud.ui->setSetting(dwe::app::MinimapSetting,
                     std::string{dwe::app::startMinimapMode(settings.minimap)});
  hud.ui->update();
  REQUIRE_FALSE(hud.ui->elementHasClass(dwe::app::HudDocument, dwe::app::MinimapPanel, "hidden"));

  // the pad's Select options page; its minimap row is on the Interface tab.
  hud.ui->setSetting("pause_menu.start_page", std::string{"options"});
  hud.ui->show("pause_menu", true);
  hud.ui->setSetting("pause_menu.start_page", std::string{});
  std::ignore = hud.ui->takeCommands();
  // Each press goes to the game as it does in a run (game.cpp: the minimap command): the changer
  // saves the mode and the game publishes it back.
  const auto press = [&] {
    REQUIRE(hud.ui->callScript("pause_menu", "adjust",
                               std::vector<std::string>{"", "show-minimap", "1"}));
    for (const dwe::ui::UiCommand& command : hud.ui->takeCommands()) {
      if (command.name == dwe::app::MinimapSetting) {
        hud.ui->setSetting(dwe::app::MinimapSetting, std::string{changer.change(command.value)});
      }
    }
  };
  press();
  // At once, with no map reload: the HUD's minimap is hidden and the choice is saved.
  CHECK(hud.ui->elementHasClass(dwe::app::HudDocument, dwe::app::MinimapPanel, "hidden"));
  CHECK(dwe::app::loadUserSettings(file).minimap == std::optional<std::string>{"off"});
  CHECK(dwe::app::startMinimapMode(dwe::app::loadUserSettings(file).minimap) ==
        dwe::app::MinimapOff);
  press();
  CHECK_FALSE(hud.ui->elementHasClass(dwe::app::HudDocument, dwe::app::MinimapPanel, "hidden"));
  CHECK(dwe::app::loadUserSettings(file).minimap == std::optional<std::string>{"on"});
}
