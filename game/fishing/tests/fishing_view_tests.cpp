#include <dwe/game/fishing/view.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <numbers>
#include <optional>

namespace {

using namespace dwe::game::fishing;

// The lake's view (1280x256 background, 320x240 screen) in the original's screen pixels.
constexpr ViewRange Lake{.min = {-480, -8}, .max = {480, 8}};
constexpr glm::ivec2 RodOut{480, 8};
constexpr glm::ivec2 BobberZone{40, 30};
constexpr std::int32_t PanRadius = 16;

} // namespace

TEST_CASE("fishing camera: a point inside the dead zone keeps the view") {
  CHECK_FALSE(panCameraTo(RodOut, RodOut + glm::ivec2{-40, -20}, Lake, PanRadius, BobberZone));
  // Measured: the bobber hanging at the rod pulls the view up 4 pixels once (y 8 to 4).
  const std::optional<glm::ivec2> next =
      panCameraTo(RodOut, RodOut + glm::ivec2{0, -34}, Lake, PanRadius, BobberZone);
  REQUIRE(next);
  CHECK(*next == glm::ivec2{480, 4});
}

TEST_CASE("fishing camera: far points pan at most the radius a frame, rounded down") {
  const std::optional<glm::ivec2> left =
      panCameraTo(RodOut, glm::ivec2{0, 8}, Lake, PanRadius, glm::ivec2{0});
  REQUIRE(left);
  CHECK(*left == glm::ivec2{464, 8});
  // A diagonal step: 16 at 45 degrees is 11.3 on each axis, down to 11 (-12 for negatives).
  const std::optional<glm::ivec2> diagonal =
      panCameraTo(glm::ivec2{0}, glm::ivec2{100, 100}, ViewRange{{-200, -200}, {200, 200}},
                  PanRadius, glm::ivec2{0});
  REQUIRE(diagonal);
  CHECK(*diagonal == glm::ivec2{11, 11});
  // Radius 0 jumps at once, clamped to the background.
  const std::optional<glm::ivec2> jump =
      panCameraTo(RodOut, glm::ivec2{-900, 100}, Lake, 0, glm::ivec2{0});
  REQUIRE(jump);
  CHECK(*jump == glm::ivec2{-480, 8});
}

TEST_CASE("fishing camera: a clamped target already reached keeps the view") {
  CHECK_FALSE(panCameraTo(RodOut, glm::ivec2{900, 8}, Lake, PanRadius, glm::ivec2{0}));
}

TEST_CASE("fishing camera: the d-pad moves 5 pixels a frame and stops at the edge") {
  // Measured: camera button and left for 9 frames moved the view 45 pixels.
  glm::ivec2 centre = RodOut;
  for (int frame = 0; frame < 9; ++frame) {
    const std::optional<glm::ivec2> next = panCameraWithDPad(centre, {-1, 0}, Lake, 5);
    REQUIRE(next);
    centre = *next;
  }
  CHECK(centre == glm::ivec2{435, 8});
  CHECK_FALSE(panCameraWithDPad(RodOut, {1, 1}, Lake, 5));
  CHECK_FALSE(panCameraWithDPad(RodOut, {0, 0}, Lake, 5));
}

TEST_CASE("fishing line: clipped to the rectangle in x then y") {
  std::array<glm::vec2, 2> line{glm::vec2{-50.0F, 50.0F}, glm::vec2{150.0F, 50.0F}};
  REQUIRE(clipLineToRect(line, {0.0F, 0.0F}, {100.0F, 100.0F}));
  CHECK(line[0] == glm::vec2{0.0F, 50.0F});
  CHECK(line[1] == glm::vec2{100.0F, 50.0F});

  std::array<glm::vec2, 2> slanted{glm::vec2{50.0F, -50.0F}, glm::vec2{50.0F, 150.0F}};
  REQUIRE(clipLineToRect(slanted, {0.0F, 0.0F}, {100.0F, 100.0F}));
  CHECK(slanted[0] == glm::vec2{50.0F, 0.0F});
  CHECK(slanted[1] == glm::vec2{50.0F, 100.0F});

  std::array<glm::vec2, 2> outside{glm::vec2{-50.0F, 10.0F}, glm::vec2{-10.0F, 90.0F}};
  CHECK_FALSE(clipLineToRect(outside, {0.0F, 0.0F}, {100.0F, 100.0F}));
}

TEST_CASE("fishing line: pieces of 64 pixels, none behind the camera") {
  const glm::vec2 min{-160.0F, -120.0F};
  const glm::vec2 max{160.0F, 120.0F};
  const auto pieces = fishingLine({.pixel = {0.0F, 0.0F}, .depth = 100.0F},
                                  {.pixel = {130.0F, 0.0F}, .depth = 300.0F}, min, max);
  // (130 + 64) / 64 = 3 pieces end to end.
  REQUIRE(pieces.size() == 3);
  CHECK(pieces.front()[0].pixel == glm::vec2{0.0F, 0.0F});
  CHECK(pieces.back()[1].pixel == glm::vec2{130.0F, 0.0F});
  CHECK(pieces.back()[1].depth == doctest::Approx(300.0F));
  CHECK(pieces[1][0].pixel == pieces[0][1].pixel);
  // A short line is still one piece.
  CHECK(fishingLine({.pixel = {0.0F, 0.0F}, .depth = 10.0F},
                    {.pixel = {5.0F, 5.0F}, .depth = 10.0F}, min, max)
            .size() == 1);
  CHECK(fishingLine({.pixel = {0.0F, 0.0F}, .depth = 0.0F}, {.pixel = {5.0F, 5.0F}, .depth = 10.0F},
                    min, max)
            .empty());
  CHECK(fishingLine({.pixel = {0.0F, 0.0F}, .depth = 10.0F},
                    {.pixel = {5.0F, 5.0F}, .depth = LineFarDepth}, min, max)
            .empty());
}

TEST_CASE("fishing line: the colour follows the tension") {
  CHECK(tensionColor(0, 0) == glm::u8vec3{255, 255, 255});
  CHECK(tensionColor(0, 100) == glm::u8vec3{255, 255, 255});
  CHECK(tensionColor(50, 100) == glm::u8vec3{255, 255, 0});
  // FISH_remapClamped rounds half a step toward the start: green stops at 1, not 0.
  CHECK(tensionColor(100, 100) == glm::u8vec3{255, 1, 0});
  CHECK(tensionColor(-50, 100) == glm::u8vec3{0, 255, 255});
  CHECK(tensionColor(-100, 100) == glm::u8vec3{0, 0, 255});
}

TEST_CASE("sea serpent: each piece follows the head one frame later") {
  std::array<float, SeaSerpentPieces> turns{};
  for (int frame = 1; frame <= 6; ++frame) {
    pushSeaSerpentTurn(turns, static_cast<float>(frame));
  }
  CHECK(turns == std::array<float, SeaSerpentPieces>{6.0F, 5.0F, 4.0F, 3.0F, 2.0F});

  // All turned the same way: the pieces lie on one line behind the head, 640 units long.
  std::array<float, SeaSerpentPieces> straight{};
  const auto body = seaSerpentBody(glm::vec3{0.0F}, straight);
  CHECK(body[0].position == glm::vec3{0.0F});
  float along = 0.0F;
  for (std::size_t i = 1; i < SeaSerpentPieces; ++i) {
    along += SeaSerpentPieceGaps.at(i - 1);
    CHECK(glm::length(body.at(i).position) == doctest::Approx(along));
    CHECK(body.at(i).position.y == doctest::Approx(0.0F));
  }
  CHECK(along == doctest::Approx(640.0F));
  // A turned piece hangs its own way: the second piece is behind the first along the first's turn.
  straight[0] = std::numbers::pi_v<float> / 2.0F;
  const auto bent = seaSerpentBody(glm::vec3{0.0F}, straight);
  CHECK(bent[0].yaw == doctest::Approx(std::numbers::pi_v<float> / 2.0F));
  CHECK(glm::length(bent[1].position) == doctest::Approx(200.0F));
  CHECK(glm::length(bent[2].position - bent[1].position) == doctest::Approx(175.0F));
}
