#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "ending_effects.hpp"

using dwe::app::EndingEffects;
using dwe::app::EndingSound;
using dwe::app::EndingSparkPlacer;
using dwe::app::OriginalPoint;

namespace {

constexpr std::int32_t Bones = 17;

// Every bone sits at (bone * 100, 0, 0); the spark's offset is added as given.
const EndingSparkPlacer Placer = [](std::int32_t bone, OriginalPoint offset,
                                    std::int32_t /*scale*/) -> std::optional<OriginalPoint> {
  return OriginalPoint{bone * 100, 0, 0} + offset;
};

void runTo(EndingEffects& effects, std::int32_t frame) {
  while (effects.frame() >= 0 && effects.frame() < frame) {
    effects.tickFrame(Placer);
  }
}

} // namespace

TEST_CASE("the ending fades the player out over 60 frames while a spark gathers each frame") {
  EndingEffects effects{Bones, 1};
  CHECK(effects.fade() == doctest::Approx(1.0));
  effects.start();
  const std::vector<EndingSound> started = effects.takeSounds();
  REQUIRE(started.size() == 1);
  CHECK(started[0].kind == EndingSound::Kind::Play);
  CHECK(started[0].sound == 0);
  CHECK(effects.fade() == doctest::Approx(1.0));

  runTo(effects, 30);
  CHECK(effects.phase() == 0);
  CHECK(effects.fade() == doctest::Approx(0.5));
  CHECK(effects.sparks().size() == 30);
  // Frame f puts its spark on bone f % 16 + 1: never the root.
  CHECK(effects.sparks()[0].bone == 2);
  CHECK(effects.sparks()[15].bone == 1);
  for (const auto& spark : effects.sparks()) {
    CHECK(spark.bone >= 1);
    CHECK(spark.bone < Bones);
    CHECK(spark.offset.x >= -70);
    CHECK(spark.offset.x < 70);
    CHECK(spark.offset.y >= -40);
    CHECK(spark.offset.z < 40);
  }

  runTo(effects, 60);
  CHECK(effects.phase() == 1);
  CHECK(effects.fade() == doctest::Approx(0.0));
  CHECK(effects.scale() == 0x1000);
}

TEST_CASE("the ending shrinks the player to frame 78, swells it to 80 and lets the sparks rise") {
  EndingEffects effects{Bones, 7};
  effects.start();
  (void)effects.takeSounds();
  runTo(effects, 69);
  CHECK(effects.scale() == 0x1000 + ((0x999 - 0x1000) * 9 / 18));
  runTo(effects, 78);
  CHECK(effects.scale() == 0x999);
  runTo(effects, 79);
  CHECK(effects.scale() == 0x999 + ((0x1800 - 0x999) / 2));
  CHECK(!effects.released());
  CHECK(effects.takeSounds().empty());

  runTo(effects, 80);
  CHECK(effects.scale() == 0x1800);
  CHECK(effects.released());
  CHECK(effects.phase() == 2);
  REQUIRE(effects.sparks().size() == 80);
  const std::vector<EndingSound> released = effects.takeSounds();
  REQUIRE(released.size() == 1);
  CHECK(released[0].sound == 1);
  // A released spark keeps its world point: its bone's, plus its offset at 1.5 times.
  const OriginalPoint first = effects.sparks()[0].offset;
  CHECK(first.x >= 200 - 105);
  CHECK(first.x <= 200 + 105);

  const std::vector<dwe::app::FieldGlow> before = effects.glows(Placer);
  REQUIRE(before.size() == 80);
  CHECK(before[0].position == first);
  CHECK(before[0].size >= 15);
  CHECK(before[0].size < 30);
  CHECK(before[0].color.r >= 60);
  CHECK(before[0].color.b < 130);

  // 450 up on frame 81, 20 less every frame, until the speed reaches 0 after frame 103.
  runTo(effects, 81);
  CHECK(effects.rise() == -450);
  CHECK(effects.glows(Placer)[0].position.y == first.y - 450);
  runTo(effects, 103);
  CHECK(effects.phase() == 3);
  CHECK(effects.rise() == -5290);
  CHECK(!effects.over());

  runTo(effects, 104);
  CHECK(effects.over());
  CHECK(effects.sparks().empty());
  CHECK(effects.glows(Placer).empty());
  const std::vector<EndingSound> stopped = effects.takeSounds();
  REQUIRE(stopped.size() == 1);
  CHECK(stopped[0].kind == EndingSound::Kind::Stop);
}

TEST_CASE("an ending spark whose bone is not drawn is left out of the frame") {
  EndingEffects effects{Bones, 3};
  effects.start();
  effects.tickFrame(Placer);
  const EndingSparkPlacer nowhere = [](std::int32_t, OriginalPoint,
                                       std::int32_t) -> std::optional<OriginalPoint> {
    return std::nullopt;
  };
  CHECK(effects.glows(nowhere).empty());
  CHECK(effects.glows(Placer).size() == 1);
}
