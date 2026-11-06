#include <dwe/render/ground_shadows.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/render/scene_grade.hpp>
#include <dwe/render/scene_light.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <numbers>

using dwe::render::BackgroundLight;
using dwe::render::daylightAt;
using dwe::render::LightKind;
using dwe::render::lightStrength;
using dwe::render::MapCamera;
using dwe::render::MapCameraDesc;
using dwe::render::MapLight;
using dwe::render::parseMapLights;
using dwe::render::roundShadow;
using dwe::render::RoundShadowShade;
using dwe::render::ShadowCaster;
using dwe::render::ShadowSpot;
using dwe::render::sunAt;
using dwe::render::sunShadow;
using dwe::render::TimeOfDaySchedule;
using dwe::render::windowLights;

namespace {

// The game pack's schedule (field/rules: time_of_day).
const TimeOfDaySchedule schedule{.dayStart = 6, .duskStart = 16, .nightStart = 20};

const ShadowSpot spot{.texture = 3, .texel = {64.0F, 0.0F}, .texels = {63.0F, 63.0F}};

} // namespace

TEST_CASE("the light table reads each map's lights, with defaults for what a light leaves out") {
  const auto table = parseMapLights(R"(maps:
  frzl01:
    - {pixel: [450, 285], radius: 70, color: [1.0, 0.55, 0.25], intensity: 0.8, kind: fire}
    - {pixel: [10, 20], radius: 30}
  leom01: []
)");
  REQUIRE(table);
  const auto stove = table->of("frzl01");
  REQUIRE(stove.size() == 2);
  CHECK(stove[0].pixel == glm::vec2{450.0F, 285.0F});
  CHECK(stove[0].radius == doctest::Approx(70.0F));
  CHECK(stove[0].color.g == doctest::Approx(0.55F));
  CHECK(stove[0].intensity == doctest::Approx(0.8F));
  CHECK(stove[0].kind == LightKind::Fire);
  CHECK(stove[1].kind == LightKind::Glow);
  CHECK(stove[1].color == glm::vec3{1.0F});
  CHECK(stove[1].intensity == doctest::Approx(MapLight{}.intensity));
  CHECK(table->of("leom01").empty());
  CHECK(table->of("twna99").empty());
}

TEST_CASE("the light table names the light it cannot read") {
  CHECK_FALSE(parseMapLights("maps:\n  a: [{pixel: [1, 2], radius: 3, kind: torch}]\n"));
  CHECK_FALSE(parseMapLights("maps:\n  a: [{pixel: [1, 2]}]\n"));
  CHECK_FALSE(parseMapLights("maps:\n  a: [{pixel: [1, 2], radius: 0}]\n"));
  CHECK_FALSE(parseMapLights("maps:\n  a: [{pixel: [1], radius: 3}]\n"));
  CHECK_FALSE(parseMapLights("maps:\n  a: [{pixel: [1, 2], radius: 3, size: 4}]\n"));
  CHECK_FALSE(parseMapLights("lights: {}\n"));
}

TEST_CASE(
    "the day's tint is warm at sunrise, as painted at noon, orange at dusk and blue at night") {
  const auto sunrise = daylightAt(6.0F, schedule, true);
  CHECK(sunrise.tint.r > sunrise.tint.b);
  const auto noon = daylightAt(11.0F, schedule, true);
  CHECK(noon.tint.r == doctest::Approx(1.0F));
  CHECK(noon.tint.b == doctest::Approx(1.0F));
  CHECK(noon.lamps == doctest::Approx(0.0F));
  const auto dusk = daylightAt(18.0F, schedule, true);
  CHECK(dusk.tint.r > 1.0F);
  CHECK(dusk.tint.b < 0.95F);
  const auto night = daylightAt(22.0F, schedule, true);
  CHECK(night.tint.b > 1.0F);
  CHECK(night.tint.r < 1.0F);
  CHECK(night.lamps == doctest::Approx(1.0F));
  // Past midnight it goes on from the last key of the evening.
  const auto late = daylightAt(2.0F, schedule, true);
  CHECK(late.tint.b > 1.0F);
  CHECK(late.lamps > 0.8F);
  // It turns gradually: no jump between two minutes.
  for (float hour = 0.0F; hour < 24.0F; hour += 1.0F / 60.0F) {
    const auto now = daylightAt(hour, schedule, true);
    const auto next = daylightAt(hour + (1.0F / 60.0F), schedule, true);
    CHECK(std::abs(now.tint.r - next.tint.r) < 0.01F);
    CHECK(std::abs(now.lamps - next.lamps) < 0.02F);
  }
}

TEST_CASE("maps without a time of day keep their colours and their lamps lit") {
  for (const float hour : {3.0F, 12.0F, 18.0F}) {
    const auto inside = daylightAt(hour, schedule, false);
    CHECK(inside.tint == glm::vec3{1.0F});
    CHECK(inside.lamps == doctest::Approx(1.0F));
    CHECK(sunAt(hour, schedule, false).strength == doctest::Approx(0.0F));
  }
}

TEST_CASE("the sun's shadow turns through the day, is shortest at noon and gone at night") {
  const auto morning = sunAt(8.0F, schedule, true);
  const auto noon = sunAt(13.0F, schedule, true);
  const auto evening = sunAt(18.5F, schedule, true);
  // Rising in +x, the shadows fall toward -x; setting in -x, toward +x; at noon toward -z.
  CHECK(morning.direction.x < -0.5F);
  CHECK(evening.direction.x > 0.5F);
  CHECK(noon.direction.y < -0.9F);
  CHECK(glm::length(morning.direction) == doctest::Approx(1.0F));
  CHECK(noon.length < morning.length);
  CHECK(noon.length < evening.length);
  CHECK(noon.strength == doctest::Approx(1.0F));
  // Weaker under the dusk palette, nothing before dawn and after dark.
  CHECK(evening.strength < noon.strength);
  CHECK(sunAt(6.0F, schedule, true).strength == doctest::Approx(0.0F));
  CHECK(sunAt(21.0F, schedule, true).strength == doctest::Approx(0.0F));
  CHECK(sunAt(3.0F, schedule, true).strength == doctest::Approx(0.0F));
}

TEST_CASE("a light's strength follows its kind over the day and flickers only when it burns") {
  const auto noon = daylightAt(12.0F, schedule, true);
  const auto night = daylightAt(23.0F, schedule, true);
  const MapLight lamp{.kind = LightKind::Lamp};
  CHECK(lightStrength(lamp, noon, 0.0, 0.0F) == doctest::Approx(0.0F));
  CHECK(lightStrength(lamp, night, 0.0, 0.0F) == doctest::Approx(lamp.intensity));
  CHECK(lightStrength(lamp, night, 5.3, 1.0F) == doctest::Approx(lamp.intensity));
  const MapLight fire{.intensity = 1.0F, .kind = LightKind::Fire};
  CHECK(lightStrength(fire, noon, 1.0, 0.0F) > 0.4F);
  CHECK(lightStrength(fire, night, 1.0, 0.0F) > lightStrength(fire, noon, 1.0, 0.0F));
  float lowest = 2.0F;
  float highest = 0.0F;
  for (int frame = 0; frame < 600; ++frame) {
    const float strength = lightStrength(fire, night, frame / 60.0, 0.5F);
    lowest = std::min(lowest, strength);
    highest = std::max(highest, strength);
  }
  // A gentle flicker: it moves, by less than a fifth either way.
  CHECK(highest - lowest > 0.1F);
  CHECK(lowest > 0.8F);
  CHECK(highest < 1.2F);
  // The same time gives the same strength: no randomness.
  CHECK(lightStrength(fire, night, 2.5, 0.5F) == lightStrength(fire, night, 2.5, 0.5F));
}

TEST_CASE("the round shadow is the original's: twice the radius, flat under the feet, subtracted") {
  const auto shadow = roundShadow({.feet = {300.0F, -40.0F, -700.0F}, .radius = 120.0F}, spot);
  CHECK(shadow.flat);
  CHECK(shadow.centre == glm::vec3{300.0F, -40.0F, -700.0F});
  CHECK(shadow.size == glm::vec2{240.0F});
  CHECK(shadow.blend == dwe::assets::SpriteBlend::Subtract);
  CHECK(shadow.color == glm::vec3{RoundShadowShade});
  CHECK(shadow.texture == spot.texture);
  CHECK(shadow.texel == spot.texel);
  CHECK(shadow.texels == spot.texels);
  CHECK(shadow.turn == doctest::Approx(0.0F));
}

TEST_CASE("the sun's shadow stretches from the feet the way the sun casts it") {
  const ShadowCaster caster{.feet = {0.0F, 0.0F, 0.0F}, .radius = 100.0F};
  const auto noon = sunAt(13.0F, schedule, true);
  const auto shadow = sunShadow(caster, noon, spot);
  REQUIRE(shadow);
  CHECK(shadow->flat);
  CHECK(shadow->size.x == doctest::Approx(noon.length * 100.0F));
  CHECK(shadow->size.y == doctest::Approx(150.0F));
  // Toward -z, starting a little behind the feet.
  CHECK(shadow->centre.z < 0.0F);
  CHECK(std::abs(shadow->centre.x) < 20.0F);
  CHECK(std::abs(shadow->centre.z) < shadow->size.x * 0.5F);
  CHECK(std::cos(shadow->turn) * noon.direction.x + (std::sin(shadow->turn) * noon.direction.y) ==
        doctest::Approx(1.0F));
  CHECK(shadow->color.r < RoundShadowShade);
  CHECK(shadow->color.r > 0.0F);
  // None at night.
  CHECK_FALSE(sunShadow(caster, sunAt(23.0F, schedule, true), spot));
}

TEST_CASE("the lights follow the camera's scroll and scale and leave the window when it does") {
  MapCamera camera{MapCameraDesc{.eye = {0.0F, 1000.0F, 1000.0F},
                                 .target = {0.0F, 0.0F, 0.0F},
                                 .focalLength = 300.0F,
                                 .backgroundSize = {640.0F, 480.0F}},
                   {640.0F, 480.0F}};
  camera.followBackgroundPixel({160.0F, 120.0F});
  const std::array lights{
      BackgroundLight{.pixel = {100.0F, 100.0F}, .radius = 40.0F, .strength = 0.5F},
      BackgroundLight{.pixel = {620.0F, 460.0F}, .radius = 10.0F, .strength = 0.5F},
      BackgroundLight{.pixel = {120.0F, 100.0F}, .radius = 40.0F, .strength = 0.0F}};
  const auto shown = windowLights(lights, camera);
  REQUIRE(shown.size() == 1);
  CHECK(shown[0].centre == camera.backgroundToWindow({100.0F, 100.0F}));
  CHECK(shown[0].radius == doctest::Approx(40.0F * camera.scale()));
}
