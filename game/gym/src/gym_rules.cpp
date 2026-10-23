#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/gym/gym_rules.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::game::gym {
namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view SectionWhat = "the gym section";

Point readPoint(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a point", {"x", "z"})) {
    return {};
  }
  return {.x = reader.integer<std::int32_t>(reader.field(node, "x")),
          .z = reader.integer<std::int32_t>(reader.field(node, "z"))};
}

ObjectMove readMove(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "an object move", {"x", "y"})) {
    return {};
  }
  return {.x = reader.integer<std::int32_t>(reader.field(node, "x")),
          .y = reader.integer<std::int32_t>(reader.field(node, "y"))};
}

// A whole number of at least one: the course divides by it.
std::int32_t readPositive(YamlReader& reader, const YamlNode& node) {
  const auto frames = reader.integer<std::int32_t>(node);
  if (reader.ok() && frames < 1) {
    reader.fail(node, "the number must be 1 or more");
  }
  return frames;
}

CloudLine readCloud(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a cloud line", {"every", "x", "z", "dx", "dz", "count"})) {
    return {};
  }
  return {.everyFrames = readPositive(reader, reader.field(node, "every")),
          .start = {.x = reader.integer<std::int32_t>(reader.field(node, "x")),
                    .z = reader.integer<std::int32_t>(reader.field(node, "z"))},
          .step = {.x = reader.integer<std::int32_t>(reader.field(node, "dx")),
                   .z = reader.integer<std::int32_t>(reader.field(node, "dz"))},
          .count = reader.integer<std::int32_t>(reader.field(node, "count"))};
}

CourseRules readCourse(YamlReader& reader, const YamlNode& node) {
  CourseRules course;
  if (!reader.expectKeys(node, "a gym course",
                         {"id", "spot", "camera_speed", "facing", "objects", "cycle_frames",
                          "beat_frames", "beat_move", "jitter", "sound_every_frames", "return_move",
                          "rest_frames", "sounds", "animations", "clouds"})) {
    return course;
  }
  course.id = reader.text(reader.field(node, "id"));
  course.spot = readPoint(reader, reader.field(node, "spot"));
  course.cameraSpeed = reader.integer<std::int32_t>(reader.field(node, "camera_speed"));
  course.facing = reader.integer<std::int32_t>(reader.field(node, "facing"));
  const YamlNode& objects = reader.field(node, "objects");
  if (reader.expectKeys(objects, "the course's objects", {"first", "count"})) {
    course.objects = {.first = reader.integer<std::int32_t>(reader.field(objects, "first")),
                      .count = reader.integer<std::int32_t>(reader.field(objects, "count"))};
  }
  course.cycleFrames = readPositive(reader, reader.field(node, "cycle_frames"));
  course.beatFrames = readPositive(reader, reader.field(node, "beat_frames"));
  course.beatMove = readMove(reader, reader.field(node, "beat_move"));
  course.jitter = reader.integer<std::int32_t>(reader.field(node, "jitter"));
  course.soundEveryFrames = readPositive(reader, reader.field(node, "sound_every_frames"));
  course.returnMove = readMove(reader, reader.field(node, "return_move"));
  course.restFrames = reader.integer<std::int32_t>(reader.field(node, "rest_frames"));
  const YamlNode& sounds = reader.field(node, "sounds");
  if (reader.expectKeys(sounds, "the course's sounds", {"start", "beat", "end"})) {
    course.sounds = {.start = reader.integer<std::int32_t>(reader.field(sounds, "start")),
                     .beat = reader.integer<std::int32_t>(reader.field(sounds, "beat")),
                     .end = reader.integer<std::int32_t>(reader.field(sounds, "end"))};
  }
  const YamlNode& animations = reader.field(node, "animations");
  if (reader.expectKeys(animations, "the course's animations", {"walk", "work", "done"})) {
    course.animations = {.walk = reader.integer<std::int32_t>(reader.field(animations, "walk")),
                         .work = reader.integer<std::int32_t>(reader.field(animations, "work")),
                         .done = reader.integer<std::int32_t>(reader.field(animations, "done"))};
  }
  for (const YamlNode& cloud : reader.sequence(reader.field(node, "clouds"), "the clouds")) {
    course.clouds.push_back(readCloud(reader, cloud));
  }
  return course;
}

CountRules readCount(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "the stats count",
                         {"delay_frames", "sound_slot", "sound", "hp_mp_max", "stat_max"})) {
    return {};
  }
  return {.delayFrames = reader.integer<std::int32_t>(reader.field(node, "delay_frames")),
          .soundSlot = reader.integer<std::int32_t>(reader.field(node, "sound_slot")),
          .sound = reader.integer<std::int32_t>(reader.field(node, "sound")),
          .hpMpMax = reader.integer<std::int32_t>(reader.field(node, "hp_mp_max")),
          .statMax = reader.integer<std::int32_t>(reader.field(node, "stat_max"))};
}

} // namespace

const CourseRules* GymRules::findCourse(std::string_view id) const {
  const auto found = std::ranges::find(courses, id, &CourseRules::id);
  return found == courses.end() ? nullptr : &*found;
}

const CourseRules* GymRules::courseOfChoice(std::int64_t choice) const {
  if (choice < 0 || std::cmp_greater_equal(choice, courseByChoice.size())) {
    return nullptr;
  }
  return findCourse(courseByChoice[static_cast<std::size_t>(choice)]);
}

core::Result<GymRules> readGymRules(const ruleset::Section& section) {
  YamlReader reader;
  GymRules rules;
  const YamlNode& node = section.node;
  if (!reader.expectKeys(node, SectionWhat,
                         {"map", "place", "course_variable", "course_by_choice", "slot_variable",
                          "manual_item", "sound_slot", "cloud_scatter", "count", "courses"})) {
    return std::unexpected{reader.error(section.file)};
  }
  rules.map = reader.text(reader.field(node, "map"));
  rules.place = reader.text(reader.field(node, "place"));
  rules.courseVariable = reader.integer<std::int32_t>(reader.field(node, "course_variable"));
  for (const YamlNode& course :
       reader.sequence(reader.field(node, "course_by_choice"), "the courses by choice")) {
    rules.courseByChoice.push_back(reader.text(course));
  }
  rules.slotVariable = reader.integer<std::int32_t>(reader.field(node, "slot_variable"));
  rules.manualItem = reader.text(reader.field(node, "manual_item"));
  rules.soundSlot = reader.integer<std::int32_t>(reader.field(node, "sound_slot"));
  rules.cloudScatter = readPositive(reader, reader.field(node, "cloud_scatter"));
  rules.count = readCount(reader, reader.field(node, "count"));
  for (const YamlNode& course : reader.sequence(reader.field(node, "courses"), "the courses")) {
    rules.courses.push_back(readCourse(reader, course));
  }
  return reader.finish(std::move(rules), section.file);
}

core::Result<GymRules> readGymRules(const ruleset::Ruleset& ruleset) {
  const ruleset::Section* section = ruleset.section(GymSectionName);
  if (section == nullptr) {
    return GymRules{};
  }
  return readGymRules(*section);
}

ruleset::SectionCheck gymSectionCheck() {
  return [](const ruleset::Section& section) -> core::Result<void> {
    return readGymRules(section).transform([](const GymRules& /*rules*/) {});
  };
}

} // namespace dwe::game::gym
