#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/gym/gym_rules.hpp>

#include <algorithm>
#include <array>
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

ObjectGroup readGroup(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "map objects", {"first", "count"})) {
    return {};
  }
  return {.first = reader.integer<std::int32_t>(reader.field(node, "first")),
          .count = reader.integer<std::int32_t>(reader.field(node, "count"))};
}

// Without y, the point is at the partner's height.
Point3 readPoint3(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a point", {"x", "z"}, {"y"})) {
    return {};
  }
  const auto y = reader.optionalInteger<std::int32_t>(node, "y");
  return {.x = reader.integer<std::int32_t>(reader.field(node, "x")),
          .y = y.value_or(0),
          .z = reader.integer<std::int32_t>(reader.field(node, "z")),
          .atPartnerHeight = !y.has_value()};
}

// An optional group of map objects: none when the key is missing.
ObjectGroup readOptionalGroup(YamlReader& reader, const YamlNode& node, std::string_view key);

bool readOptionalFlag(YamlReader& reader, const YamlNode& node, std::string_view key,
                      bool otherwise) {
  const YamlNode* value = node.find(key);
  return value == nullptr ? otherwise : reader.boolean(*value);
}

std::vector<Point3> readPoints3(YamlReader& reader, const YamlNode& node) {
  std::vector<Point3> points;
  for (const YamlNode& point : reader.sequence(node, "the points")) {
    points.push_back(readPoint3(reader, point));
  }
  return points;
}

std::int32_t integerOf(YamlReader& reader, const YamlNode& node, std::string_view key) {
  return reader.integer<std::int32_t>(reader.field(node, key));
}

ObjectGroup readOptionalGroup(YamlReader& reader, const YamlNode& node, std::string_view key) {
  const YamlNode* group = node.find(key);
  return group == nullptr ? ObjectGroup{} : readGroup(reader, *group);
}

HpCourse readHp(YamlReader& reader, const YamlNode& node) {
  HpCourse course;
  if (!reader.expectKeys(node, "the hp course",
                         {"facing", "objects", "work_animation", "beat_frames", "beat_move",
                          "jitter", "beat_sound_every", "beat_sound", "return_move", "clouds"},
                         {"arrival_sound", "shown"})) {
    return course;
  }
  course.arrivalSound = readOptionalFlag(reader, node, "arrival_sound", true);
  course.shown = readOptionalGroup(reader, node, "shown");
  course.facing = integerOf(reader, node, "facing");
  course.objects = readGroup(reader, reader.field(node, "objects"));
  course.workAnimation = integerOf(reader, node, "work_animation");
  course.beatFrames = readPositive(reader, reader.field(node, "beat_frames"));
  course.beatMove = readMove(reader, reader.field(node, "beat_move"));
  course.jitter = integerOf(reader, node, "jitter");
  course.beatSoundEvery = readPositive(reader, reader.field(node, "beat_sound_every"));
  course.beatSound = integerOf(reader, node, "beat_sound");
  course.returnMove = readMove(reader, reader.field(node, "return_move"));
  for (const YamlNode& cloud : reader.sequence(reader.field(node, "clouds"), "the clouds")) {
    course.clouds.push_back(readCloud(reader, cloud));
  }
  return course;
}

MpCourse readMp(YamlReader& reader, const YamlNode& node) {
  MpCourse course;
  if (!reader.expectKeys(node, "the mp course",
                         {"waypoint", "facing", "objects", "hidden", "shown", "head_lift",
                          "work_animation", "leave_animation"},
                         {"loop_sound", "restart_work"})) {
    return course;
  }
  course.restartWork = readOptionalFlag(reader, node, "restart_work", false);
  course.waypoint = readPoint(reader, reader.field(node, "waypoint"));
  course.facing = integerOf(reader, node, "facing");
  course.objects = readGroup(reader, reader.field(node, "objects"));
  course.hidden = readGroup(reader, reader.field(node, "hidden"));
  course.shown = readGroup(reader, reader.field(node, "shown"));
  course.headLift = integerOf(reader, node, "head_lift");
  course.workAnimation = integerOf(reader, node, "work_animation");
  course.loopSound = reader.optionalInteger<std::int32_t>(node, "loop_sound");
  course.leaveAnimation = integerOf(reader, node, "leave_animation");
  return course;
}

SpeciesAnimation readSpeciesAnimation(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a species' punch", {"animation", "frames"})) {
    return {};
  }
  return {.animation = integerOf(reader, node, "animation"),
          .frames = readPositive(reader, reader.field(node, "frames"))};
}

OffenseSwap readSwap(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(
          node, "the offense swap",
          {"frames", "hit_shown", "hit_hidden", "hit_restarted", "undo_shown", "undo_hidden"})) {
    return {};
  }
  return {.hitShown = readGroup(reader, reader.field(node, "hit_shown")),
          .hitHidden = readGroup(reader, reader.field(node, "hit_hidden")),
          .hitRestarted = readGroup(reader, reader.field(node, "hit_restarted")),
          .undoShown = readGroup(reader, reader.field(node, "undo_shown")),
          .undoHidden = readGroup(reader, reader.field(node, "undo_hidden")),
          .frames = readPositive(reader, reader.field(node, "frames"))};
}

void readSwing(YamlReader& reader, const YamlNode& node, OffenseCourse& course);

OffenseCourse readOffense(YamlReader& reader, const YamlNode& node) {
  OffenseCourse course;
  // The bag swings, or the hit swaps objects (the other spots).
  const bool swaps = node.isMapping() && node.find("swap") != nullptr;
  const bool ok =
      swaps ? reader.expectKeys(
                  node, "the offense course",
                  {"facing", "objects", "particles", "hit_sound", "leave_animation", "swap"})
            : reader.expectKeys(node, "the offense course",
                                {"facing", "objects", "particles", "hit_sound", "recoil_frames",
                                 "recoil_move", "rebound_frames", "swing_frames", "swing_base",
                                 "swing_divisor", "leave_animation"});
  if (!ok) {
    return course;
  }
  course.facing = integerOf(reader, node, "facing");
  course.objects = readGroup(reader, reader.field(node, "objects"));
  course.particles = readPoints3(reader, reader.field(node, "particles"));
  course.hitSound = integerOf(reader, node, "hit_sound");
  if (swaps) {
    course.swap = readSwap(reader, reader.field(node, "swap"));
  } else {
    readSwing(reader, node, course);
  }
  course.leaveAnimation = integerOf(reader, node, "leave_animation");
  return course;
}

void readPunches(YamlReader& reader, const YamlNode& node, GymRules& rules) {
  if (!reader.expectKeys(node, "the punches", {"default", "by_species"})) {
    return;
  }
  rules.defaultPunch = readSpeciesAnimation(reader, reader.field(node, "default"));
  const YamlNode& bySpecies = reader.field(node, "by_species");
  if (!bySpecies.isMapping()) {
    reader.fail(bySpecies, "the punches by species must be a mapping");
    return;
  }
  for (const core::YamlEntry& entry : bySpecies.entries()) {
    rules.punches.emplace_back(entry.key, readSpeciesAnimation(reader, entry.value));
  }
}

void readSwing(YamlReader& reader, const YamlNode& node, OffenseCourse& course) {
  course.recoilFrames = integerOf(reader, node, "recoil_frames");
  course.recoilMove = readMove(reader, reader.field(node, "recoil_move"));
  course.reboundFrames = integerOf(reader, node, "rebound_frames");
  course.swingFrames = readPositive(reader, reader.field(node, "swing_frames"));
  course.swingBase = integerOf(reader, node, "swing_base");
  const YamlNode& divisor = reader.field(node, "swing_divisor");
  if (reader.expectKeys(divisor, "the swing divisor", {"x", "y"})) {
    course.swingDivisor = {.x = readPositive(reader, reader.field(divisor, "x")),
                           .y = readPositive(reader, reader.field(divisor, "y"))};
  }
}

DefenseCourse readDefense(YamlReader& reader, const YamlNode& node) {
  DefenseCourse course;
  if (!reader.expectKeys(node, "the defense course",
                         {"facing", "objects", "particles", "loop_sound", "work_animation", "push",
                          "dust_behind", "swings"})) {
    return course;
  }
  course.facing = integerOf(reader, node, "facing");
  course.objects = readGroup(reader, reader.field(node, "objects"));
  course.particles = readPoints3(reader, reader.field(node, "particles"));
  course.loopSound = integerOf(reader, node, "loop_sound");
  course.workAnimation = integerOf(reader, node, "work_animation");
  course.push = integerOf(reader, node, "push");
  course.dustBehind = integerOf(reader, node, "dust_behind");
  for (const YamlNode& swing : reader.sequence(reader.field(node, "swings"), "the swings")) {
    if (!reader.expectKeys(swing, "a swing", {"dx", "limit", "push"})) {
      continue;
    }
    const auto dx = integerOf(reader, swing, "dx");
    if (reader.ok() && dx == 0) {
      reader.fail(swing, "a swing must move");
    }
    course.swings.push_back({.dx = dx,
                             .limit = integerOf(reader, swing, "limit"),
                             .push = reader.boolean(reader.field(swing, "push"))});
  }
  if (reader.ok() && course.swings.empty()) {
    reader.fail(node, "the defense course needs a swing");
  }
  return course;
}

SpeedCourse readSpeed(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "the speed course",
                         {"facing", "run_animation", "lap_frames", "restart"})) {
    return {};
  }
  SpeedCourse course{.facing = integerOf(reader, node, "facing"),
                     .runAnimation = integerOf(reader, node, "run_animation"),
                     .lapFrames = readPositive(reader, reader.field(node, "lap_frames"))};
  // restart: {x: ...} or {z: ...}: the one coordinate a lap starts again at.
  const YamlNode& restart = reader.field(node, "restart");
  if (reader.expectKeys(restart, "the lap's restart", {}, {"x", "z"})) {
    const auto x = reader.optionalInteger<std::int32_t>(restart, "x");
    const auto z = reader.optionalInteger<std::int32_t>(restart, "z");
    if (x.has_value() == z.has_value()) {
      reader.fail(restart, "a lap starts again at x or at z");
    }
    course.restartAxis = z ? Axis::Z : Axis::X;
    course.restart = z.value_or(x.value_or(0));
  }
  return course;
}

TreadmillCourse readTreadmill(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(
          node, "the treadmill course",
          {"waypoint", "facing", "run_animation", "loop_sound", "shown", "leave_spot"})) {
    return {};
  }
  return {.waypoint = readPoint(reader, reader.field(node, "waypoint")),
          .facing = integerOf(reader, node, "facing"),
          .runAnimation = integerOf(reader, node, "run_animation"),
          .loopSound = integerOf(reader, node, "loop_sound"),
          .shown = readGroup(reader, reader.field(node, "shown")),
          .leaveSpot = readPoint(reader, reader.field(node, "leave_spot"))};
}

DefensePulseCourse readDefensePulse(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "the defense pulse course",
                         {"facing", "hidden", "shown", "particles", "hit_sound", "work_animation",
                          "hit_after_frames", "recover_frames"})) {
    return {};
  }
  return {.facing = integerOf(reader, node, "facing"),
          .hidden = readGroup(reader, reader.field(node, "hidden")),
          .shown = readGroup(reader, reader.field(node, "shown")),
          .particles = readPoints3(reader, reader.field(node, "particles")),
          .hitSound = integerOf(reader, node, "hit_sound"),
          .workAnimation = integerOf(reader, node, "work_animation"),
          .hitAfterFrames = readPositive(reader, reader.field(node, "hit_after_frames")),
          .recoverFrames = readPositive(reader, reader.field(node, "recover_frames"))};
}

DefenseDropCourse readDefenseDrop(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "the defense drop course",
                         {"work_animation", "falling", "drop", "until_row", "hit", "particles",
                          "hit_sound", "recover_frames"})) {
    return {};
  }
  DefenseDropCourse course{.workAnimation = integerOf(reader, node, "work_animation"),
                           .falling = readGroup(reader, reader.field(node, "falling")),
                           .drop = integerOf(reader, node, "drop"),
                           .untilRow = integerOf(reader, node, "until_row"),
                           .hit = readGroup(reader, reader.field(node, "hit")),
                           .particles = readPoints3(reader, reader.field(node, "particles")),
                           .hitSound = integerOf(reader, node, "hit_sound"),
                           .recoverFrames =
                               readPositive(reader, reader.field(node, "recover_frames"))};
  if (reader.ok() && (course.drop <= 0 || course.falling.count < 1)) {
    reader.fail(node, "the drop needs an object that falls");
  }
  return course;
}

std::vector<std::int32_t> readIntegers(YamlReader& reader, const YamlNode& node,
                                       std::string_view what) {
  std::vector<std::int32_t> values;
  for (const YamlNode& value : reader.sequence(node, what)) {
    values.push_back(reader.integer<std::int32_t>(value));
  }
  return values;
}

BrainLearning readLearning(YamlReader& reader, const YamlNode& node) {
  BrainLearning learning;
  if (!reader.expectKeys(
          node, "the brains learning",
          {"order_thresholds", "move_thresholds", "move_cap", "moves_by_special", "weights"})) {
    return learning;
  }
  learning.orderThresholds =
      readIntegers(reader, reader.field(node, "order_thresholds"), "the order thresholds");
  learning.moveThresholds =
      readIntegers(reader, reader.field(node, "move_thresholds"), "the move thresholds");
  learning.moveCap = integerOf(reader, node, "move_cap");
  for (const YamlNode& moves :
       reader.sequence(reader.field(node, "moves_by_special"), "the moves by special")) {
    learning.movesBySpecial.push_back(readIntegers(reader, moves, "a special's moves"));
  }
  for (const YamlNode& row : reader.sequence(reader.field(node, "weights"), "the weights")) {
    std::array<std::int32_t, 3> weights{};
    std::size_t slot = 0;
    for (const YamlNode& weight : reader.sequence(row, "a rank's weights", weights.size())) {
      weights.at(slot++) = reader.integer<std::int32_t>(weight);
    }
    learning.weights.push_back(weights);
  }
  for (const auto& moves : learning.movesBySpecial) {
    if (reader.ok() && moves.size() > learning.weights.size()) {
      reader.fail(node, "every move of a special needs a rank in the weights");
    }
  }
  return learning;
}

BrainCourse readBrain(YamlReader& reader, const YamlNode& node, const BrainLearning& learning) {
  BrainCourse course;
  course.learning = learning;
  // The tamer teaches, or the map's first character does (teacher).
  const bool teacher = node.isMapping() && node.find("teacher") != nullptr;
  const bool ok =
      teacher ? reader.expectKeys(node, "the brain course",
                                  {"tamer_spot", "tamer_walk", "tamer_stand", "partner_stand",
                                   "partner_learn", "learn_every_frames", "teacher"})
              : reader.expectKeys(node, "the brain course",
                                  {"tamer_spot", "tamer_walk", "tamer_stand", "tamer_teach",
                                   "partner_stand", "partner_learn", "teach_every_frames",
                                   "teach_sound", "learn_every_frames"});
  if (!ok) {
    return course;
  }
  course.tamerSpot = readPoint(reader, reader.field(node, "tamer_spot"));
  course.tamerWalk = integerOf(reader, node, "tamer_walk");
  course.tamerStand = integerOf(reader, node, "tamer_stand");
  course.partnerStand = integerOf(reader, node, "partner_stand");
  course.partnerLearn = integerOf(reader, node, "partner_learn");
  if (teacher) {
    const YamlNode& block = reader.field(node, "teacher");
    if (reader.expectKeys(block, "the teacher",
                          {"animation", "sound", "sound_every_frames", "done_animation"})) {
      course.teacher = BrainTeacher{
          .animation = integerOf(reader, block, "animation"),
          .sound = integerOf(reader, block, "sound"),
          .soundEveryFrames = readPositive(reader, reader.field(block, "sound_every_frames")),
          .doneAnimation = integerOf(reader, block, "done_animation")};
    }
  } else {
    course.tamerTeach = integerOf(reader, node, "tamer_teach");
    course.teachEveryFrames = readPositive(reader, reader.field(node, "teach_every_frames"));
    course.teachSound = integerOf(reader, node, "teach_sound");
  }
  course.learnEveryFrames = readPositive(reader, reader.field(node, "learn_every_frames"));
  return course;
}

// The kinds of choreography, by the key that holds each.
constexpr std::array<std::string_view, 9> CourseKinds{
    "hp",           "mp",       "offense", "defense", "speed", "brain", "defense_pulse",
    "defense_drop", "treadmill"};

// The course's own choreography, under the key that names its kind.
CourseDetail readDetail(YamlReader& reader, const YamlNode& node, std::string_view kind,
                        const BrainLearning& learning) {
  const YamlNode& detail = reader.field(node, kind);
  if (kind == "hp") {
    return readHp(reader, detail);
  }
  if (kind == "mp") {
    return readMp(reader, detail);
  }
  if (kind == "offense") {
    return readOffense(reader, detail);
  }
  if (kind == "defense") {
    return readDefense(reader, detail);
  }
  if (kind == "speed") {
    return readSpeed(reader, detail);
  }
  if (kind == "brain") {
    return readBrain(reader, detail, learning);
  }
  if (kind == "defense_pulse") {
    return readDefensePulse(reader, detail);
  }
  if (kind == "defense_drop") {
    return readDefenseDrop(reader, detail);
  }
  return readTreadmill(reader, detail);
}

// The site's map, place and practice belong to each of its courses.
struct Site {
  std::string map;
  std::string place;
  bool countsPractice = true;
  const BrainLearning* learning = nullptr;
};

CourseRules readCourse(YamlReader& reader, const YamlNode& node, const Site& site) {
  CourseRules course;
  course.map = site.map;
  course.place = site.place;
  course.countsPractice = site.countsPractice;
  if (!reader.expectKeys(node, "a gym course",
                         {"id", "choice", "spot", "camera_speed", "cycle_frames", "rest_frames",
                          "sounds", "animations", "slot_symbol"},
                         {"gains", "machine", "hp", "mp", "offense", "defense", "speed", "brain",
                          "defense_pulse", "defense_drop", "treadmill"})) {
    return course;
  }
  course.id = reader.text(reader.field(node, "id"));
  course.choice = integerOf(reader, node, "choice");
  const YamlNode* gains = node.find("gains");
  course.gains = gains != nullptr ? reader.text(*gains) : course.id;
  course.spot = readPoint(reader, reader.field(node, "spot"));
  course.cameraSpeed = integerOf(reader, node, "camera_speed");
  course.cycleFrames = readPositive(reader, reader.field(node, "cycle_frames"));
  course.restFrames = integerOf(reader, node, "rest_frames");
  course.slotSymbol = integerOf(reader, node, "slot_symbol");
  if (const YamlNode* machine = node.find("machine"); machine != nullptr) {
    course.machine = readPoint(reader, *machine);
  }
  const YamlNode& sounds = reader.field(node, "sounds");
  if (reader.expectKeys(sounds, "the course's sounds", {"end"}, {"start"})) {
    course.startSound = reader.optionalInteger<std::int32_t>(sounds, "start");
    course.endSound = integerOf(reader, sounds, "end");
  }
  const YamlNode& animations = reader.field(node, "animations");
  if (reader.expectKeys(animations, "the course's animations", {"walk", "done"})) {
    course.walkAnimation = integerOf(reader, animations, "walk");
    course.doneAnimation = integerOf(reader, animations, "done");
  }
  // One kind of choreography, and only one.
  std::string_view kind;
  std::int32_t kinds = 0;
  for (const std::string_view candidate : CourseKinds) {
    if (node.find(candidate) != nullptr) {
      kind = candidate;
      ++kinds;
    }
  }
  if (reader.ok() && kinds != 1) {
    reader.fail(node, "a course has one choreography: hp, mp, offense, defense, speed, brain, "
                      "defense_pulse, defense_drop or treadmill");
  }
  if (reader.ok()) {
    course.detail = readDetail(reader, node, kind, *site.learning);
  }
  return course;
}

void readSite(YamlReader& reader, const YamlNode& node, const BrainLearning& learning,
              std::vector<CourseRules>& courses) {
  if (!reader.expectKeys(node, "a training site", {"map", "place", "courses"},
                         {"counts_practice"})) {
    return;
  }
  const Site site{.map = reader.text(reader.field(node, "map")),
                  .place = reader.text(reader.field(node, "place")),
                  .countsPractice = readOptionalFlag(reader, node, "counts_practice", true),
                  .learning = &learning};
  const std::size_t first = courses.size();
  for (const YamlNode& course : reader.sequence(reader.field(node, "courses"), "the courses")) {
    CourseRules read = readCourse(reader, course, site);
    for (std::size_t other = first; reader.ok() && other < courses.size(); ++other) {
      if (courses[other].id == read.id || courses[other].choice == read.choice) {
        reader.fail(course, "a site's courses need their own id and choice");
      }
    }
    courses.push_back(std::move(read));
  }
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

SlotRules readSlots(YamlReader& reader, const YamlNode& node) {
  SlotRules slots;
  if (!reader.expectKeys(node, "the roulette",
                         {"reels", "assist_by_roll", "jackpot_symbol", "spin_speed", "cell_pixels",
                          "settle_pixels", "result_frames", "payouts", "sounds"})) {
    return slots;
  }
  for (const YamlNode& reel : reader.sequence(reader.field(node, "reels"), "the reels", 3)) {
    slots.reels.push_back(readIntegers(reader, reel, "a reel's symbols"));
  }
  if (reader.ok() &&
      (slots.reels.front().empty() || std::ranges::any_of(slots.reels, [&](const auto& reel) {
         return reel.size() != slots.reels.front().size();
       }))) {
    reader.fail(node, "the reels need the same number of symbols, at least one");
  }
  slots.assistByRoll =
      readIntegers(reader, reader.field(node, "assist_by_roll"), "the assists by roll");
  if (reader.ok() && (slots.assistByRoll.empty() ||
                      std::ranges::any_of(slots.assistByRoll, [](std::int32_t assist) {
                        return assist < 0 || assist > 2;
                      }))) {
    reader.fail(node, "the assists by roll are 0, 1 or 2, at least one");
  }
  slots.jackpotSymbol = integerOf(reader, node, "jackpot_symbol");
  slots.spinSpeed = readPositive(reader, reader.field(node, "spin_speed"));
  slots.cellPixels = readPositive(reader, reader.field(node, "cell_pixels"));
  slots.settlePixels = integerOf(reader, node, "settle_pixels");
  slots.resultFrames = integerOf(reader, node, "result_frames");
  const YamlNode& payouts = reader.field(node, "payouts");
  if (reader.expectKeys(payouts, "the roulette's payouts",
                        {"jackpot", "own_symbol", "triple", "miss"})) {
    slots.payouts = {.jackpot = integerOf(reader, payouts, "jackpot"),
                     .ownSymbol = integerOf(reader, payouts, "own_symbol"),
                     .triple = integerOf(reader, payouts, "triple"),
                     .miss = integerOf(reader, payouts, "miss")};
  }
  const YamlNode& sounds = reader.field(node, "sounds");
  if (reader.expectKeys(sounds, "the roulette's sounds",
                        {"start", "press", "stop", "jackpot", "triple", "miss"})) {
    slots.sounds = {.start = integerOf(reader, sounds, "start"),
                    .press = integerOf(reader, sounds, "press"),
                    .stop = integerOf(reader, sounds, "stop"),
                    .jackpot = integerOf(reader, sounds, "jackpot"),
                    .triple = integerOf(reader, sounds, "triple"),
                    .miss = integerOf(reader, sounds, "miss")};
  }
  return slots;
}

} // namespace

const SpeciesAnimation& GymRules::punchOf(std::string_view species) const {
  const auto found =
      std::ranges::find(punches, species, &std::pair<std::string, SpeciesAnimation>::first);
  return found == punches.end() ? defaultPunch : found->second;
}

const CourseRules* GymRules::findCourse(std::string_view map, std::string_view id) const {
  const auto found = std::ranges::find_if(
      courses, [&](const CourseRules& course) { return course.map == map && course.id == id; });
  return found == courses.end() ? nullptr : &*found;
}

const CourseRules* GymRules::courseOfChoice(std::string_view map, std::int64_t choice) const {
  const auto found = std::ranges::find_if(courses, [&](const CourseRules& course) {
    return course.map == map && course.choice == choice;
  });
  return found == courses.end() ? nullptr : &*found;
}

bool GymRules::hasMap(std::string_view map) const {
  return std::ranges::contains(courses, map, &CourseRules::map);
}

core::Result<GymRules> readGymRules(const ruleset::Section& section) {
  YamlReader reader;
  GymRules rules;
  const YamlNode& node = section.node;
  if (!reader.expectKeys(node, SectionWhat,
                         {"course_variable", "slot_variable", "manual_item", "sound_slot",
                          "cloud_scatter", "watch_animation", "count", "slots", "punches",
                          "brain_learning", "sites"})) {
    return std::unexpected{reader.error(section.file)};
  }
  rules.courseVariable = reader.integer<std::int32_t>(reader.field(node, "course_variable"));
  rules.slotVariable = reader.integer<std::int32_t>(reader.field(node, "slot_variable"));
  rules.manualItem = reader.text(reader.field(node, "manual_item"));
  rules.soundSlot = reader.integer<std::int32_t>(reader.field(node, "sound_slot"));
  rules.cloudScatter = readPositive(reader, reader.field(node, "cloud_scatter"));
  rules.watchAnimation = reader.integer<std::int32_t>(reader.field(node, "watch_animation"));
  rules.count = readCount(reader, reader.field(node, "count"));
  rules.slots = readSlots(reader, reader.field(node, "slots"));
  rules.slots.soundSlot = rules.soundSlot;
  readPunches(reader, reader.field(node, "punches"), rules);
  // trn_reward.c: TRN_awardBrainsTrainingGains: every brains course teaches the same way.
  const BrainLearning learning = readLearning(reader, reader.field(node, "brain_learning"));
  for (const YamlNode& site : reader.sequence(reader.field(node, "sites"), "the sites")) {
    readSite(reader, site, learning, rules.courses);
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
