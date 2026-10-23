#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::gym {

// The ruleset section with the gym's choreography: rules/<id>/gym.yaml
// (content/schemas/ruleset_gym.schema.json). Frames are the original's, 20 per second.
inline constexpr std::string_view GymSectionName = "gym";

// A point of the field, in world units (x and z).
struct Point {
  std::int32_t x = 0;
  std::int32_t z = 0;
};

// A step of the map's background objects, in background pixels.
struct ObjectMove {
  std::int32_t x = 0;
  std::int32_t y = 0;
};

// The map's background objects a course moves (MAP_TILE_DATA.objects).
struct ObjectGroup {
  std::int32_t first = 0;
  std::int32_t count = 0;
};

// trn_reward.c: TRN_createCloudFXLine.
struct CloudLine {
  std::int32_t everyFrames = 1;
  Point start;
  Point step;
  std::int32_t count = 0;
};

struct CourseSounds {
  std::int32_t start = 0;
  std::int32_t beat = 0;
  std::int32_t end = 0;
};

struct CourseAnimations {
  std::int32_t walk = 0;
  std::int32_t work = 0;
  std::int32_t done = 0;
};

// One course of the gym (trn_*.c: TRN_setup*Training and TRN_tick*Training). `id` is also its
// course in training.yaml.
struct CourseRules {
  std::string id;
  Point spot;
  std::int32_t cameraSpeed = 0;
  // 4096 per turn.
  std::int32_t facing = 0;
  ObjectGroup objects;
  std::int32_t cycleFrames = 0;
  std::int32_t beatFrames = 1;
  ObjectMove beatMove;
  std::int32_t jitter = 0;
  std::int32_t soundEveryFrames = 1;
  ObjectMove returnMove;
  std::int32_t restFrames = 0;
  CourseSounds sounds;
  CourseAnimations animations;
  std::vector<CloudLine> clouds;
};

// trn_hud.c: the stats box's count.
struct CountRules {
  std::int32_t delayFrames = 0;
  std::int32_t soundSlot = 0;
  std::int32_t sound = 0;
  std::int32_t hpMpMax = 0;
  std::int32_t statMax = 0;
};

struct GymRules {
  std::string map;
  std::string place;
  std::int32_t courseVariable = 0;
  std::vector<std::string> courseByChoice;
  std::int32_t slotVariable = 0;
  std::string manualItem;
  std::int32_t soundSlot = 0;
  std::int32_t cloudScatter = 0;
  CountRules count;
  std::vector<CourseRules> courses;

  // The course with this id, or nullptr.
  [[nodiscard]] const CourseRules* findCourse(std::string_view id) const;
  // The course a choice of the script names (the course variable), or nullptr.
  [[nodiscard]] const CourseRules* courseOfChoice(std::int64_t choice) const;
};

// Reads the gym section; errors name the file, line and column.
core::Result<GymRules> readGymRules(const ruleset::Section& section);

// The gym rules of a ruleset; none (no course) when it has no gym section.
core::Result<GymRules> readGymRules(const ruleset::Ruleset& ruleset);

ruleset::SectionCheck gymSectionCheck();

} // namespace dwe::game::gym
