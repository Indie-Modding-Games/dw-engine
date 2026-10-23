#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/fishing/rules.hpp>

#include <algorithm>
#include <span>
#include <utility>

namespace dwe::game::fishing {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view SectionText = "the fishing section";
constexpr std::string_view CheckEveryKey = "check_every";
constexpr std::string_view PickKey = "pick";
constexpr std::string_view ShoreByYKey = "shore_by_y";
constexpr std::string_view ItemKey = "item";
constexpr std::string_view SeaSerpentKey = "sea_serpent";
constexpr std::string_view BiteChanceKey = "bite_chance";
constexpr std::string_view BiteEveryKey = "bite_every";
constexpr std::string_view BitesKey = "bites";
constexpr std::string_view EveryKey = "every";
constexpr std::string_view ReachKey = "reach";
constexpr std::string_view AttractReachKey = "attract_reach";
constexpr std::string_view PopulationKey = "population";
constexpr std::string_view PoolKey = "pool";
constexpr std::string_view SpawnBelowKey = "spawn_below";
constexpr std::string_view LifetimeKey = "lifetime";
constexpr std::string_view ScaredPauseKey = "scared_pause";
constexpr std::string_view HookKey = "hook";
constexpr std::string_view LargeSizeKey = "large_size";
constexpr std::string_view SmallKey = "small";
constexpr std::string_view LargeKey = "large";
constexpr std::string_view NibbleKey = "nibble";
constexpr std::string_view PullKey = "pull";
constexpr std::string_view PowerBonusKey = "power_bonus";
constexpr std::string_view PowerMaxBonusKey = "power_max_bonus";
constexpr std::string_view TensionKey = "tension";
constexpr std::string_view LimitKey = "limit";
constexpr std::string_view OldRodKey = "old_rod";
constexpr std::string_view AmazingRodKey = "amazing_rod";
constexpr std::string_view SizesKey = "sizes";
constexpr std::string_view SlackKey = "slack";
constexpr std::string_view FishRunningKey = "fish_running";
constexpr std::string_view HeldKey = "held";
constexpr std::string_view HeldRunningKey = "held_running";
constexpr std::string_view SnapStepsKey = "snap_steps";
constexpr std::string_view SnapFramesKey = "snap_frames_per_step";
constexpr std::string_view JunkKey = "junk";
constexpr std::string_view AfterKey = "after";
constexpr std::string_view ChanceKey = "chance";
constexpr std::string_view RollKey = "roll";
constexpr std::string_view CastKey = "cast";
constexpr std::string_view PowerMaxKey = "power_max";
constexpr std::string_view SpeedKey = "speed";
constexpr std::string_view MinKey = "min";
constexpr std::string_view RightLimitKey = "right_limit";
constexpr std::string_view OpenAboveKey = "open_above";
constexpr std::string_view SoundsKey = "sounds";
constexpr std::string_view SlotKey = "slot";
constexpr std::string_view SplashSoundKey = "splash";
constexpr std::string_view BiteKey = "bite";
constexpr std::string_view StruggleKey = "struggle";
constexpr std::string_view RunKey = "run";
constexpr std::string_view LandedKey = "landed";
constexpr std::string_view LostKey = "lost";
constexpr std::string_view NoticeKey = "notice";

constexpr std::size_t PairSize = 2;

std::int32_t integer(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  return reader.integer<std::int32_t>(reader.field(parent, key));
}

Range range(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  const auto values = reader.sequence(reader.field(parent, key), key, PairSize);
  if (values.size() != PairSize) {
    return {};
  }
  return {.min = reader.integer<std::int32_t>(values[0]),
          .max = reader.integer<std::int32_t>(values[1])};
}

HookTiming parseTiming(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  const YamlNode& node = reader.field(parent, key);
  if (!reader.expectKeys(node, key, {NibbleKey, PullKey})) {
    return {};
  }
  return {.nibble = range(reader, node, NibbleKey), .pull = integer(reader, node, PullKey)};
}

HookRules parseHook(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, HookKey,
                         {LargeSizeKey, SmallKey, LargeKey, PowerBonusKey, PowerMaxBonusKey})) {
    return {};
  }
  return {.largeSize = integer(reader, node, LargeSizeKey),
          .small = parseTiming(reader, node, SmallKey),
          .large = parseTiming(reader, node, LargeKey),
          .powerBonus = integer(reader, node, PowerBonusKey),
          .powerMaxBonus = integer(reader, node, PowerMaxBonusKey)};
}

TensionRules parseTension(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, TensionKey,
                         {LimitKey, SizesKey, SlackKey, FishRunningKey, HeldKey, HeldRunningKey,
                          SnapStepsKey, SnapFramesKey})) {
    return {};
  }
  const YamlNode& limit = reader.field(node, LimitKey);
  if (!reader.expectKeys(limit, LimitKey, {OldRodKey, AmazingRodKey})) {
    return {};
  }
  return {.oldRodLimit = integer(reader, limit, OldRodKey),
          .amazingRodLimit = integer(reader, limit, AmazingRodKey),
          .sizes = range(reader, node, SizesKey),
          .slack = range(reader, node, SlackKey),
          .fishRunning = range(reader, node, FishRunningKey),
          .held = range(reader, node, HeldKey),
          .heldRunning = range(reader, node, HeldRunningKey),
          .snapSteps = range(reader, node, SnapStepsKey),
          .snapFramesPerStep = integer(reader, node, SnapFramesKey)};
}

CastRules parseCast(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, CastKey, {PowerBonusKey, PowerMaxKey, SpeedKey})) {
    return {};
  }
  const YamlNode& speed = reader.field(node, SpeedKey);
  if (!reader.expectKeys(speed, SpeedKey, {MinKey, OldRodKey, AmazingRodKey})) {
    return {};
  }
  return {.powerBonus = integer(reader, node, PowerBonusKey),
          .powerMax = integer(reader, node, PowerMaxKey),
          .minSpeed = integer(reader, speed, MinKey),
          .oldRodSpeed = integer(reader, speed, OldRodKey),
          .amazingRodSpeed = integer(reader, speed, AmazingRodKey)};
}

FishingSounds parseSounds(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, SoundsKey,
                         {SlotKey, CastKey, SplashSoundKey, NibbleKey, BiteKey, StruggleKey, RunKey,
                          LandedKey, ItemKey, LostKey, NoticeKey})) {
    return {};
  }
  return {.slot = integer(reader, node, SlotKey),
          .cast = integer(reader, node, CastKey),
          .splash = integer(reader, node, SplashSoundKey),
          .nibble = integer(reader, node, NibbleKey),
          .bite = integer(reader, node, BiteKey),
          .struggle = integer(reader, node, StruggleKey),
          .run = integer(reader, node, RunKey),
          .landed = integer(reader, node, LandedKey),
          .item = integer(reader, node, ItemKey),
          .lost = integer(reader, node, LostKey),
          .notice = integer(reader, node, NoticeKey)};
}

// FISH_lerpTableByX / FISH_lerpTableByY: the line's coordinate at `at` on the other axis, or
// nothing outside the line's span.
std::optional<std::int32_t> lineAt(std::span<const glm::ivec2> line, std::int32_t at, int axis) {
  const int other = 1 - axis;
  if (at < line.front()[axis] || line.back()[axis] < at) {
    return std::nullopt;
  }
  std::size_t i = line.size() - 2;
  while (i > 0 && at < line[i][axis]) {
    --i;
  }
  const glm::ivec2 a = line[i];
  const glm::ivec2 b = line[i + 1];
  if (b[axis] == a[axis]) {
    return a[other];
  }
  return a[other] + (((at - a[axis]) * (b[other] - a[other])) / (b[axis] - a[axis]));
}

} // namespace

core::Result<FishingRules> parseFishingRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, SectionText,
                         {SeaSerpentKey, BitesKey, PopulationKey, HookKey, TensionKey, JunkKey,
                          CastKey, ShoreByYKey, SoundsKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  FishingRules rules;
  const YamlNode& serpent = reader.field(root, SeaSerpentKey);
  if (reader.expectKeys(serpent, SeaSerpentKey, {BiteChanceKey, BiteEveryKey, CheckEveryKey})) {
    rules.seaSerpent = {.biteChance = integer(reader, serpent, BiteChanceKey),
                        .biteEvery = integer(reader, serpent, BiteEveryKey),
                        .checkEvery = integer(reader, serpent, CheckEveryKey)};
  }
  const YamlNode& bites = reader.field(root, BitesKey);
  if (reader.expectKeys(bites, BitesKey, {EveryKey, ReachKey, AttractReachKey})) {
    rules.bites = {.every = integer(reader, bites, EveryKey),
                   .reach = integer(reader, bites, ReachKey),
                   .attractReach = integer(reader, bites, AttractReachKey)};
  }
  const YamlNode& population = reader.field(root, PopulationKey);
  if (reader.expectKeys(population, PopulationKey,
                        {PoolKey, SpawnBelowKey, LifetimeKey, ScaredPauseKey})) {
    rules.population = {.pool = integer(reader, population, PoolKey),
                        .spawnBelow = integer(reader, population, SpawnBelowKey),
                        .lifetime = range(reader, population, LifetimeKey),
                        .scaredPause = integer(reader, population, ScaredPauseKey)};
  }
  rules.hook = parseHook(reader, reader.field(root, HookKey));
  rules.tension = parseTension(reader, reader.field(root, TensionKey));
  const YamlNode& junk = reader.field(root, JunkKey);
  if (reader.expectKeys(junk, JunkKey, {AfterKey, EveryKey, ChanceKey, RollKey, PickKey})) {
    rules.junk = {.after = integer(reader, junk, AfterKey),
                  .every = integer(reader, junk, EveryKey),
                  .chance = integer(reader, junk, ChanceKey),
                  .roll = integer(reader, junk, RollKey),
                  .pick = integer(reader, junk, PickKey)};
  }
  rules.cast = parseCast(reader, reader.field(root, CastKey));
  const YamlNode& shore = reader.field(root, ShoreByYKey);
  if (reader.expectKeys(shore, ShoreByYKey, {RightLimitKey, OpenAboveKey})) {
    rules.shoreByY = {.rightLimit = integer(reader, shore, RightLimitKey),
                      .openAbove = integer(reader, shore, OpenAboveKey)};
  }
  rules.sounds = parseSounds(reader, reader.field(root, SoundsKey));
  return reader.finish(std::move(rules), section.file);
}

bool isWater(const Shoreline& shoreline, const ShoreByYRules& byY, glm::ivec2 point) {
  constexpr int X = 0;
  constexpr int Y = 1;
  if (shoreline.points.size() < 2) {
    return false;
  }
  if (shoreline.axis == Shoreline::Axis::X) {
    // Past the line's ends the original's lerp gives 0, which counts as water.
    const std::optional<std::int32_t> lineY = lineAt(shoreline.points, point.x, X);
    return !lineY || point.y >= *lineY;
  }
  if (point.x >= byY.rightLimit) {
    return false;
  }
  if (point.y < byY.openAbove) {
    return true;
  }
  const std::optional<std::int32_t> lineX = lineAt(shoreline.points, point.y, Y);
  return !lineX || point.x <= *lineX;
}

std::int32_t remapClamped(std::int32_t x, Range from, Range to) {
  if (from.max == from.min) {
    return to.min;
  }
  if (from.max < from.min) {
    std::swap(from.min, from.max);
  }
  x = std::clamp(x, from.min, from.max);
  const std::int32_t span = from.max - from.min;
  return to.min + ((((to.max - to.min) * (x - from.min)) + (span / 2)) / span);
}

} // namespace dwe::game::fishing
