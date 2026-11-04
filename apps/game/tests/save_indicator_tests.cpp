#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "save_indicator.hpp"
#include "user_settings.hpp"

namespace dwe::app {
namespace {

using Rect = ui::Ui::Rect;

// The game pack from the source tree, and a test document with a window where the test puts it,
// in memory, and their UI at 1280x720 (a pixel a dp).
struct IndicatorUi {
  assets::MemoryFileSystem memory;
  std::unique_ptr<assets::VirtualFileSystem> files;
  std::unique_ptr<localization::Catalog> texts;
  std::unique_ptr<ui::Ui> ui;

  explicit IndicatorUi(std::string_view windowStyle) {
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
      return assets::PackLocation{
          .fileSystem = &memory, .root = std::string{root}, .origin = assets::PackOrigin::Engine};
    };
    const std::array locations{location("packs/base"), location("packs/game"),
                               location("packs/tests")};
    auto mounted = assets::VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<assets::VirtualFileSystem>(std::move(*mounted));
    auto catalog = localization::Catalog::load(*files, "en");
    REQUIRE_MESSAGE(catalog.has_value(), catalog.error().message);
    texts = std::make_unique<localization::Catalog>(std::move(*catalog));
    auto created = ui::Ui::create(ui::UiDesc{.files = files.get(),
                                             .texts = texts.get(),
                                             .width = 1280,
                                             .height = 720,
                                             .clock = {},
                                             .scripts = {}});
    REQUIRE_MESSAGE(created.has_value(), created.error().message);
    ui = std::move(*created);
    for (const auto [name, asset] : std::array{
             std::pair{SaveIndicatorDocument, std::string_view{"ui/save_indicator/document"}},
             std::pair{std::string_view{"window"}, std::string_view{"ui/window/document"}}}) {
      auto document = assets::AssetId::parse(asset);
      REQUIRE_MESSAGE(document.has_value(), document.error().message);
      auto loaded = ui->loadDocument(name, *document);
      REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
    }
    ui->show("window");
  }

  [[nodiscard]] double number(std::string_view setting) const {
    const ui::UiValue value = ui->setting(setting);
    const double* number = std::get_if<double>(&value);
    return number != nullptr ? *number : -1.0;
  }

  // One frame: the indicator, then the layout, as the game runs them.
  void frame(SaveIndicator& indicator, std::uint64_t saves, double seconds) const {
    indicator.update(saves, seconds);
    ui->update();
  }
};

bool overlaps(const Rect& a, const Rect& b) {
  return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

} // namespace

TEST_CASE("the save notice holds for a second and a half from the save, then fades out") {
  SaveNotice notice;
  CHECK_FALSE(notice.active());
  CHECK(notice.opacity() == 0.0F);
  notice.start();
  CHECK(notice.active());
  CHECK(notice.opacity() == 1.0F);
  notice.advance(1.4);
  CHECK(notice.opacity() == 1.0F);
  notice.advance(0.35);
  CHECK(notice.opacity() == doctest::Approx(0.5F));
  // A save during the fade starts it again.
  notice.start();
  CHECK(notice.opacity() == 1.0F);
  notice.advance(SaveNotice::HoldSeconds + SaveNotice::FadeSeconds);
  CHECK_FALSE(notice.active());
  CHECK(notice.opacity() == 0.0F);
  // Time going back (a paused clock) leaves it as it is.
  notice.start();
  notice.advance(-1.0);
  CHECK(notice.opacity() == 1.0F);
}

TEST_CASE("the figure trots its lane back and forth, turning round at each end") {
  using Gait = LaneRunner::Gait;
  constexpr float Half = 300.0F;
  constexpr float Quarter = std::numbers::pi_v<float> / 2.0F;
  LaneRunner runner{Half};
  CHECK(runner.x() == -Half);
  CHECK(runner.direction() == 1.0F);
  CHECK(runner.yaw() == doctest::Approx(Quarter));
  // The first lap runs: half the lane at the run's speed.
  CHECK(runner.lap() == 0);
  CHECK(runner.gait() == Gait::Run);
  runner.advance(Half / LaneRunner::RunSpeed);
  CHECK(runner.x() == doctest::Approx(0.0F));
  CHECK_FALSE(runner.turning());
  // At the right end it turns round there, through facing the viewer, onto the next lap.
  runner.advance(Half / LaneRunner::RunSpeed);
  CHECK(runner.x() == doctest::Approx(Half));
  CHECK(runner.turning());
  CHECK(runner.direction() == -1.0F);
  CHECK(runner.lap() == 1);
  CHECK(runner.gait() == Gait::Walk);
  runner.advance(LaneRunner::TurnSeconds / 2.0F);
  CHECK(runner.x() == doctest::Approx(Half));
  CHECK(runner.yaw() == doctest::Approx(0.0F).epsilon(0.01));
  runner.advance(LaneRunner::TurnSeconds / 2.0F);
  CHECK_FALSE(runner.turning());
  CHECK(runner.yaw() == doctest::Approx(-Quarter));
  // The second lap walks back left, slower.
  runner.advance(Half / LaneRunner::WalkSpeed);
  CHECK(runner.x() == doctest::Approx(0.0F));
  // One long step crosses the end and turns there too.
  runner.advance((Half / LaneRunner::WalkSpeed) + LaneRunner::TurnSeconds +
                 (Half / LaneRunner::speedOf(LaneRunner::gaitOfLap(2))));
  CHECK(std::abs(runner.x()) < 0.01F);
  CHECK(runner.direction() == 1.0F);
  CHECK(runner.lap() == 2);
  // A frame of no time, or of time going back, leaves it where it is.
  runner.advance(-1.0F);
  CHECK(std::abs(runner.x()) < 0.01F);
}

TEST_CASE("the lane's laps mostly alternate walking and running, keeping a gait now and then") {
  using Gait = LaneRunner::Gait;
  // The same laps every time: run, then the other gait each lap, now and then the same twice.
  const std::array expected{Gait::Run, Gait::Walk, Gait::Walk, Gait::Run,  Gait::Walk, Gait::Walk,
                            Gait::Run, Gait::Walk, Gait::Run,  Gait::Walk, Gait::Walk, Gait::Run};
  for (std::uint32_t lap = 0; lap < expected.size(); ++lap) {
    CAPTURE(lap);
    CHECK(LaneRunner::gaitOfLap(lap) == expected.at(lap));
  }
  // Never three laps the same in a row, and about as many of each.
  int runs = 0;
  for (std::uint32_t lap = 0; lap < 1000; ++lap) {
    const Gait gait = LaneRunner::gaitOfLap(lap);
    runs += gait == Gait::Run ? 1 : 0;
    if (lap >= 2) {
      CAPTURE(lap);
      CHECK_FALSE(
          (gait == LaneRunner::gaitOfLap(lap - 1) && gait == LaneRunner::gaitOfLap(lap - 2)));
    }
  }
  CHECK(runs > 400);
  CHECK(runs < 600);
  // The runner keeps the same laps as it goes.
  LaneRunner runner{100.0F};
  for (int frame = 0; frame < 3000; ++frame) {
    runner.advance(1.0F / 30.0F);
    REQUIRE(runner.gait() == LaneRunner::gaitOfLap(runner.lap()));
  }
  CHECK(runner.lap() > 100);
}

TEST_CASE("the indicator goes just above the windows in its way, or nowhere when none is clear") {
  const Rect corner{.left = 1000.0F, .top = 640.0F, .right = 1250.0F, .bottom = 690.0F};
  CHECK(liftClearOf(corner, {}, 6.0F, 6.0F) == 0.0F);
  // A window elsewhere leaves it in its corner.
  const std::array away{Rect{.left = 0.0F, .top = 600.0F, .right = 400.0F, .bottom = 700.0F}};
  CHECK(liftClearOf(corner, away, 6.0F, 6.0F) == 0.0F);
  // The bars in the corner: just above them, `gap` over the higher one.
  const std::array bars{Rect{.left = 1020.0F, .top = 630.0F, .right = 1250.0F, .bottom = 655.0F},
                        Rect{.left = 1020.0F, .top = 665.0F, .right = 1250.0F, .bottom = 690.0F}};
  const std::optional<float> overBars = liftClearOf(corner, bars, 6.0F, 6.0F);
  REQUIRE(overBars.has_value());
  CHECK(*overBars == doctest::Approx(690.0F - 630.0F + 6.0F));
  // A window up the whole right side leaves no place.
  const std::array side{Rect{.left = 900.0F, .top = 0.0F, .right = 1280.0F, .bottom = 720.0F}};
  CHECK_FALSE(liftClearOf(corner, side, 6.0F, 6.0F).has_value());
}

TEST_CASE("the indicator shows for each save written while the option is on, and only then") {
  IndicatorUi game{"left: 0; top: 0; width: 10dp; height: 10dp;"};
  {
    SaveIndicator indicator{*game.ui, 3, true};
    CHECK(game.ui->isVisible(SaveIndicatorDocument));
    CHECK(game.ui->setting(SaveNoticeSetting) == ui::UiValue{true});
    game.frame(indicator, 3, 0.1);
    CHECK_FALSE(indicator.shown());
    CHECK(game.number(SaveIndicatorOpacitySetting) == 0.0);

    // A save: whole at once, in its corner.
    game.frame(indicator, 4, 0.1);
    CHECK(indicator.shown());
    CHECK(game.number(SaveIndicatorOpacitySetting) == 1.0);
    CHECK(game.number(SaveIndicatorLiftSetting) == 0.0);
    CHECK(game.ui->setting(SaveIndicatorShownSetting) == ui::UiValue{true});
    CHECK(game.ui->styleProperty(SaveIndicatorDocument, "indicator", "opacity") == "1");
    const std::optional<Rect> rect = game.ui->elementRect(SaveIndicatorDocument, "indicator");
    REQUIRE(rect.has_value());
    CHECK(rect->right < 1280.0F);
    CHECK(rect->bottom < 720.0F);
    CHECK(rect->left > 640.0F);
    CHECK(rect->top > 360.0F);
    // Fading after its hold, gone after the fade.
    game.frame(indicator, 4, 1.6);
    CHECK(game.number(SaveIndicatorOpacitySetting) > 0.0);
    CHECK(game.number(SaveIndicatorOpacitySetting) < 1.0);
    game.frame(indicator, 4, 0.5);
    CHECK_FALSE(indicator.shown());
    CHECK(game.ui->setting(SaveIndicatorShownSetting) == ui::UiValue{false});

    // Turned off, a save shows nothing; turned on again, the next one does.
    indicator.setEnabled(false);
    CHECK(game.ui->setting(SaveNoticeSetting) == ui::UiValue{false});
    game.frame(indicator, 5, 0.1);
    CHECK_FALSE(indicator.shown());
    indicator.setEnabled(true);
    game.frame(indicator, 5, 0.1);
    CHECK_FALSE(indicator.shown());
    game.frame(indicator, 6, 0.1);
    CHECK(indicator.shown());
  }
  CHECK_FALSE(game.ui->isVisible(SaveIndicatorDocument));
}

TEST_CASE("a skipped autosave shows its own notice, without the figure's Saving") {
  IndicatorUi game{"left: 0; top: 0; width: 10dp; height: 10dp;"};
  SaveIndicator indicator{*game.ui, 0, true};
  CHECK(game.ui->setting(SaveIndicatorTextSetting) == ui::UiValue{std::string{SavingText}});
  // The first count only sets where it starts.
  indicator.noteSkippedAutosaves(2);
  game.frame(indicator, 0, 0.1);
  CHECK_FALSE(indicator.shown());

  indicator.noteSkippedAutosaves(3);
  game.frame(indicator, 0, 0.1);
  CHECK(indicator.shown());
  CHECK(game.ui->setting(SaveIndicatorTextSetting) == ui::UiValue{std::string{AutosaveNoSlotText}});
  // A save after it says "Saving" again.
  game.frame(indicator, 1, 0.1);
  CHECK(game.ui->setting(SaveIndicatorTextSetting) == ui::UiValue{std::string{SavingText}});

  // With the notice off, a skip shows nothing either.
  indicator.setEnabled(false);
  game.frame(indicator, 1, 3.0);
  indicator.noteSkippedAutosaves(4);
  game.frame(indicator, 1, 0.1);
  CHECK_FALSE(indicator.shown());
}

TEST_CASE("the autosave option is published and kept in the user settings") {
  IndicatorUi game{"left: 0; top: 0; width: 10dp; height: 10dp;"};
  UserSettings settings;
  CHECK(changeAutosave(ui::UiValue{false}, *game.ui, settings, std::nullopt) ==
        std::optional<bool>{false});
  CHECK(game.ui->setting(AutosaveSetting) == ui::UiValue{false});
  CHECK(settings.autosave == std::optional<bool>{false});
  CHECK(changeAutosave(ui::UiValue{true}, *game.ui, settings, std::nullopt) ==
        std::optional<bool>{true});
  CHECK(settings.autosave == std::optional<bool>{true});
  // A value that is not on or off changes nothing.
  CHECK_FALSE(changeAutosave(ui::UiValue{std::string{"x"}}, *game.ui, settings, std::nullopt));
  CHECK(settings.autosave == std::optional<bool>{true});
}

TEST_CASE("the indicator moves up clear of a window in its corner, and hides when none is clear") {
  SUBCASE("a box in the corner") {
    IndicatorUi game{"right: 0; bottom: 0; width: 600dp; height: 120dp;"};
    SaveIndicator indicator{*game.ui, 0, true};
    game.ui->update();
    game.frame(indicator, 1, 0.0);
    CHECK(indicator.shown());
    CHECK(game.number(SaveIndicatorLiftSetting) > 0.0);
    const std::optional<Rect> rect = game.ui->elementRect(SaveIndicatorDocument, "indicator");
    const std::optional<Rect> window = game.ui->elementRect("window", "window");
    REQUIRE(rect.has_value());
    REQUIRE(window.has_value());
    CHECK_FALSE(overlaps(*rect, *window));
    CHECK(rect->bottom <= window->top);
    // It stays there while the window does.
    game.frame(indicator, 1, 0.1);
    CHECK(game.ui->elementRect(SaveIndicatorDocument, "indicator") == rect);
  }
  SUBCASE("a box up the whole right side") {
    IndicatorUi game{"right: 0; top: 0; width: 600dp; height: 100%;"};
    SaveIndicator indicator{*game.ui, 0, true};
    game.ui->update();
    game.frame(indicator, 1, 0.0);
    CHECK_FALSE(indicator.shown());
    CHECK(game.number(SaveIndicatorOpacitySetting) == 0.0);
  }
}

} // namespace dwe::app
