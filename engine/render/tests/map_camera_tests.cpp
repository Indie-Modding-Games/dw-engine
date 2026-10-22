#include <dwe/render/map_camera.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

using dwe::render::BackgroundPoint;
using dwe::render::MapCamera;
using dwe::render::mapCameraDesc;
using dwe::render::OriginalMapCamera;

namespace {

// Two real cameras from the extracted maps: an open town square and a small room.
const OriginalMapCamera town{
    .viewpoint = {0, -4301, -5973}, .reference = {0, 0, -256}, .projection = 1813, .tiles = {6, 5}};
const OriginalMapCamera room{
    .viewpoint = {-650, -800, -1400}, .reference = {0, 0, 0}, .projection = 686, .tiles = {5, 3}};
const OriginalMapCamera smallRoom{
    .viewpoint = {-209, -392, -769}, .reference = {68, 0, -3}, .projection = 373, .tiles = {3, 2}};

// The original's pipeline in fixed point, as the decomp does it: GsSetRefView2 builds a 4.12
// rotation looking from the doubled viewpoint to the doubled reference with no roll, the GTE
// transforms (RTPS: (TR << 12 + R * V) >> 12) and divides by SZ with the projection distance H and
// a zero offset, and map.c keeps background pixel = (sx + W * 64, sy + H * 64) with cameraX =
// clamp(W * 64 + sx(tamer) - 160, 0, W * 128 - 320) (setupMap, initializeDrawingOffsets,
// tickCameraFollowPlayer).
struct OriginalPipeline {
  std::array<std::array<std::int64_t, 3>, 3> rotation{};
  std::array<std::int64_t, 3> translation{};
  std::int64_t projection = 0;
  std::int64_t width = 0;
  std::int64_t height = 0;

  explicit OriginalPipeline(const OriginalMapCamera& camera)
      : projection(camera.projection), width(camera.tiles.x), height(camera.tiles.y) {
    const glm::dvec3 eye = glm::dvec3{camera.viewpoint} * 2.0;
    const glm::dvec3 target = glm::dvec3{camera.reference} * 2.0;
    const glm::dvec3 axisZ = glm::normalize(target - eye);
    const glm::dvec3 axisX = glm::normalize(glm::cross(glm::dvec3{0.0, 1.0, 0.0}, axisZ));
    const glm::dvec3 axisY = glm::cross(axisZ, axisX);
    const std::array axes{axisX, axisY, axisZ};
    for (std::size_t row = 0; row < 3; ++row) {
      for (std::size_t column = 0; column < 3; ++column) {
        rotation[row][column] =
            std::llround(axes[row][static_cast<glm::length_t>(column)] * 4096.0);
      }
    }
    const std::array<std::int64_t, 3> fixedEye{std::llround(eye.x), std::llround(eye.y),
                                               std::llround(eye.z)};
    for (std::size_t row = 0; row < 3; ++row) {
      std::int64_t sum = 0;
      for (std::size_t column = 0; column < 3; ++column) {
        sum += rotation[row][column] * fixedEye[column];
      }
      translation[row] = -(sum >> 12);
    }
  }

  // RTPS: view-space vector, with SZ in z.
  [[nodiscard]] std::array<std::int64_t, 3> transform(glm::ivec3 point) const {
    std::array<std::int64_t, 3> view{};
    const std::array<std::int64_t, 3> input{point.x, point.y, point.z};
    for (std::size_t row = 0; row < 3; ++row) {
      std::int64_t sum = translation[row] << 12;
      for (std::size_t column = 0; column < 3; ++column) {
        sum += rotation[row][column] * input[column];
      }
      view[row] = sum >> 12;
    }
    view[2] = std::clamp<std::int64_t>(view[2], 0, 0xFFFF);
    return view;
  }

  // The GTE's screen coordinates with offset 0, saturated like SXY.
  [[nodiscard]] glm::i64vec2 project(glm::ivec3 point) const {
    const auto view = transform(point);
    const std::int64_t depth = std::max<std::int64_t>(view[2], 1);
    const std::int64_t quotient =
        std::min<std::int64_t>(((projection << 16) + (depth / 2)) / depth, 0x1FFFF);
    return {std::clamp<std::int64_t>((quotient * view[0]) >> 16, -1024, 1023),
            std::clamp<std::int64_t>((quotient * view[1]) >> 16, -1024, 1023)};
  }

  [[nodiscard]] glm::i64vec2 backgroundPixel(glm::ivec3 point) const {
    return project(point) + glm::i64vec2{width * 64, height * 64};
  }

  // The background pixel at the top-left of the 320 x 240 screen while following the tamer.
  [[nodiscard]] glm::i64vec2 cameraCorner(glm::ivec3 tamer) const {
    const glm::i64vec2 pixel = backgroundPixel(tamer);
    return {std::clamp<std::int64_t>(pixel.x - 160, 0, (width * 128) - 320),
            std::clamp<std::int64_t>(pixel.y - 120, 0, (height * 128) - 240)};
  }

  // overworld.c: calcMapObjectOrder.
  [[nodiscard]] std::int64_t orderValue(glm::ivec3 point) const {
    const std::int64_t value = transform(point)[2] >> 4;
    if (value < 0x23) {
      return 0x23;
    }
    return value >= 0x1000 ? 0xFF6 : value;
  }
};

glm::vec3 toWorld(glm::ivec3 original) {
  return dwe::core::fromOriginalAxes(glm::vec3{original});
}

// Points on and above the ground of a map, in the original's axes (Y down), spread over its 100 x
// 100 cell grid.
std::array<glm::ivec3, 9> samplePoints(int spread) {
  return {{
      {0, 0, 0},
      {spread, 0, 0},
      {-spread, 0, spread},
      {spread / 2, -150, -spread},
      {-spread, -300, -spread / 2},
      {spread, 0, spread},
      {37, -80, 412},
      {-spread / 3, 0, -spread / 4},
      {spread / 5, -40, spread / 3},
  }};
}

void checkAgainstOriginal(const OriginalMapCamera& raw, glm::ivec3 tamer, int spread) {
  const OriginalPipeline original{raw};
  MapCamera camera{mapCameraDesc(raw)};
  camera.setViewport({320.0F, 240.0F});
  camera.follow(toWorld(tamer));
  const glm::i64vec2 corner = original.cameraCorner(tamer);
  CHECK(std::abs(camera.scroll().x - static_cast<float>(corner.x)) <= 1.0F);
  CHECK(std::abs(camera.scroll().y - static_cast<float>(corner.y)) <= 1.0F);

  for (const glm::ivec3 point : samplePoints(spread)) {
    CAPTURE(point.x);
    CAPTURE(point.y);
    CAPTURE(point.z);
    // The original works in fixed point (a 4.12 rotation, truncated translation and quotient) and
    // truncates every projection to whole pixels: its background pixel is within a pixel and a
    // quarter of ours, and a window position (a pixel minus the truncated scroll) within one and
    // three quarters.
    const glm::i64vec2 pixel = original.backgroundPixel(point);
    const auto background = camera.projectToBackground(toWorld(point));
    REQUIRE(background.has_value());
    CHECK(std::abs(background.value_or(BackgroundPoint{}).pixel.x - static_cast<float>(pixel.x)) <=
          1.25F);
    CHECK(std::abs(background.value_or(BackgroundPoint{}).pixel.y - static_cast<float>(pixel.y)) <=
          1.25F);
    const glm::i64vec2 expected = pixel - corner;
    const auto window = camera.worldToWindow(toWorld(point));
    REQUIRE(window.has_value());
    CHECK(std::abs(window.value_or(glm::vec2{}).x - static_cast<float>(expected.x)) <= 1.75F);
    CHECK(std::abs(window.value_or(glm::vec2{}).y - static_cast<float>(expected.y)) <= 1.75F);

    // At four times the original size every position scales by four.
    MapCamera large{mapCameraDesc(raw)};
    large.setViewport({1280.0F, 960.0F});
    large.follow(toWorld(tamer));
    const auto scaled = large.worldToWindow(toWorld(point));
    REQUIRE(scaled.has_value());
    CHECK(std::abs((scaled.value_or(glm::vec2{}).x / 4.0F) - static_cast<float>(expected.x)) <=
          1.75F);
    CHECK(std::abs((scaled.value_or(glm::vec2{}).y / 4.0F) - static_cast<float>(expected.y)) <=
          1.75F);

    const auto depth = static_cast<float>(original.orderValue(point));
    const float ours =
        std::clamp(std::floor(camera.viewDepth(toWorld(point)) / 16.0F), 35.0F, 4086.0F);
    CHECK(std::abs(ours - depth) <= 1.0F);
  }
}

} // namespace

TEST_CASE("the raw camera is doubled and turned into world space") {
  const auto desc = mapCameraDesc(town);
  CHECK(desc.eye.x == doctest::Approx(0.0F));
  CHECK(desc.eye.y == doctest::Approx(8602.0F));
  CHECK(desc.eye.z == doctest::Approx(11946.0F));
  CHECK(desc.target.z == doctest::Approx(512.0F));
  CHECK(desc.focalLength == doctest::Approx(1813.0F));
  CHECK(desc.backgroundSize.x == doctest::Approx(768.0F));
  CHECK(desc.backgroundSize.y == doctest::Approx(640.0F));
}

TEST_CASE("the reference point projects to the centre of the background") {
  for (const auto& raw : {town, room, smallRoom}) {
    const MapCamera camera{mapCameraDesc(raw)};
    const auto point = camera.projectToBackground(mapCameraDesc(raw).target);
    REQUIRE(point.has_value());
    CHECK(point.value_or(BackgroundPoint{}).pixel.x ==
          doctest::Approx(camera.desc().backgroundSize.x / 2.0F));
    CHECK(point.value_or(BackgroundPoint{}).pixel.y ==
          doctest::Approx(camera.desc().backgroundSize.y / 2.0F));
  }
}

TEST_CASE("projections match the original's fixed-point pipeline within a pixel") {
  SUBCASE("town, tamer in the middle") {
    checkAgainstOriginal(town, {120, 0, -300}, 2400);
  }
  SUBCASE("town, tamer at the left edge") {
    checkAgainstOriginal(town, {-3200, 0, 400}, 2400);
  }
  SUBCASE("town, tamer at the bottom right") {
    checkAgainstOriginal(town, {2800, 0, -2500}, 2400);
  }
  SUBCASE("room") {
    checkAgainstOriginal(room, {150, 0, 100}, 500);
  }
  SUBCASE("small room") {
    checkAgainstOriginal(smallRoom, {0, 0, 0}, 250);
  }
}

TEST_CASE("the matrices agree with the helpers") {
  MapCamera camera{mapCameraDesc(room)};
  camera.setViewport({1920.0F, 1080.0F});
  camera.follow(toWorld({100, 0, 50}));
  for (const glm::ivec3 point : samplePoints(500)) {
    const glm::vec3 world = toWorld(point);
    const glm::vec4 clip = camera.viewProjection() * glm::vec4{world, 1.0F};
    const glm::vec3 ndc = glm::vec3{clip} / clip.w;
    const glm::vec2 window = (glm::vec2{ndc} + 1.0F) * 0.5F * camera.viewportSize();
    const auto expected = camera.worldToWindow(world);
    REQUIRE(expected.has_value());
    CHECK(window.x == doctest::Approx(expected.value_or(glm::vec2{}).x).epsilon(1e-4));
    CHECK(window.y == doctest::Approx(expected.value_or(glm::vec2{}).y).epsilon(1e-4));
    const float depth = camera.viewDepth(world);
    CHECK(ndc.z == doctest::Approx(MapCamera::depthBufferValue(depth)).epsilon(1e-4));

    // A screen-aligned quad placed at the same background pixel and depth lands on the same spot.
    const auto background = camera.projectToBackground(world);
    REQUIRE(background.has_value());
    const glm::vec4 quad =
        camera.backgroundToClip(background.value_or(BackgroundPoint{}).pixel,
                                background.value_or(BackgroundPoint{}).viewDepth);
    CHECK(quad.x == doctest::Approx(ndc.x).epsilon(1e-4));
    CHECK(quad.y == doctest::Approx(ndc.y).epsilon(1e-4));
    CHECK(quad.z == doctest::Approx(ndc.z).epsilon(1e-4));
  }
}

TEST_CASE("depth values grow from the near clip to the far clip") {
  CHECK(MapCamera::depthBufferValue(dwe::render::MapNearClip) == doctest::Approx(0.0F));
  CHECK(MapCamera::depthBufferValue(dwe::render::MapFarClip) == doctest::Approx(1.0F));
  CHECK(MapCamera::depthBufferValue(dwe::render::mapSpriteDepth(100)) <
        MapCamera::depthBufferValue(dwe::render::mapSpriteDepth(101)));
}

TEST_CASE("any viewport shows at least the original's window, wider or taller, never stretched") {
  struct Case {
    glm::vec2 viewport;
    glm::vec2 visible;
  };
  const std::array cases{
      Case{.viewport = {320.0F, 240.0F}, .visible = {320.0F, 240.0F}},
      Case{.viewport = {1280.0F, 720.0F}, .visible = {426.667F, 240.0F}},
      Case{.viewport = {1920.0F, 1080.0F}, .visible = {426.667F, 240.0F}},
      Case{.viewport = {2560.0F, 1080.0F}, .visible = {568.889F, 240.0F}},
      Case{.viewport = {800.0F, 600.0F}, .visible = {320.0F, 240.0F}},
      Case{.viewport = {1280.0F, 800.0F}, .visible = {384.0F, 240.0F}},
      Case{.viewport = {1080.0F, 1920.0F}, .visible = {320.0F, 568.889F}},
  };
  for (const Case& test : cases) {
    MapCamera camera{mapCameraDesc(town)};
    camera.setViewport(test.viewport);
    CHECK(camera.visibleSize().x == doctest::Approx(test.visible.x).epsilon(1e-3));
    CHECK(camera.visibleSize().y == doctest::Approx(test.visible.y).epsilon(1e-3));
    // One background pixel is a square in the window: the same scale on both axes.
    const glm::vec2 unit =
        camera.backgroundToWindow({1.0F, 1.0F}) - camera.backgroundToWindow({0.0F, 0.0F});
    CHECK(unit.x == doctest::Approx(unit.y).epsilon(1e-4));
  }
}

TEST_CASE("the window follows the focus and stops at the background's edges") {
  MapCamera camera{mapCameraDesc(town)};
  camera.setViewport({1920.0F, 1080.0F});
  camera.followBackgroundPixel({384.0F, 320.0F});
  const glm::vec2 visible = camera.visibleSize();
  CHECK(camera.backgroundToWindow({384.0F, 320.0F}).x == doctest::Approx(960.0F).epsilon(0.005));
  CHECK(camera.backgroundToWindow({384.0F, 320.0F}).y == doctest::Approx(540.0F).epsilon(0.005));

  camera.followBackgroundPixel({10.0F, 5.0F});
  CHECK(camera.scroll().x == doctest::Approx(0.0F));
  CHECK(camera.scroll().y == doctest::Approx(0.0F));

  camera.followBackgroundPixel({760.0F, 639.0F});
  CHECK(camera.scroll().x == doctest::Approx(768.0F - visible.x));
  CHECK(camera.scroll().y == doctest::Approx(640.0F - visible.y));

  const auto rect = camera.backgroundRect();
  CHECK(rect.origin.x == doctest::Approx(0.0F));
  CHECK(rect.size.x == doctest::Approx(1920.0F));
  CHECK(rect.size.y == doctest::Approx(1080.0F));
}

TEST_CASE("a background narrower than the window is centred between bars") {
  MapCamera camera{mapCameraDesc(smallRoom)}; // 384 x 256 pixels
  camera.setViewport({2560.0F, 1080.0F});     // shows 568.9 x 240
  camera.followBackgroundPixel({0.0F, 0.0F});
  const auto rect = camera.backgroundRect();
  CHECK(rect.size.x == doctest::Approx(384.0F * 4.5F));
  CHECK(rect.origin.x == doctest::Approx((2560.0F - (384.0F * 4.5F)) / 2.0F));
  CHECK(rect.origin.y == doctest::Approx(0.0F));
  CHECK(rect.size.y == doctest::Approx(1080.0F));
  CHECK(camera.scroll().y == doctest::Approx(0.0F));
}

TEST_CASE("picking the ground gives back the point under the cursor") {
  for (const auto& raw : {town, room}) {
    MapCamera camera{mapCameraDesc(raw)};
    camera.setViewport({2560.0F, 1080.0F});
    camera.follow(toWorld({100, 0, -100}));
    for (const glm::ivec3 point : samplePoints(400)) {
      const glm::vec3 ground = toWorld({point.x, 0, point.z});
      const auto window = camera.worldToWindow(ground);
      REQUIRE(window.has_value());
      const auto picked = camera.pickGround(window.value_or(glm::vec2{}));
      REQUIRE(picked.has_value());
      CHECK(std::abs(picked.value_or(glm::vec3{}).x - ground.x) < 0.5F);
      CHECK(std::abs(picked.value_or(glm::vec3{}).z - ground.z) < 0.5F);
      CHECK(picked.value_or(glm::vec3{}).y == doctest::Approx(0.0F));
    }
  }
}

TEST_CASE("points behind the camera do not project") {
  const MapCamera camera{mapCameraDesc(room)};
  const glm::vec3 behind = (camera.desc().eye * 2.0F) - camera.desc().target;
  CHECK_FALSE(camera.projectToBackground(behind).has_value());
  CHECK_FALSE(camera.worldToWindow(behind).has_value());
}
