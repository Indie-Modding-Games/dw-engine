#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/render/scene_light.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

namespace dwe::render {

namespace {

constexpr float HoursPerDay = 24.0F;

// A point of the day's grade, at an hour.
struct DaylightKey {
  float hour = 0.0F;
  glm::vec3 tint{1.0F};
  float lamps = 0.0F;
};

float wrapHour(float hour) {
  const float wrapped = std::fmod(hour, HoursPerDay);
  return wrapped < 0.0F ? wrapped + HoursPerDay : wrapped;
}

// The day's keys for a schedule, in order of the hour. Tuned against the three palettes: the tint
// only leans the way the painted light already goes, so it never fights it.
std::array<DaylightKey, 9> daylightKeys(const TimeOfDaySchedule& schedule) {
  const auto day = static_cast<float>(schedule.dayStart);
  const auto dusk = static_cast<float>(schedule.duskStart);
  const auto night = static_cast<float>(schedule.nightStart);
  std::array<DaylightKey, 9> keys{{
      // Before dawn: the night's blue going grey.
      {.hour = day - 1.5F, .tint = {0.93F, 0.96F, 1.05F}, .lamps = 0.8F},
      // Sunrise: warm gold.
      {.hour = day, .tint = {1.06F, 0.99F, 0.89F}, .lamps = 0.3F},
      {.hour = day + 2.5F, .tint = {1.02F, 1.0F, 0.97F}, .lamps = 0.0F},
      // Noon: as painted.
      {.hour = (day + dusk) * 0.5F, .tint = {1.0F, 1.0F, 1.0F}, .lamps = 0.0F},
      {.hour = dusk - 1.0F, .tint = {1.02F, 1.0F, 0.97F}, .lamps = 0.0F},
      // Dusk: orange.
      {.hour = dusk, .tint = {1.06F, 0.97F, 0.88F}, .lamps = 0.2F},
      {.hour = night - 0.5F, .tint = {1.07F, 0.94F, 0.87F}, .lamps = 0.7F},
      // Night: blue.
      {.hour = night, .tint = {0.93F, 0.97F, 1.08F}, .lamps = 1.0F},
      {.hour = night + 3.0F, .tint = {0.91F, 0.96F, 1.1F}, .lamps = 1.0F},
  }};
  for (DaylightKey& key : keys) {
    key.hour = wrapHour(key.hour);
  }
  std::ranges::sort(keys, {}, &DaylightKey::hour);
  return keys;
}

float smoothRamp(float from, float to, float value) {
  const float t = std::clamp((value - from) / (to - from), 0.0F, 1.0F);
  return t * t * (3.0F - (2.0F * t));
}

LightKind kindNamed(assets::YamlReader& reader, const core::YamlNode& node) {
  const std::string name = reader.text(node);
  if (name == "fire") {
    return LightKind::Fire;
  }
  if (name == "lamp") {
    return LightKind::Lamp;
  }
  if (name != "glow") {
    reader.fail(node, "the kind is fire, lamp or glow");
  }
  return LightKind::Glow;
}

} // namespace

std::span<const MapLight> MapLightTable::of(std::string_view mapId) const {
  const auto found = maps.find(mapId);
  return found != maps.end() ? std::span<const MapLight>{found->second}
                             : std::span<const MapLight>{};
}

core::Result<MapLightTable> parseMapLights(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  assets::YamlReader reader;
  MapLightTable table;
  if (reader.expectKeys(*tree, "the light table", {"maps"})) {
    const core::YamlNode& maps = reader.field(*tree, "maps");
    if (!maps.isMapping()) {
      reader.fail(maps, "maps is a mapping of map ids to lists of lights");
    }
    for (const core::YamlEntry& entry :
         maps.isMapping() ? maps.entries() : std::span<const core::YamlEntry>{}) {
      std::vector<MapLight>& lights = table.maps[entry.key];
      for (const core::YamlNode& node : reader.sequence(entry.value, "a map's lights")) {
        if (!reader.expectKeys(node, "a light", {"pixel", "radius"},
                               {"color", "intensity", "kind"})) {
          break;
        }
        MapLight light;
        const auto pixel = reader.sequence(reader.field(node, "pixel"), "pixel", 2);
        if (pixel.size() == 2) {
          light.pixel = {reader.number(pixel[0]), reader.number(pixel[1])};
        }
        light.radius = reader.number(reader.field(node, "radius"));
        if (light.radius <= 0.0F) {
          reader.fail(node, "the radius is above 0");
        }
        if (const core::YamlNode* color = node.find("color")) {
          const auto channels = reader.sequence(*color, "color", 3);
          if (channels.size() == 3) {
            light.color = {reader.number(channels[0]), reader.number(channels[1]),
                           reader.number(channels[2])};
          }
        }
        if (const core::YamlNode* intensity = node.find("intensity")) {
          light.intensity = reader.number(*intensity);
        }
        if (const core::YamlNode* kind = node.find("kind")) {
          light.kind = kindNamed(reader, *kind);
        }
        lights.push_back(light);
      }
    }
  }
  return reader.finish(std::move(table), "field/lights");
}

DaylightGrade daylightAt(float hour, const TimeOfDaySchedule& schedule, bool outdoor) {
  if (!outdoor) {
    return {.tint = glm::vec3{1.0F}, .lamps = 1.0F};
  }
  const auto keys = daylightKeys(schedule);
  const float now = wrapHour(hour);
  // The key at or before now, wrapping through midnight, and the one after it.
  std::size_t before = keys.size() - 1;
  for (std::size_t index = 0; index < keys.size(); ++index) {
    if (keys[index].hour <= now) {
      before = index;
    }
  }
  const DaylightKey& from = keys[before];
  const DaylightKey& to = keys[(before + 1) % keys.size()];
  const float span = wrapHour(to.hour - from.hour);
  const float t = span > 0.0F ? wrapHour(now - from.hour) / span : 0.0F;
  return {.tint = glm::mix(from.tint, to.tint, t), .lamps = std::lerp(from.lamps, to.lamps, t)};
}

SunShadow sunAt(float hour, const TimeOfDaySchedule& schedule, bool outdoor) {
  const auto day = static_cast<float>(schedule.dayStart);
  const auto night = static_cast<float>(schedule.nightStart);
  const float now = wrapHour(hour);
  if (!outdoor || night <= day || now < day || now >= night) {
    return {};
  }
  const float t = (now - day) / (night - day);
  const float angle = std::numbers::pi_v<float> * t;
  const float height = std::sin(angle);
  // The sun crosses the sky on the world's +z side, so at noon the shadows fall toward -z.
  const glm::vec2 sun{std::cos(angle), 0.6F * height};
  constexpr float NoonLength = 1.3F;
  constexpr float LowSunLength = 2.4F;
  constexpr float DuskStrength = 0.65F;
  const float dusk = now >= static_cast<float>(schedule.duskStart) ? DuskStrength : 1.0F;
  return {.direction = -glm::normalize(sun),
          .length = NoonLength + (LowSunLength * (1.0F - height)),
          .strength = smoothRamp(0.0F, 0.06F, t) * (1.0F - smoothRamp(0.94F, 1.0F, t)) * dusk};
}

float lightStrength(const MapLight& light, const DaylightGrade& grade, double seconds, float seed) {
  const auto time = static_cast<float>(std::fmod(seconds, 3600.0));
  switch (light.kind) {
  case LightKind::Fire: {
    const float flicker = 1.0F + (0.07F * std::sin((7.3F * time) + seed)) +
                          (0.05F * std::sin((13.7F * time) + (2.1F * seed))) +
                          (0.03F * std::sin((29.3F * time) + (4.7F * seed)));
    return light.intensity * (0.65F + (0.35F * grade.lamps)) * flicker;
  }
  case LightKind::Lamp:
    return light.intensity * grade.lamps;
  case LightKind::Glow:
    return light.intensity * (0.75F + (0.25F * grade.lamps)) *
           (1.0F + (0.06F * std::sin((1.6F * time) + seed)));
  }
  return 0.0F;
}

} // namespace dwe::render
