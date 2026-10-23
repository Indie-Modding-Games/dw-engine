#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
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

// A point of the field with its height, where an effect starts.
struct Point3 {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
};

// A move of map objects, in background pixels.
struct ObjectMove {
  std::int32_t x = 0;
  std::int32_t y = 0;
};

// Map objects (depth-layer sprites) by their place in the map's list.
struct ObjectGroup {
  std::int32_t first = 0;
  std::int32_t count = 0;
};

// trn_reward.c: TRN_createCloudFXLine: `count` clouds from `start`, `step` apart, every
// `everyFrames` frames, drifting with the beats done.
struct CloudLine {
  std::int32_t everyFrames = 1;
  Point start;
  Point step;
  std::int32_t count = 0;
};

// trn_hp.c: the partner lifts the weights.
struct HpCourse {
  std::int32_t facing = 0;
  ObjectGroup objects;
  std::int32_t workAnimation = 0;
  std::int32_t beatFrames = 1;
  ObjectMove beatMove;
  std::int32_t jitter = 0;
  std::int32_t beatSoundEvery = 1;
  std::int32_t beatSound = 0;
  ObjectMove returnMove;
  std::vector<CloudLine> clouds;
};

// trn_mp.c: the partner sits under the machine, whose lit part sits over its head.
struct MpCourse {
  // Where it walks first, and back to.
  Point waypoint;
  std::int32_t facing = 0;
  // The objects lifted over its head, and the sets hidden and shown meanwhile.
  ObjectGroup objects;
  ObjectGroup hidden;
  ObjectGroup shown;
  // Background pixels between its head and the objects.
  std::int32_t headLift = 0;
  std::int32_t workAnimation = 0;
  std::int32_t loopSound = 0;
  std::int32_t leaveAnimation = 0;
};

// A species' punch at the offense course: its animation and the frames to the hit.
struct SpeciesAnimation {
  std::int32_t animation = 0;
  std::int32_t frames = 1;
};

// trn_off.c: the partner hits the punching bag, which swings back.
struct OffenseCourse {
  std::int32_t facing = 0;
  ObjectGroup objects;
  std::vector<Point3> particles;
  std::int32_t hitSound = 0;
  // The first frames of the swing push the bag one way, the next frames back.
  std::int32_t recoilFrames = 0;
  ObjectMove recoilMove;
  std::int32_t reboundFrames = 0;
  // Then it shakes, less as the swing goes on: (base - frame) / divisor, until swingFrames.
  std::int32_t swingFrames = 1;
  std::int32_t swingBase = 0;
  ObjectMove swingDivisor{.x = 1, .y = 1};
  std::int32_t leaveAnimation = 0;
  SpeciesAnimation defaultAnimation;
  std::vector<std::pair<std::string, SpeciesAnimation>> speciesAnimations;

  // TRN_TRAINING_ANIM_IDS: the species' punch.
  [[nodiscard]] const SpeciesAnimation& animationOf(std::string_view species) const;
};

// One swing of the defense course's log: the objects move by `dx` a frame until they reach
// `limit` from where they were stored; a pushing swing also pushes the partner back.
struct DefenseSwing {
  std::int32_t dx = 0;
  std::int32_t limit = 0;
  bool push = false;
};

// trn_def.c: a log swings into the partner, who holds it off.
struct DefenseCourse {
  std::int32_t facing = 0;
  ObjectGroup objects;
  std::vector<Point3> particles;
  std::int32_t loopSound = 0;
  std::int32_t workAnimation = 0;
  // World units a pushing swing moves the partner a frame, and how far behind it its dust rises.
  std::int32_t push = 0;
  std::int32_t dustBehind = 0;
  std::vector<DefenseSwing> swings;
};

// trn_speed.c: the partner runs laps across the screen.
struct SpeedCourse {
  std::int32_t facing = 0;
  std::int32_t runAnimation = 0;
  // Every lapFrames, if it ran off screen, it starts again at restartX.
  std::int32_t lapFrames = 1;
  std::int32_t restartX = 0;
};

// trn_reward.c: TRN_awardBrainsTrainingGains and TRN_tryLearnMove: what a brains session may
// teach.
struct BrainLearning {
  // Crossing one of these gives new orders.
  std::vector<std::int32_t> orderThresholds;
  // Crossing one of these, or reaching moveCap, may teach a move.
  std::vector<std::int32_t> moveThresholds;
  std::int32_t moveCap = 0;
  // The moves (by the original's number) each special type teaches, first tried first.
  std::vector<std::vector<std::int32_t>> movesBySpecial;
  // The chance in percent of the move found at each rank, for each of the species' 3 specials.
  std::vector<std::array<std::int32_t, 3>> weights;
};

// trn_brain.c: the tamer teaches the partner at the blackboard.
struct BrainCourse {
  Point tamerSpot;
  std::int32_t tamerWalk = 0;
  std::int32_t tamerStand = 0;
  std::int32_t tamerTeach = 0;
  std::int32_t partnerStand = 0;
  std::int32_t partnerLearn = 0;
  std::int32_t teachEveryFrames = 1;
  std::int32_t teachSound = 0;
  std::int32_t learnEveryFrames = 1;
  BrainLearning learning;
};

using CourseDetail =
    std::variant<HpCourse, MpCourse, OffenseCourse, DefenseCourse, SpeedCourse, BrainCourse>;

// A course of the gym: what every course has, and its own choreography.
struct CourseRules {
  // The training course (training.yaml) whose gains it gives.
  std::string id;
  // The machine the partner trains at.
  Point spot;
  std::int32_t cameraSpeed = 0;
  // The session ends after this or on cancel.
  std::int32_t cycleFrames = 1;
  // Frames between the count and leaving.
  std::int32_t restFrames = 0;
  std::int32_t startSound = 0;
  std::int32_t endSound = 0;
  std::int32_t walkAnimation = 0;
  std::int32_t doneAnimation = 0;
  // The roulette symbol that is the course's own, which pays more.
  std::int32_t slotSymbol = 0;
  CourseDetail detail;
};

// trn_hud.c: the stats box after a session.
struct CountRules {
  std::int32_t delayFrames = 0;
  std::int32_t soundSlot = 0;
  std::int32_t sound = 0;
  std::int32_t hpMpMax = 0;
  std::int32_t statMax = 0;
};

// The rate in tenths each line of the roulette pays.
struct SlotPayouts {
  std::int32_t jackpot = 0;
  std::int32_t ownSymbol = 0;
  std::int32_t triple = 0;
  std::int32_t miss = 0;
};

struct SlotSounds {
  std::int32_t start = 0;
  std::int32_t press = 0;
  std::int32_t stop = 0;
  std::int32_t jackpot = 0;
  std::int32_t triple = 0;
  std::int32_t miss = 0;
};

// trn_slots.c: the roulette over a course.
struct SlotRules {
  // The symbols of each of the three reels, all of the same length.
  std::vector<std::vector<std::int32_t>> reels;
  // The assist of each roll of randomLimit(size): 0 none, 1 matches, 2 aims at the jackpot.
  std::vector<std::int32_t> assistByRoll;
  std::int32_t jackpotSymbol = 0;
  // Pixels per frame of a spinning reel, and the height of a cell.
  std::int32_t spinSpeed = 0;
  std::int32_t cellPixels = 1;
  // How far a reel that is about to stop is drawn past its cell.
  std::int32_t settlePixels = 0;
  // How long the line shows before the session goes on without a press.
  std::int32_t resultFrames = 0;
  // The gym's sound slot.
  std::int32_t soundSlot = 0;
  SlotPayouts payouts;
  SlotSounds sounds;
};

struct GymRules {
  // The map the gym works on.
  std::string map;
  // The training place whose gains the courses give.
  std::string place;
  // The story variable that picks the course, and the course of each of its values.
  std::int32_t courseVariable = 0;
  std::vector<std::string> courseByChoice;
  // The story variable that enables the roulette.
  std::int32_t slotVariable = 0;
  std::string manualItem;
  // The sound slot of the courses' sounds.
  std::int32_t soundSlot = 0;
  std::int32_t cloudScatter = 1;
  CountRules count;
  SlotRules slots;
  std::vector<CourseRules> courses;

  [[nodiscard]] const CourseRules* findCourse(std::string_view id) const;
  [[nodiscard]] const CourseRules* courseOfChoice(std::int64_t choice) const;
};

// Reads the gym section; errors name the file, line and column.
core::Result<GymRules> readGymRules(const ruleset::Section& section);

// The gym rules of a ruleset; none (no course) when it has no gym section.
core::Result<GymRules> readGymRules(const ruleset::Ruleset& ruleset);

ruleset::SectionCheck gymSectionCheck();

} // namespace dwe::game::gym
