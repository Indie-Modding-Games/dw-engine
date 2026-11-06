#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/field/rules.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::game::field {

namespace {

using assets::YamlReader;
using core::YamlNode;

// A distance, speed or time: zero or more.
float readNonNegative(YamlReader& reader, const YamlNode& node) {
  const float value = reader.number(node);
  if (value < 0.0F) {
    reader.fail(node, "the value cannot be negative");
  }
  return value;
}

// A size that must be more than zero.
float readPositive(YamlReader& reader, const YamlNode& node) {
  const float value = reader.number(node);
  if (reader.ok() && value <= 0.0F) {
    reader.fail(node, "the value must be more than zero");
  }
  return value;
}

MoverRules readMover(YamlReader& reader, const YamlNode& node, std::string_view what,
                     std::initializer_list<std::string_view> extraKeys = {}) {
  std::vector<std::string_view> keys{"model", "radius", "walk_speed", "run_speed", "turn_speed"};
  keys.insert(keys.end(), extraKeys.begin(), extraKeys.end());
  if (!node.isMapping()) {
    reader.fail(node, std::format("{} must be a mapping", what));
    return {};
  }
  for (const core::YamlEntry& entry : node.entries()) {
    if (std::ranges::find(keys, entry.key) == keys.end()) {
      reader.fail(entry.value, std::format("unknown key '{}' in {}", entry.key, what));
      return {};
    }
  }
  MoverRules mover{.model = reader.text(reader.field(node, "model")),
                   .radius = readPositive(reader, reader.field(node, "radius")),
                   .walkSpeed = readPositive(reader, reader.field(node, "walk_speed")),
                   .runSpeed = readPositive(reader, reader.field(node, "run_speed")),
                   .turnSpeed = readNonNegative(reader, reader.field(node, "turn_speed"))};
  if (reader.ok() && mover.runSpeed < mover.walkSpeed) {
    reader.fail(node, std::format("{}: run_speed cannot be less than walk_speed", what));
  }
  return mover;
}

PartnerRules readPartner(YamlReader& reader, const YamlNode& node) {
  PartnerRules partner{
      .mover = readMover(reader, node, "partner",
                         {"run_distance", "walk_distance", "spawn_distance", "trail_length",
                          "sight_half_width", "stuck_seconds", "teleport_distance",
                          "happy_gesture_from", "unhappy_gesture_below", "gesture_idle_plays"})};
  if (!reader.ok()) {
    return partner;
  }
  partner.runDistance = readPositive(reader, reader.field(node, "run_distance"));
  partner.walkDistance = readPositive(reader, reader.field(node, "walk_distance"));
  partner.spawnDistance = readNonNegative(reader, reader.field(node, "spawn_distance"));
  partner.trailLength = reader.integer<std::int32_t>(reader.field(node, "trail_length"));
  partner.sightHalfWidth = readNonNegative(reader, reader.field(node, "sight_half_width"));
  partner.stuckSeconds = readPositive(reader, reader.field(node, "stuck_seconds"));
  partner.teleportDistance = readPositive(reader, reader.field(node, "teleport_distance"));
  partner.happyGestureFrom = reader.integer<std::int32_t>(reader.field(node, "happy_gesture_from"));
  partner.unhappyGestureBelow =
      reader.integer<std::int32_t>(reader.field(node, "unhappy_gesture_below"));
  partner.gestureIdlePlays = reader.integer<std::int32_t>(reader.field(node, "gesture_idle_plays"));
  if (reader.ok() && partner.runDistance < partner.walkDistance) {
    reader.fail(node, "partner: run_distance cannot be less than walk_distance");
  }
  if (reader.ok() && partner.trailLength < 1) {
    reader.fail(node, "partner: trail_length must be at least 1");
  }
  if (reader.ok() && partner.gestureIdlePlays < 1) {
    reader.fail(node, "partner: gesture_idle_plays must be at least 1");
  }
  if (reader.ok() && partner.happyGestureFrom <= partner.unhappyGestureBelow) {
    reader.fail(node, "partner: happy_gesture_from must be above unhappy_gesture_below");
  }
  return partner;
}

InputRules readInput(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "input", {"dead_zone", "arrive_distance"})) {
    return {};
  }
  InputRules input{.deadZone = readNonNegative(reader, reader.field(node, "dead_zone")),
                   .arriveDistance = readPositive(reader, reader.field(node, "arrive_distance"))};
  if (reader.ok() && input.deadZone >= 1.0F) {
    reader.fail(node, "input: dead_zone must be less than 1");
  }
  return input;
}

TransitionRules readTransition(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "transition",
                         {"frame_seconds", "fade_out_frames", "fade_in_frames", "shade_step",
                          "shade_max", "shade_full", "name_frame"},
                         {"start_fade_in_frames"})) {
    return {};
  }
  const auto count = [&](std::string_view key) {
    const YamlNode& value = reader.field(node, key);
    const auto number = reader.integer<std::int32_t>(value);
    if (reader.ok() && number < 1) {
      reader.fail(value, core::formatMessage(messages::TransitionCountTooLow, key));
    }
    return number;
  };
  TransitionRules transition{.frameSeconds =
                                 readPositive(reader, reader.field(node, "frame_seconds")),
                             .fadeOutFrames = count("fade_out_frames"),
                             .fadeInFrames = count("fade_in_frames"),
                             .shadeStep = count("shade_step"),
                             .shadeMax = count("shade_max"),
                             .shadeFull = count("shade_full"),
                             .nameFrame = count("name_frame")};
  if (node.find("start_fade_in_frames") != nullptr) {
    transition.startFadeInFrames = count("start_fade_in_frames");
  }
  if (reader.ok() && (transition.shadeMax > MaxShade || transition.shadeFull > MaxShade)) {
    reader.fail(node, core::formatMessage(messages::TransitionShadeTooHigh, MaxShade));
  }
  return transition;
}

glm::vec3 readLight(YamlReader& reader, const YamlNode& node, std::string_view what) {
  const auto items = reader.sequence(node, what, 3);
  if (items.size() != 3) {
    return glm::vec3{1.0F};
  }
  const glm::vec3 light{reader.number(items[0]), reader.number(items[1]), reader.number(items[2])};
  if (reader.ok() && (glm::any(glm::lessThan(light, glm::vec3{0.0F})) ||
                      glm::any(glm::greaterThan(light, glm::vec3{1.0F})))) {
    reader.fail(node, std::format("time_of_day: {} must be three numbers from 0 to 1", what));
  }
  return light;
}

TimeOfDayRules readTimeOfDay(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "time_of_day",
                         {"day_start", "dusk_start", "night_start", "dusk_light", "night_light"})) {
    return {};
  }
  TimeOfDayRules time{
      .dayStart = reader.integer<std::int32_t>(reader.field(node, "day_start")),
      .duskStart = reader.integer<std::int32_t>(reader.field(node, "dusk_start")),
      .nightStart = reader.integer<std::int32_t>(reader.field(node, "night_start")),
      .duskLight = readLight(reader, reader.field(node, "dusk_light"), "dusk_light"),
      .nightLight = readLight(reader, reader.field(node, "night_light"), "night_light")};
  constexpr std::int32_t HoursPerDay = 24;
  if (reader.ok() && (time.dayStart < 0 || time.dayStart >= time.duskStart ||
                      time.duskStart >= time.nightStart || time.nightStart >= HoursPerDay)) {
    reader.fail(node,
                "time_of_day: the hours must go 0 <= day_start < dusk_start < night_start <= 23");
  }
  return time;
}

// A choice among names: its index in `names`, or a failure that lists them.
std::size_t readChoice(YamlReader& reader, const YamlNode& node,
                       std::initializer_list<std::string_view> names) {
  const std::string name = reader.text(node);
  const auto* found = std::ranges::find(names, name);
  if (found == names.end()) {
    if (reader.ok()) {
      reader.fail(node, std::format("unknown name {}", name));
    }
    return 0;
  }
  return static_cast<std::size_t>(found - names.begin());
}

NpcTrackingMode readTrackingMode(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "npc.tracking.modes", {"follow_modes", "spot", "reaction"},
                         {"gait"})) {
    return {};
  }
  NpcTrackingMode mode{.followModes = {},
                       .spot = static_cast<NpcSpot>(
                           readChoice(reader, reader.field(node, "spot"), {"rect", "radius"})),
                       .reaction = static_cast<NpcReaction>(readChoice(
                           reader, reader.field(node, "reaction"), {"look", "chase", "follow"})),
                       .gait = NpcGait::Walk};
  for (const YamlNode& followMode :
       reader.sequence(reader.field(node, "follow_modes"), "follow_modes")) {
    mode.followModes.push_back(reader.integer<std::int32_t>(followMode));
  }
  if (const YamlNode* gait = node.find("gait"); gait != nullptr) {
    mode.gait = static_cast<NpcGait>(readChoice(reader, *gait, {"walk", "run"}));
  }
  if (reader.ok() && mode.followModes.empty()) {
    reader.fail(node, "npc.tracking.modes: follow_modes cannot be empty");
  }
  return mode;
}

NpcTrackingRules readTracking(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "npc.tracking",
                         {"radius_tenths", "chase_wait_frames", "follow_wait_frames", "modes"})) {
    return {};
  }
  NpcTrackingRules tracking{
      .radiusTenths = reader.integer<std::int32_t>(reader.field(node, "radius_tenths")),
      .chaseWaitFrames = reader.integer<std::int32_t>(reader.field(node, "chase_wait_frames")),
      .followWaitFrames = reader.integer<std::int32_t>(reader.field(node, "follow_wait_frames")),
      .modes = {}};
  for (const YamlNode& mode : reader.sequence(reader.field(node, "modes"), "modes")) {
    tracking.modes.push_back(readTrackingMode(reader, mode));
  }
  const bool positive =
      tracking.radiusTenths > 0 && tracking.chaseWaitFrames >= 0 && tracking.followWaitFrames >= 0;
  if (reader.ok() && !positive) {
    reader.fail(node,
                "npc.tracking: radius_tenths must be more than zero and the waits not negative");
  }
  return tracking;
}

NpcRules readNpc(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "npc",
                         {"walk_speed", "run_speed", "turn_speed", "grace_turn_speed",
                          "stuck_turn_speed", "talk_reach"},
                         {"tracking"})) {
    return {};
  }
  NpcRules npc{.walkSpeed = readPositive(reader, reader.field(node, "walk_speed")),
               .runSpeed = readPositive(reader, reader.field(node, "run_speed")),
               .turnSpeed = readPositive(reader, reader.field(node, "turn_speed")),
               .graceTurnSpeed = readPositive(reader, reader.field(node, "grace_turn_speed")),
               .stuckTurnSpeed = readPositive(reader, reader.field(node, "stuck_turn_speed")),
               .talkReach = readNonNegative(reader, reader.field(node, "talk_reach")),
               .tracking = std::nullopt};
  if (const YamlNode* tracking = node.find("tracking"); tracking != nullptr) {
    npc.tracking = readTracking(reader, *tracking);
  }
  if (reader.ok() && npc.runSpeed < npc.walkSpeed) {
    reader.fail(node, "npc: run_speed cannot be less than walk_speed");
  }
  return npc;
}

template <typename T>
core::Result<T> loadDocument(const assets::VirtualFileSystem& vfs, std::string_view idText,
                             core::Result<T> (*parse)(std::string_view)) {
  auto id = assets::AssetId::parse(idText);
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto bytes = vfs.read(*id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  return parse(std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}

} // namespace

std::int32_t fadeOutShade(const TransitionRules& rules, std::int32_t frame,
                          std::optional<std::int32_t> from) {
  // fade.c: renderFadeOut adds the step to FADE_PROGRESS, which still holds the shade the last
  // fade drew.
  const std::int32_t start = from.value_or(fadeInShade(rules, rules.fadeInFrames));
  return std::min(rules.shadeMax, start + (rules.shadeStep * std::max(frame, 0)));
}

std::int32_t fadeInShade(const TransitionRules& rules, std::int32_t frame,
                         std::optional<std::int32_t> frames) {
  // fade.c: renderFadeIn draws shadeMax - step * FADE_IN_CURRENT, with the step fadeFromBlack's
  // length gives.
  const std::int32_t step = frames ? rules.shadeMax / (*frames + 1) : rules.shadeStep;
  return std::clamp(rules.shadeMax - (step * std::max(frame, 0)), 0, rules.shadeMax);
}

float shadeDarkness(const TransitionRules& rules, std::int32_t shade) {
  // fade.c: renderFade's texel (ETCTIM entry 4, palette 6: 0xFFFF) is white, with the
  // semi-transparency bit set; the console multiplies it by shade / 128 and subtracts the result
  // (FADE_MODE 2, B - F), saturating at white.
  return std::clamp(static_cast<float>(shade) / static_cast<float>(rules.shadeFull), 0.0F, 1.0F);
}

ScriptMotionRules readScriptMotion(YamlReader& reader, const YamlNode* node) {
  if (node == nullptr || !reader.expectKeys(*node, "script_motion", {"look_turn_speed"})) {
    return {};
  }
  return {.lookTurnSpeed = readPositive(reader, reader.field(*node, "look_turn_speed"))};
}

core::Result<FieldRules> parseFieldRules(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("field rules: {}", tree.error().message)}};
  }
  YamlReader reader;
  if (!reader.expectKeys(*tree, "the field rules",
                         {"player", "partner", "input", "transition", "time_of_day"},
                         {"npc", "script_motion"})) {
    return std::unexpected{reader.error("field rules")};
  }
  FieldRules rules{.player = readMover(reader, reader.field(*tree, "player"), "player"),
                   .partner = readPartner(reader, reader.field(*tree, "partner")),
                   .input = readInput(reader, reader.field(*tree, "input")),
                   .transition = readTransition(reader, reader.field(*tree, "transition")),
                   .timeOfDay = readTimeOfDay(reader, reader.field(*tree, "time_of_day")),
                   .npc = {},
                   .scriptMotion = readScriptMotion(reader, tree->find("script_motion"))};
  if (const YamlNode* npc = tree->find("npc"); npc != nullptr) {
    rules.npc = readNpc(reader, *npc);
  }
  return reader.finish(std::move(rules), "field rules");
}

core::Result<FieldWarps> parseFieldWarps(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("field warps: {}", tree.error().message)}};
  }
  YamlReader reader;
  if (!reader.expectKeys(*tree, "the field warps", {"start"})) {
    return std::unexpected{reader.error("field warps")};
  }
  FieldWarps warps;
  const YamlNode& start = reader.field(*tree, "start");
  if (reader.expectKeys(start, "start", {"map", "exit"})) {
    warps.startMap = reader.text(reader.field(start, "map"));
    warps.startExit = reader.integer<std::int32_t>(reader.field(start, "exit"));
  }
  return reader.finish(std::move(warps), "field warps");
}

core::Result<FieldRules> loadFieldRules(const assets::VirtualFileSystem& vfs) {
  return loadDocument<FieldRules>(vfs, "field/rules", &parseFieldRules);
}

core::Result<FieldWarps> loadFieldWarps(const assets::VirtualFileSystem& vfs) {
  return loadDocument<FieldWarps>(vfs, "field/warps", &parseFieldWarps);
}

} // namespace dwe::game::field
