// The game loop's pace (game.cpp: the frame's ticks and the time the drawing moves by): whatever
// the display's rate, the logic, the game clock and the characters' clips run at the original's
// pace, and the clips never run ahead of the logic.
#include <dwe/animation/animation_player.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/core/fixed_step.hpp>
#include <dwe/ruleset/game_clock.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <random>
#include <vector>

#include "common.hpp"

namespace {

using dwe::app::MaxFrameSeconds;
using dwe::app::TickSeconds;

// One joint and a 20-tick clip that does not repeat: the player's time is the sum of what it ran.
dwe::assets::ModelData oneJointModel() {
  dwe::assets::ModelData model;
  model.nodes = {{.name = "bone_00", .parent = std::nullopt, .rest = {}}};
  model.nodeOrder = {0};
  dwe::assets::AnimationClip clip;
  clip.name = "anim_00";
  clip.id = 0;
  clip.duration = 1.0F;
  clip.ticksPerSecond = 20;
  clip.tickCount = 21;
  clip.channels.push_back({.node = 0,
                           .path = dwe::assets::ChannelPath::Translation,
                           .times = {0.0F, 1.0F},
                           .values = {{0, 0, 0, 0}, {0, 0, 20, 0}}});
  model.clips.push_back(clip);
  return model;
}

struct Paced {
  std::int64_t ticks = 0;
  // Game frames of the clock (20 a second at normal speed) with the fraction it carries.
  double clockFrames = 0.0;
  // Seconds of clip time the drawing played.
  double clipSeconds = 0.0;
  // The fraction of a tick the drawing stands past the last tick.
  double alpha = 0.0;
};

// As game.cpp: the frame's real time becomes fixed ticks, each tick advances the game clock by a
// tick, and the drawing (the clips) moves on by the time the step took in.
Paced runLoop(const std::vector<double>& frames) {
  dwe::core::FixedStep step{TickSeconds, MaxFrameSeconds};
  dwe::ruleset::GameClock clock{dwe::ruleset::ClockRules{}};
  const std::int64_t clockStart = clock.elapsedFrames();
  const dwe::assets::ModelData model = oneJointModel();
  dwe::animation::AnimationPlayer player{model};
  REQUIRE(player.play(0U));
  Paced paced;
  for (const double seconds : frames) {
    const int ticks = step.advance(seconds);
    for (int tick = 0; tick < ticks; ++tick) {
      static_cast<void>(clock.advance(TickSeconds));
    }
    paced.ticks += ticks;
    static_cast<void>(player.advance(static_cast<float>(step.takenSeconds())));
  }
  paced.clockFrames =
      static_cast<double>(clock.elapsedFrames() - clockStart) + clock.pendingFrames();
  paced.clipSeconds = player.time();
  paced.alpha = step.alpha();
  return paced;
}

std::vector<double> steadyFrames(double framesPerSecond, double seconds) {
  const auto count = static_cast<std::size_t>(std::lround(framesPerSecond * seconds));
  return std::vector<double>(count, 1.0 / framesPerSecond);
}

constexpr double Seconds = 30.0;
constexpr std::int64_t TicksIn30Seconds = 30 * 60;
// The original: 20 game frames a second, 20 a game minute (clock.c: tickGameClock), so 30 s of play
// are 600 frames, 30 game minutes.
constexpr double ClockFramesIn30Seconds = 600.0;

} // namespace

TEST_CASE("the logic, the clock and the clips run at the same pace at any display rate") {
  for (const double rate : {30.0, 60.0, 144.0, 240.0}) {
    CAPTURE(rate);
    const Paced paced = runLoop(steadyFrames(rate, Seconds));
    // A rate that does not divide a tick can leave the last one for the next frame.
    CHECK(std::llabs(paced.ticks - TicksIn30Seconds) <= 1);
    CHECK(paced.clockFrames == doctest::Approx(ClockFramesIn30Seconds).epsilon(0.001));
    CHECK(paced.clipSeconds == doctest::Approx(Seconds).epsilon(0.0001));
  }
}

TEST_CASE("a busy display's uneven frames run the same logic and clips as a steady one") {
  // Vsync off on a loaded machine: frames from half a millisecond to 40 ms.
  std::mt19937 random{7U};
  std::uniform_real_distribution<double> frameSeconds{0.0005, 0.040};
  std::vector<double> frames;
  double total = 0.0;
  while (total < Seconds) {
    frames.push_back(std::min(frameSeconds(random), Seconds - total));
    total += frames.back();
  }
  const Paced busy = runLoop(frames);
  const Paced steady = runLoop(steadyFrames(60.0, Seconds));
  CHECK(std::llabs(busy.ticks - steady.ticks) <= 1);
  CHECK(busy.clockFrames == doctest::Approx(steady.clockFrames).epsilon(0.001));
  CHECK(busy.clipSeconds == doctest::Approx(steady.clipSeconds).epsilon(0.0001));
}

TEST_CASE("after a stall the clips stand where the logic does, not ahead of it") {
  // A load or a hitch of 2 s: the logic runs only the cap's ticks, and so do the clips.
  std::vector<double> frames = steadyFrames(60.0, 1.0);
  frames.push_back(2.0);
  const std::vector<double> after = steadyFrames(144.0, 1.0);
  frames.insert(frames.end(), after.begin(), after.end());
  const Paced paced = runLoop(frames);
  const double logicSeconds = static_cast<double>(paced.ticks) * TickSeconds;
  CHECK(paced.clipSeconds == doctest::Approx(logicSeconds + (paced.alpha * TickSeconds)));
  CHECK(paced.clipSeconds == doctest::Approx(2.0 + MaxFrameSeconds).epsilon(0.001));
  CHECK(paced.clockFrames == doctest::Approx(logicSeconds * 20.0).epsilon(0.001));
}
