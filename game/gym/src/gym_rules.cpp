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

Point3 readPoint3(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a point", {"x", "y", "z"})) {
    return {};
  }
  return {.x = reader.integer<std::int32_t>(reader.field(node, "x")),
          .y = reader.integer<std::int32_t>(reader.field(node, "y")),
          .z = reader.integer<std::int32_t>(reader.field(node, "z"))};
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

HpCourse readHp(YamlReader& reader, const YamlNode& node) {
  HpCourse course;
  if (!reader.expectKeys(node, "the hp course",
                         {"facing", "objects", "work_animation", "beat_frames", "beat_move",
                          "jitter", "beat_sound_every", "beat_sound", "return_move", "clouds"})) {
    return course;
  }
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
                          "work_animation", "loop_sound", "leave_animation"})) {
    return course;
  }
  course.waypoint = readPoint(reader, reader.field(node, "waypoint"));
  course.facing = integerOf(reader, node, "facing");
  course.objects = readGroup(reader, reader.field(node, "objects"));
  course.hidden = readGroup(reader, reader.field(node, "hidden"));
  course.shown = readGroup(reader, reader.field(node, "shown"));
  course.headLift = integerOf(reader, node, "head_lift");
  course.workAnimation = integerOf(reader, node, "work_animation");
  course.loopSound = integerOf(reader, node, "loop_sound");
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

OffenseCourse readOffense(YamlReader& reader, const YamlNode& node) {
  OffenseCourse course;
  if (!reader.expectKeys(node, "the offense course",
                         {"facing", "objects", "particles", "hit_sound", "recoil_frames",
                          "recoil_move", "rebound_frames", "swing_frames", "swing_base",
                          "swing_divisor", "leave_animation", "punch", "punch_by_species"})) {
    return course;
  }
  course.facing = integerOf(reader, node, "facing");
  course.objects = readGroup(reader, reader.field(node, "objects"));
  course.particles = readPoints3(reader, reader.field(node, "particles"));
  course.hitSound = integerOf(reader, node, "hit_sound");
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
  course.leaveAnimation = integerOf(reader, node, "leave_animation");
  course.defaultAnimation = readSpeciesAnimation(reader, reader.field(node, "punch"));
  const YamlNode& bySpecies = reader.field(node, "punch_by_species");
  if (!bySpecies.isMapping()) {
    reader.fail(bySpecies, "the punches by species must be a mapping");
  } else {
    for (const core::YamlEntry& entry : bySpecies.entries()) {
      course.speciesAnimations.emplace_back(entry.key, readSpeciesAnimation(reader, entry.value));
    }
  }
  return course;
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
                         {"facing", "run_animation", "lap_frames", "restart_x"})) {
    return {};
  }
  return {.facing = integerOf(reader, node, "facing"),
          .runAnimation = integerOf(reader, node, "run_animation"),
          .lapFrames = readPositive(reader, reader.field(node, "lap_frames")),
          .restartX = integerOf(reader, node, "restart_x")};
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

BrainCourse readBrain(YamlReader& reader, const YamlNode& node) {
  BrainCourse course;
  if (!reader.expectKeys(node, "the brain course",
                         {"tamer_spot", "tamer_walk", "tamer_stand", "tamer_teach", "partner_stand",
                          "partner_learn", "teach_every_frames", "teach_sound",
                          "learn_every_frames", "learning"})) {
    return course;
  }
  course.tamerSpot = readPoint(reader, reader.field(node, "tamer_spot"));
  course.tamerWalk = integerOf(reader, node, "tamer_walk");
  course.tamerStand = integerOf(reader, node, "tamer_stand");
  course.tamerTeach = integerOf(reader, node, "tamer_teach");
  course.partnerStand = integerOf(reader, node, "partner_stand");
  course.partnerLearn = integerOf(reader, node, "partner_learn");
  course.teachEveryFrames = readPositive(reader, reader.field(node, "teach_every_frames"));
  course.teachSound = integerOf(reader, node, "teach_sound");
  course.learnEveryFrames = readPositive(reader, reader.field(node, "learn_every_frames"));
  course.learning = readLearning(reader, reader.field(node, "learning"));
  return course;
}

// The course's own choreography, under the key that names its kind.
CourseDetail readDetail(YamlReader& reader, const YamlNode& node, std::string_view id) {
  if (const YamlNode* hp = node.find("hp"); id == "hp" && hp != nullptr) {
    return readHp(reader, *hp);
  }
  if (const YamlNode* mp = node.find("mp"); id == "mp" && mp != nullptr) {
    return readMp(reader, *mp);
  }
  if (const YamlNode* offense = node.find("offense"); id == "offense" && offense != nullptr) {
    return readOffense(reader, *offense);
  }
  if (const YamlNode* defense = node.find("defense"); id == "defense" && defense != nullptr) {
    return readDefense(reader, *defense);
  }
  if (const YamlNode* speed = node.find("speed"); id == "speed" && speed != nullptr) {
    return readSpeed(reader, *speed);
  }
  if (const YamlNode* brain = node.find("brain"); id == "brain" && brain != nullptr) {
    return readBrain(reader, *brain);
  }
  reader.fail(node, "a course needs the choreography of its id (hp, mp, offense, defense, "
                    "speed or brain) and no other");
  return HpCourse{};
}

CourseRules readCourse(YamlReader& reader, const YamlNode& node) {
  CourseRules course;
  if (!reader.expectKeys(node, "a gym course",
                         {"id", "spot", "camera_speed", "cycle_frames", "rest_frames", "sounds",
                          "animations", "slot_symbol", "machine"},
                         {"hp", "mp", "offense", "defense", "speed", "brain"})) {
    return course;
  }
  course.id = reader.text(reader.field(node, "id"));
  course.spot = readPoint(reader, reader.field(node, "spot"));
  course.cameraSpeed = integerOf(reader, node, "camera_speed");
  course.cycleFrames = readPositive(reader, reader.field(node, "cycle_frames"));
  course.restFrames = integerOf(reader, node, "rest_frames");
  course.slotSymbol = integerOf(reader, node, "slot_symbol");
  course.machine = readPoint(reader, reader.field(node, "machine"));
  const YamlNode& sounds = reader.field(node, "sounds");
  if (reader.expectKeys(sounds, "the course's sounds", {"start", "end"})) {
    course.startSound = integerOf(reader, sounds, "start");
    course.endSound = integerOf(reader, sounds, "end");
  }
  const YamlNode& animations = reader.field(node, "animations");
  if (reader.expectKeys(animations, "the course's animations", {"walk", "done"})) {
    course.walkAnimation = integerOf(reader, animations, "walk");
    course.doneAnimation = integerOf(reader, animations, "done");
  }
  // Only the id's own choreography: another kind's key is a mistake.
  std::int32_t kinds = 0;
  for (const std::string_view kind : {"hp", "mp", "offense", "defense", "speed", "brain"}) {
    kinds += node.find(kind) != nullptr ? 1 : 0;
  }
  if (reader.ok() && kinds != 1) {
    reader.fail(node, "a course has the choreography of its id and no other");
  }
  if (reader.ok()) {
    course.detail = readDetail(reader, node, course.id);
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

const SpeciesAnimation& OffenseCourse::animationOf(std::string_view species) const {
  const auto found = std::ranges::find(speciesAnimations, species,
                                       &std::pair<std::string, SpeciesAnimation>::first);
  return found == speciesAnimations.end() ? defaultAnimation : found->second;
}

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
                          "manual_item", "sound_slot", "cloud_scatter", "count", "slots",
                          "courses"})) {
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
  rules.slots = readSlots(reader, reader.field(node, "slots"));
  rules.slots.soundSlot = rules.soundSlot;
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
