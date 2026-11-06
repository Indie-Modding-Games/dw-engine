#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/tournament/tournament.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string_view>

namespace dwe::game::tournament {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view ArenaKey = "arena";
constexpr std::string_view ArenasKey = "arenas";
constexpr std::string_view ModelKey = "model";
constexpr std::string_view CollisionKey = "collision";
constexpr std::string_view StartXKey = "start_x";
constexpr std::string_view WalkInKey = "walk_in";
constexpr std::string_view SecondArenaMapKey = "second_arena_map";
constexpr std::string_view YawsKey = "yaws";
constexpr std::string_view PartnerKey = "partner";
constexpr std::string_view RivalKey = "rival";
constexpr std::string_view CameraKey = "camera";
constexpr std::string_view ProjectionKey = "projection";
constexpr std::string_view DistanceDivisorKey = "distance_divisor";
constexpr std::string_view MinDistanceKey = "min_distance";
constexpr std::string_view MaxDistanceKey = "max_distance";
constexpr std::string_view EyeHeightKey = "eye_height";
constexpr std::string_view DropKey = "drop";

std::int32_t integer(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  return reader.integer<std::int32_t>(reader.field(parent, key));
}

// SquareRoot0: the integer square root.
std::int64_t squareRoot(std::int64_t value) {
  std::int64_t root = 0;
  while ((root + 1) * (root + 1) <= value) {
    ++root;
  }
  return root;
}

} // namespace

core::Result<ArenaRules> parseArenaRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& arena = reader.field(section.node, ArenaKey);
  ArenaRules rules;
  if (reader.expectKeys(arena, ArenaKey, {ArenasKey, SecondArenaMapKey, YawsKey, CameraKey})) {
    for (const YamlNode& item : reader.sequence(reader.field(arena, ArenasKey), ArenasKey)) {
      if (!reader.expectKeys(item, ArenasKey, {ModelKey, CollisionKey, StartXKey, WalkInKey})) {
        continue;
      }
      rules.arenas.push_back({.model = reader.text(reader.field(item, ModelKey)),
                              .collision = reader.text(reader.field(item, CollisionKey)),
                              .startX = integer(reader, item, StartXKey),
                              .walkIn = integer(reader, item, WalkInKey)});
    }
    rules.secondArenaMap = reader.text(reader.field(arena, SecondArenaMapKey));
    const YamlNode& yaws = reader.field(arena, YawsKey);
    if (reader.expectKeys(yaws, YawsKey, {PartnerKey, RivalKey})) {
      rules.partnerYaw = integer(reader, yaws, PartnerKey);
      rules.rivalYaw = integer(reader, yaws, RivalKey);
    }
    const YamlNode& camera = reader.field(arena, CameraKey);
    if (reader.expectKeys(camera, CameraKey,
                          {ProjectionKey, DistanceDivisorKey, MinDistanceKey, MaxDistanceKey,
                           EyeHeightKey, DropKey})) {
      rules.camera = {.projection = integer(reader, camera, ProjectionKey),
                      .distanceDivisor = integer(reader, camera, DistanceDivisorKey),
                      .minDistance = integer(reader, camera, MinDistanceKey),
                      .maxDistance = integer(reader, camera, MaxDistanceKey),
                      .eyeHeight = integer(reader, camera, EyeHeightKey),
                      .drop = integer(reader, camera, DropKey)};
    }
  }
  if (!reader.ok()) {
    return std::unexpected{reader.error(section.file)};
  }
  if (rules.arenas.empty()) {
    return core::fail(std::format("{}: arena.arenas is empty", section.file));
  }
  if (rules.camera.distanceDivisor <= 0) {
    return core::fail(
        std::format("{}: arena.camera.distance_divisor is not positive", section.file));
  }
  return rules;
}

std::size_t arenaFor(const ArenaRules& rules, std::string_view map) {
  return map == rules.secondArenaMap && rules.arenas.size() > 1 ? 1 : 0;
}

ArenaStart arenaStart(const Arena& arena) {
  const std::int32_t x = arena.startX - arena.walkIn;
  return {.partner = {.x = x, .z = 0}, .rival = {.x = -x, .z = 0}};
}

ArenaView arenaView(const ArenaCamera& camera, ArenaPoint partner, ArenaPoint rival) {
  const std::int64_t dx = rival.x - partner.x;
  const std::int64_t dz = rival.z - partner.z;
  const std::int64_t dist = squareRoot((dx * dx) + (dz * dz));
  std::int64_t d = (dist * camera.projection) / camera.distanceDivisor;
  d = std::clamp<std::int64_t>(d, camera.minDistance,
                               std::max(camera.minDistance, camera.maxDistance));
  const std::int64_t midX = partner.x + (dx / 2);
  const std::int64_t midZ = partner.z + (dz / 2);
  // Fighters on the same spot have no side to look from: the camera stays behind them on -z.
  const std::int64_t sideX = dist != 0 ? (d * dz) / dist : 0;
  const std::int64_t sideZ = dist != 0 ? (d * dx) / dist : d;
  return {.eye = {static_cast<std::int32_t>(midX + sideX), -camera.eyeHeight,
                  static_cast<std::int32_t>(midZ - sideZ)},
          .target = {static_cast<std::int32_t>(midX), camera.drop - camera.eyeHeight,
                     static_cast<std::int32_t>(midZ)}};
}

} // namespace dwe::game::tournament
