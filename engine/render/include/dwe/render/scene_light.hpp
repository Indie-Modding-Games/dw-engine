#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>
#include <dwe/render/map_scene.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The field's enhanced light (docs/architecture/render.md, «Paso de luz y color»): engine
// improvements with no counterpart in the original, which only fades its palettes and its three
// model lights by time of day (map.c: updateTimeOfDay). Nothing here changes a rule of the game.
namespace dwe::render {

// How a light behaves over the day and the seconds.
enum class LightKind : std::uint8_t {
  // A fire, a stove: always lit, stronger at night, flickers.
  Fire,
  // A lamp, a lit window: only from dusk to dawn (always in maps without a time of day), steady.
  Lamp,
  // A glowing thing (crystals, screens): always lit, pulses slowly.
  Glow,
};

// A light source painted in a map's background, where its glow should fall. In background pixels,
// so it stays on the painted lamp whatever the window's size and the camera's scroll.
struct MapLight {
  glm::vec2 pixel{0.0F};
  // Where its light ends.
  float radius = 64.0F;
  glm::vec3 color{1.0F};
  // How much it brightens what it falls on at its centre (1 doubles it and a bit).
  float intensity = 0.5F;
  LightKind kind = LightKind::Glow;
};

// The lights of each map (`field/lights` in the game pack,
// content/schemas/field_lights.schema.json).
struct MapLightTable {
  std::map<std::string, std::vector<MapLight>, std::less<>> maps;

  // A map's lights; none for a map the table leaves out.
  [[nodiscard]] std::span<const MapLight> of(std::string_view mapId) const;
};

// Reads the light table: `maps:` with a list of lights per map id, each `{pixel: [x, y], radius,
// color: [r, g, b], intensity, kind: fire | lamp | glow}` (color, intensity and kind optional).
core::Result<MapLightTable> parseMapLights(std::string_view yaml);

// The look of an hour of the game's clock over the scene.
struct DaylightGrade {
  // Multiplies the scene: warm in the morning, neutral at noon, orange at dusk, blue at night.
  glm::vec3 tint{1.0F};
  // How lit the lamps are, 0 (by day) to 1 (at night).
  float lamps = 0.0F;
};

// The grade at `hour` (fractional, 0-24) on an outdoor map, keyed to the times of day the schedule
// starts, so the tint turns with the palettes. Maps without a time of day (`outdoor` false:
// caves, houses) keep a neutral tint and their lamps lit.
DaylightGrade daylightAt(float hour, const TimeOfDaySchedule& schedule, bool outdoor);

// Where the sun casts the characters' soft shadows at an hour.
struct SunShadow {
  // The unit ground direction (world x, z) the shadow runs from the feet.
  glm::vec2 direction{0.0F, -1.0F};
  // Its length from the feet, in the caster's radii: short at noon, long at the ends of the day.
  float length = 0.0F;
  // How dark it is, 0 (none: at night and in maps without a time of day) to 1.
  float strength = 0.0F;
};

// The sun from the start of the day (rising in world +x) to the start of the night (setting in
// -x), at its highest halfway; weaker once the dusk palette is up. Outdoor maps only.
SunShadow sunAt(float hour, const TimeOfDaySchedule& schedule, bool outdoor);

// A light's strength now: its intensity, how lit its kind is at this grade, and its flicker
// (fire) or pulse (glow) at `seconds` of real time; `seed` keeps neighbouring lights apart. A
// deterministic wave: it draws nothing from the game's random numbers.
float lightStrength(const MapLight& light, const DaylightGrade& grade, double seconds, float seed);

} // namespace dwe::render
