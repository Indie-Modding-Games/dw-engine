#include "new_game_scene_rules.hpp"

#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>

#include <glm/vec3.hpp>

#include <cstddef>
#include <utility>

namespace dwe::app {

namespace {

using assets::YamlReader;
using core::YamlNode;

InterviewHost readHost(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(
          node, "the host",
          {"species", "position", "clip", "turn_frame", "turned_clip", "yaw", "turned_yaw"})) {
    return {};
  }
  glm::vec3 position{0.0F};
  const auto coordinates = reader.sequence(reader.field(node, "position"), "the host's position");
  if (coordinates.size() == 3) {
    for (std::size_t axis = 0; axis < coordinates.size(); ++axis) {
      position[static_cast<glm::length_t>(axis)] = reader.number(coordinates[axis]);
    }
  } else {
    reader.fail(node, "the host's position takes x, y and z");
  }
  return InterviewHost{
      .species = reader.text(reader.field(node, "species")),
      .position = position,
      .clip = reader.integer<std::uint32_t>(reader.field(node, "clip")),
      .turnFrame = reader.integer<std::uint32_t>(reader.field(node, "turn_frame")),
      .turnedClip = reader.integer<std::uint32_t>(reader.field(node, "turned_clip")),
      .yaw = reader.integer<std::int32_t>(reader.field(node, "yaw")),
      .turnedYaw = reader.integer<std::int32_t>(reader.field(node, "turned_yaw")),
  };
}

game::story::DialogueSound readSound(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a sound", {"slot", "sound"})) {
    return {};
  }
  return {.slot = reader.integer<std::int64_t>(reader.field(node, "slot")),
          .sound = reader.integer<std::int64_t>(reader.field(node, "sound"))};
}

SceneSounds readSounds(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "the scene's sounds", {"cursor", "confirm", "refused"})) {
    return {};
  }
  return {.cursor = readSound(reader, reader.field(node, "cursor")),
          .confirm = readSound(reader, reader.field(node, "confirm")),
          .refused = readSound(reader, reader.field(node, "refused"))};
}

NamingRules readNaming(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "the naming box",
                         {"length", "columns", "rows", "full_delete_takes_two",
                          "repeat_delay_frames", "repeat_frames"})) {
    return {};
  }
  NamingRules naming{
      .length =
          static_cast<std::size_t>(reader.integer<std::uint32_t>(reader.field(node, "length"))),
      .columns =
          static_cast<std::size_t>(reader.integer<std::uint32_t>(reader.field(node, "columns"))),
      .rows = {},
      .fullDeleteTakesTwo = reader.boolean(reader.field(node, "full_delete_takes_two")),
      .repeatDelayFrames = reader.integer<std::uint32_t>(reader.field(node, "repeat_delay_frames")),
      .repeatFrames = reader.integer<std::uint32_t>(reader.field(node, "repeat_frames")),
      .frameTicks = 1};
  for (const YamlNode& row : reader.sequence(reader.field(node, "rows"), "the letters' rows")) {
    naming.rows.push_back(reader.text(row));
  }
  if (reader.ok() && (naming.length == 0 || naming.columns == 0 || naming.rows.empty() ||
                      naming.rows.size() % 2 != 0 || naming.repeatFrames == 0)) {
    reader.fail(node, "a name needs a length, columns, an even number of rows and a repeat");
  }
  return naming;
}

} // namespace

core::Result<NewGameSceneRules> readNewGameSceneRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, "the new_game_scene section",
                         {"lines", "partners", "fade_in_frames", "fade_out_frames", "frame_ticks",
                          "camera_distance", "camera_projection", "camera_centre_line", "host",
                          "sounds", "naming"})) {
    return std::unexpected{reader.error(section.file)};
  }
  NewGameSceneRules rules;
  rules.lines = reader.integer<std::int32_t>(reader.field(root, "lines"));
  for (const YamlNode& partner :
       reader.sequence(reader.field(root, "partners"), "the answers' partners")) {
    rules.partners.push_back(reader.text(partner));
  }
  rules.fadeInFrames = reader.integer<std::uint32_t>(reader.field(root, "fade_in_frames"));
  rules.fadeOutFrames = reader.integer<std::uint32_t>(reader.field(root, "fade_out_frames"));
  rules.frameTicks = reader.integer<std::uint32_t>(reader.field(root, "frame_ticks"));
  rules.cameraDistance = reader.number(reader.field(root, "camera_distance"));
  rules.cameraProjection = reader.number(reader.field(root, "camera_projection"));
  rules.cameraCentreLine = reader.number(reader.field(root, "camera_centre_line"));
  rules.host = readHost(reader, reader.field(root, "host"));
  rules.sounds = readSounds(reader, reader.field(root, "sounds"));
  rules.naming = readNaming(reader, reader.field(root, "naming"));
  rules.naming.frameTicks = rules.frameTicks;
  if (reader.ok() && (rules.partners.empty() || rules.frameTicks == 0)) {
    reader.fail(root, "the scene needs a partner for each answer and at least a tick a frame");
  }
  return reader.finish(std::move(rules), section.file);
}

std::optional<NewGameSceneRules> loadNewGameSceneRules(const assets::VirtualFileSystem& files,
                                                       std::string_view rulesetId) {
  auto rules = ruleset::loadRuleset(files, rulesetId);
  if (!rules) {
    core::logWarning("new game scene: no ruleset: {}", rules.error().message);
    return std::nullopt;
  }
  const ruleset::Section* section = rules->section(NewGameSceneSection);
  if (section == nullptr) {
    core::logInfo("new game scene: the ruleset has no {} section; no opening questions",
                  NewGameSceneSection);
    return std::nullopt;
  }
  auto read = readNewGameSceneRules(*section);
  if (!read) {
    core::logWarning("new game scene: {}", read.error().message);
    return std::nullopt;
  }
  return std::move(*read);
}

} // namespace dwe::app
