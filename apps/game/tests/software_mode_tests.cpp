#include <dwe/render/screen_target.hpp>

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "graphics_options.hpp"
#include "pause_menu_ui.hpp"
#include "software_mode.hpp"
#include "user_settings.hpp"

using dwe::app::decideQuality;
using dwe::app::FrameLimiter;
using dwe::app::GraphicsChanger;
using dwe::app::GraphicsKnob;
using dwe::app::GraphicsKnobs;
using dwe::app::GraphicsOptions;
using dwe::app::GraphicsPreset;
using dwe::app::HighestQuality;
using dwe::app::knobIndex;
using dwe::app::LowestQuality;
using dwe::app::parseNoticeReason;
using dwe::app::QualityDecision;
using dwe::app::QualityTuner;
using dwe::app::SceneryQuality;
using dwe::app::ShadowMode;
using dwe::app::SlowFrameWatch;
using dwe::app::SoftwareMode;
using dwe::app::TextureFilter;
using dwe::app::test::MenuUi;

namespace {

std::vector<double> frames(std::size_t count, double seconds) {
  return std::vector<double>(count, seconds);
}

GraphicsKnobs allKnobs() {
  return GraphicsKnobs{}.set();
}

} // namespace

TEST_CASE("software mode follows the device, but not in autotests unless asked") {
  using dwe::app::softwareModeActive;
  CHECK(softwareModeActive(SoftwareMode::Auto, true, false));
  CHECK_FALSE(softwareModeActive(SoftwareMode::Auto, false, false));
  CHECK_FALSE(softwareModeActive(SoftwareMode::Auto, true, true));
  CHECK(softwareModeActive(SoftwareMode::On, false, true));
  CHECK_FALSE(softwareModeActive(SoftwareMode::Off, true, false));
  CHECK(dwe::app::parseSoftwareMode("on") == SoftwareMode::On);
  CHECK_FALSE(dwe::app::parseSoftwareMode("maybe"));
}

TEST_CASE("lavapipe's threads come from LP_NUM_THREADS when it is a number") {
  using dwe::app::softwareThreads;
  CHECK(softwareThreads(std::nullopt, 8) == 8);
  CHECK(softwareThreads("2", 8) == 2);
  CHECK(softwareThreads("0", 8) == 8);
  CHECK(softwareThreads("four", 8) == 8);
  CHECK(softwareThreads(std::nullopt, 0) == 1);
}

TEST_CASE("fewer threads start from a cheaper quality") {
  using dwe::app::startQuality;
  CHECK(startQuality(1) == 0);
  CHECK(startQuality(2) == 0);
  CHECK(startQuality(4) == 1);
  CHECK(startQuality(8) == 2);
  CHECK(startQuality(16) == 3);
}

TEST_CASE("the preset sets only the options the player left") {
  const GraphicsOptions lowest = dwe::app::withQuality({}, LowestQuality, allKnobs());
  CHECK(lowest.renderScale == 50);
  CHECK(lowest.shadows == ShadowMode::Off);
  CHECK(lowest.textureFilter == TextureFilter::Nearest);
  CHECK(lowest.sceneryQuality == SceneryQuality::Smooth);
  CHECK_FALSE(lowest.antialiasing);
  CHECK_FALSE(lowest.enhancedLighting);
  CHECK_FALSE(lowest.vividColors);
  CHECK(lowest.frameLimit == dwe::app::SoftwareFrameLimit);

  dwe::app::UserSettings settings;
  settings.shadows = "sun";
  settings.renderScale = "100";
  const GraphicsKnobs knobs = dwe::app::unchosenKnobs(settings);
  CHECK_FALSE(knobs.test(knobIndex(GraphicsKnob::Shadows)));
  CHECK_FALSE(knobs.test(knobIndex(GraphicsKnob::RenderScale)));
  CHECK(knobs.test(knobIndex(GraphicsKnob::Antialiasing)));
  const GraphicsOptions kept =
      dwe::app::withQuality(dwe::app::startGraphicsOptions(settings), LowestQuality, knobs);
  CHECK(kept.shadows == ShadowMode::Sun);
  CHECK(kept.renderScale == 100);
  CHECK_FALSE(kept.antialiasing);
}

TEST_CASE("the quality goes down when the frames miss the budget and never back up to it") {
  constexpr double Fps = 30.0;
  const auto slow = frames(60, 0.045);
  const QualityDecision down = decideQuality(slow, Fps, 2, HighestQuality);
  CHECK(down == QualityDecision{.level = 1, .ceiling = 1});
  const auto fast = frames(60, 0.010);
  CHECK(decideQuality(fast, Fps, 1, 1) == QualityDecision{.level = 1, .ceiling = 1});
  CHECK(decideQuality(fast, Fps, 1, HighestQuality) ==
        QualityDecision{.level = 2, .ceiling = HighestQuality});
  CHECK(decideQuality(slow, Fps, LowestQuality, HighestQuality).level == LowestQuality);
  CHECK(decideQuality(fast, Fps, HighestQuality, HighestQuality).level == HighestQuality);
}

TEST_CASE("frames far over the budget drop two levels at once") {
  constexpr double Fps = 30.0;
  const auto crawling = frames(60, 0.070);
  CHECK(decideQuality(crawling, Fps, 3, HighestQuality) ==
        QualityDecision{.level = 1, .ceiling = 1});
  CHECK(decideQuality(crawling, Fps, 1, HighestQuality) ==
        QualityDecision{.level = LowestQuality, .ceiling = LowestQuality});
  CHECK(decideQuality(frames(60, 0.050), Fps, 3, HighestQuality).level == 2);
}

TEST_CASE("between the two thresholds the quality stays") {
  constexpr double Fps = 30.0;
  const double budget = 1.0 / Fps;
  CHECK(decideQuality(frames(60, budget), Fps, 2, HighestQuality).level == 2);
  CHECK(decideQuality(frames(60, budget * 0.7), Fps, 2, HighestQuality).level == 2);
  auto spiky = frames(60, budget * 0.3);
  for (std::size_t index = 0; index < 10; ++index) {
    spiky[index] = budget * 0.9;
  }
  CHECK(decideQuality(spiky, Fps, 2, HighestQuality).level == 2);
}

TEST_CASE("the tuner waits for a whole window after settling, and starts again on a load") {
  QualityTuner tuner{2, 30.0};
  constexpr double Frame = 1.0 / 20.0;
  int changes = 0;
  double seconds = 0.0;
  std::optional<int> changed;
  while (seconds < QualityTuner::SettleSeconds + QualityTuner::WindowSeconds - Frame) {
    changed = tuner.frame(Frame, Frame);
    changes += changed ? 1 : 0;
    seconds += Frame;
  }
  CHECK(changes == 0);
  std::ignore = tuner.frame(2.0, 2.0);
  for (int index = 0; index < 5; ++index) {
    CHECK_FALSE(tuner.frame(Frame, Frame));
  }
  for (int index = 0; index < 80 && !changed; ++index) {
    changed = tuner.frame(Frame, Frame);
  }
  REQUIRE(changed);
  CHECK(*changed == 1);
  CHECK(tuner.ceiling() == 1);
}

TEST_CASE("the tuner does not swing between two levels") {
  QualityTuner tuner{1, 30.0};
  std::vector<int> levels;
  for (int index = 0; index < 2000; ++index) {
    const double busy = tuner.level() >= 2 ? 0.040 : 0.015;
    if (const auto level = tuner.frame(1.0 / 30.0, busy)) {
      levels.push_back(*level);
    }
  }
  CHECK(levels == std::vector<int>{2, 1});
  CHECK(tuner.level() == 1);
}

TEST_CASE("the frame limiter holds frames to the rate and does not hurry after a stall") {
  FrameLimiter limiter;
  CHECK(limiter.wait(0.0, 30) == doctest::Approx(0.0));
  CHECK(limiter.wait(0.010, 30) == doctest::Approx((1.0 / 30.0) - 0.010));
  CHECK(limiter.wait(1.0, 30) == doctest::Approx(0.0));
  CHECK(limiter.wait(1.005, 30) == doctest::Approx((1.0 / 30.0) - 0.005));
  CHECK(limiter.wait(2.0, 0) == doctest::Approx(0.0));
}

TEST_CASE("the render scale's scene size is the window's, scaled and rounded") {
  using dwe::render::sceneTargetExtent;
  const dwe::rhi::Extent window{.width = 1280, .height = 720};
  CHECK(sceneTargetExtent(window, 1.0F) == window);
  CHECK(sceneTargetExtent(window, 0.5F) == dwe::rhi::Extent{.width = 640, .height = 360});
  CHECK(sceneTargetExtent(window, 0.75F) == dwe::rhi::Extent{.width = 960, .height = 540});
  CHECK(sceneTargetExtent(window, 0.01F) == dwe::rhi::Extent{.width = 320, .height = 180});
  CHECK(sceneTargetExtent({.width = 1, .height = 1}, 0.5F) ==
        dwe::rhi::Extent{.width = 1, .height = 1});
}

TEST_CASE("the options page shows the preset's values, and a choice stops being automatic") {
  dwe::app::UserSettings settings;
  const GraphicsKnobs knobs = dwe::app::unchosenKnobs(settings);
  GraphicsOptions start = dwe::app::withQuality({}, LowestQuality, knobs);
  start.autoQuality = true;
  GraphicsChanger changer{settings, std::nullopt, start};
  changer.setAutomatic(knobs);
  MenuUi menu;
  changer.publish(*menu.ui);
  menu.ui->setSetting("pause_menu.start_page", std::string{"options"});
  menu.ui->show("pause_menu", true);
  REQUIRE(menu.ui->callScript("pause_menu", "show_tab", std::vector<std::string>{"", "graphics"}));
  menu.ui->update();
  CHECK(menu.ui->elementText("pause_menu", "render-scale-value") == "50%");
  CHECK(menu.ui->elementText("pause_menu", "frame-limit-value") == "30 fps");
  std::ignore = menu.ui->takeCommands();
  REQUIRE(menu.ui->callScript("pause_menu", "adjust",
                              std::vector<std::string>{"", "render-scale", "1"}));
  for (const auto& command : menu.ui->takeCommands()) {
    std::ignore = changer.handle(command, *menu.ui);
  }
  CHECK(changer.options().renderScale != 50);
  CHECK(settings.renderScale.has_value());
  CHECK_FALSE(changer.automatic().test(knobIndex(GraphicsKnob::RenderScale)));
  GraphicsOptions tuned = changer.options();
  tuned.renderScale = 50;
  tuned.shadows = ShadowMode::Round;
  changer.applyAutomatic(tuned, *menu.ui);
  CHECK(changer.options().renderScale != 50);
  CHECK(changer.options().shadows == ShadowMode::Round);
  CHECK_FALSE(settings.shadows.has_value());
}

TEST_CASE("the launcher's graphics reason is read with its values") {
  const auto reason =
      parseNoticeReason("driver-too-old|gpu=NVIDIA GeForce GTX 1060|minimum=511.65|odd");
  REQUIRE(reason.has_value());
  CHECK(reason->code == "driver-too-old");
  CHECK(reason->param("gpu") == "NVIDIA GeForce GTX 1060");
  CHECK(reason->param("minimum") == "511.65");
  CHECK(reason->param("driver").empty());
  CHECK(reason->params.size() == 2);
  CHECK(parseNoticeReason("compatible-path")->params.empty());
  CHECK_FALSE(parseNoticeReason("").has_value());
  CHECK_FALSE(parseNoticeReason("Bad Code|gpu=x").has_value());
}

TEST_CASE("a weak card steps lighter only after a window of slow frames") {
  const auto run = [](double frameSeconds) {
    SlowFrameWatch watch;
    bool stepped = false;
    for (double played = 0.0; played < 9.0; played += frameSeconds) {
      stepped = watch.frame(frameSeconds) || stepped;
    }
    return stepped;
  };
  CHECK_FALSE(run(1.0 / 60.0));
  CHECK_FALSE(run(1.0 / 45.0));
  CHECK(run(1.0 / 30.0));
}

TEST_CASE("the presets go from the default look down, and the options name theirs") {
  const GraphicsOptions quality;
  CHECK(dwe::app::presetOf(quality) == GraphicsPreset::Quality);
  const GraphicsOptions balanced =
      dwe::app::withPreset(quality, GraphicsPreset::Balanced, allKnobs());
  CHECK(dwe::app::presetOf(balanced) == GraphicsPreset::Balanced);
  CHECK(balanced.renderScale == 100);
  CHECK(balanced.shadows == ShadowMode::Round);
  const GraphicsOptions performance =
      dwe::app::withPreset(quality, GraphicsPreset::Performance, allKnobs());
  CHECK(dwe::app::presetOf(performance) == GraphicsPreset::Performance);
  CHECK(performance.renderScale == 75);
  CHECK(performance.frameLimit == quality.frameLimit);
  GraphicsKnobs onlyShadows;
  onlyShadows.set(dwe::app::knobIndex(dwe::app::GraphicsKnob::Shadows));
  const GraphicsOptions mixed =
      dwe::app::withPreset(quality, GraphicsPreset::Performance, onlyShadows);
  CHECK(mixed.shadows == ShadowMode::Off);
  CHECK(mixed.renderScale == 100);
  CHECK_FALSE(dwe::app::presetOf(mixed).has_value());
  CHECK(dwe::app::lighterPreset(GraphicsPreset::Quality) == GraphicsPreset::Balanced);
  CHECK_FALSE(dwe::app::lighterPreset(GraphicsPreset::Performance).has_value());
}
