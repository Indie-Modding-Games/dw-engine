#include <dwe/assets/clip_motion.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/gym/course_flow.hpp>
#include <dwe/game/gym/gym_rules.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/scene/world.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/gym_adapter.hpp>
#include <dwe/session/gym_field_ports.hpp>

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "adapter_fixture.hpp"
#include "fixture.hpp"
#include "session_fixture.hpp"

// The training check's unit level (docs/guides/comprobar-entrenamiento.md): every course of the
// game pack's gym.yaml runs a whole session on a real field, whose characters' clips all carry them
// a made-up step a frame, through the field's own root motion (Field::moveByClip) and the gym's
// ports on it (session::GymFieldPorts). The partner must go no farther than the original lets it:
// held where the original sets animFlag 2, carried by its clip alone where it does not. A change
// that lets a held clip carry the partner (c684baa6 did, for every scripted clip), or that pushes
// it besides its clip, fails here.

using dwe::game::field::Field;
using dwe::game::field::testing::originalRules;
using dwe::game::field::testing::roomMap;
using dwe::game::gym::CourseRules;
using dwe::game::gym::CourseRun;
using dwe::game::gym::GymInput;
using dwe::game::gym::SessionRecord;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

// The made-up root step of every frame of every clip but its first (the frame a clip starts on
// moves no one, as startAnimation's first frame).
constexpr std::int32_t Step = 10;
// Clip ids 0 to this, every clip of the courses.
constexpr std::int32_t ClipCount = 64;
constexpr std::size_t ClipFrames = 16;
constexpr double Tick = 1.0 / 60.0;
// Long enough for the walk there, the session, the count and the walk back.
constexpr int MaxTicks = 60 * 600;
// The count's box takes a confirm this often, as a player who reads it.
constexpr int ConfirmEvery = 30;
// The record reads whole units each frame: a few frames' rounding.
constexpr std::int32_t Rounding = Step / 2;

std::shared_ptr<const dwe::assets::ClipMotions> movingClips() {
  dwe::assets::ClipMotions clips;
  for (std::int32_t id = 0; id < ClipCount; ++id) {
    dwe::assets::ClipMotion& clip = clips[id];
    clip.steps.assign(ClipFrames, {.forward = Step});
    clip.steps.front() = {};
    clip.repeatFrom = 1;
  }
  return std::make_shared<const dwe::assets::ClipMotions>(std::move(clips));
}

// The ports of the game without its screen: the map's objects are rows kept here, and nothing is
// drawn.
class FieldPorts final : public dwe::session::GymFieldPorts {
public:
  using GymFieldPorts::GymFieldPorts;

  bool animationEnded(dwe::game::gym::Actor actor) override {
    return !anyEntityOf(actor).has_value();
  }
  void storeObjects(const dwe::game::gym::ObjectGroup& group) override {
    stored_.clear();
    for (std::int32_t index = 0; index < group.count; ++index) {
      stored_.push_back(rows_[group.first + index]);
    }
  }
  void restoreObjects(const dwe::game::gym::ObjectGroup& group) override {
    for (std::size_t index = 0; index < stored_.size(); ++index) {
      rows_[group.first + static_cast<std::int32_t>(index)] = stored_.at(index);
    }
    stored_.clear();
  }
  void moveObjects(const dwe::game::gym::ObjectGroup& group,
                   dwe::game::gym::ObjectMove move) override {
    for (std::int32_t index = 0; index < group.count; ++index) {
      rows_[group.first + index] += move.y;
    }
  }
  void showObjects(const dwe::game::gym::ObjectGroup& /*group*/, bool /*shown*/) override {}
  void restartObjects(const dwe::game::gym::ObjectGroup& /*group*/) override {}
  std::optional<std::int32_t> objectRow(std::int32_t object) override { return rows_[object]; }
  std::optional<std::int32_t> partnerHeadRow() override { return std::nullopt; }
  void addCloud(dwe::game::gym::Point /*at*/) override {}
  void addParticle(dwe::game::gym::Point3 /*at*/) override {}
  void removeClouds() override {}

protected:
  float clipGroundSpeed(dwe::scene::Entity /*entity*/, std::uint32_t /*clip*/) const override {
    return 0.0F;
  }

private:
  std::map<std::int32_t, std::int32_t> rows_;
  std::vector<std::int32_t> stored_;
};

// A whole session of a course of the game pack, on a field of its own.
struct Played {
  SessionRecord record;
  bool finished = false;
  // The ground the player covered during the session.
  float playerPath = 0.0F;
};

const CourseRules& courseOf(const dwe::game::gym::GymRules& rules, std::string_view map,
                            std::string_view id) {
  const CourseRules* found = rules.findCourse(map, id);
  REQUIRE_MESSAGE(found != nullptr, map, " has no course ", id);
  return *found;
}

Played play(std::string_view map, std::string_view id) {
  const TestPacks packs;
  auto gym = std::make_unique<dwe::session::GymAdapter>();
  const dwe::session::GymAdapter& adapter = *gym;
  std::vector<std::unique_ptr<dwe::session::SessionSystem>> systems;
  systems.push_back(std::move(gym));
  const auto session = newGameWith(packs, std::move(systems));
  const dwe::game::gym::GymRules& rules = adapter.rules();
  const CourseRules& course = courseOf(rules, map, id);

  dwe::scene::World world;
  Field field{world, originalRules()};
  // The player stands a little off the course's spot, as at a gym's sign.
  const glm::vec3 spot = dwe::session::gymWorldOf(course.spot, 0.0F);
  field.enterMapAt(roomMap(std::string{map}), {.position = spot + glm::vec3{0.0F, 0.0F, 300.0F}});
  const auto clips = movingClips();
  field.setPartnerClips(dwe::assets::ClipMotions{*clips});
  world.registry().emplace_or_replace<dwe::game::field::CharacterClipMotions>(field.playerEntity(),
                                                                              clips);
  FieldPorts ports{{.session = session.get(), .field = &field, .rules = &rules}};
  CourseRun run{rules, course, false};
  const double frameSeconds = 1.0 / session->clock().rules().framesPerSecond;
  Played played;
  for (int tick = 0; tick < MaxTicks && !run.finished(); ++tick) {
    const bool counting = run.record().award.has_value();
    const bool during = run.record().frames > 0 && !counting;
    const glm::vec3 before = field.player().position;
    run.tick(ports, Tick, frameSeconds, GymInput{.confirm = counting && tick % ConfirmEvery == 0});
    field.tick(static_cast<float>(Tick));
    if (during) {
      glm::vec3 moved = field.player().position - before;
      moved.y = 0.0F;
      played.playerPath += glm::length(moved);
    }
  }
  played.record = run.record();
  played.finished = run.finished();
  return played;
}

// What the original lets a course do, measured with the made-up step: how many frames the session
// lasts, how far the partner may go frame by frame (path) and from where it arrived (offset), and
// the stat the session must raise.
struct Expected {
  std::int32_t frames = 1200;
  std::int32_t minPath = 0;
  std::int32_t maxPath = 0;
  std::int32_t maxOffset = 0;
  dwe::game::partner::Stat gains = dwe::game::partner::Stat::Hp;
  // Whether the player stands where it teaches (trn_brain.c holds its clip too).
  bool playerHeld = false;
};

// A session whose clips carry the partner every frame and nothing else moves it (no animFlag 2
// in the original): at most the clip's step on every frame.
constexpr std::int32_t carried(std::int32_t frames) {
  return frames * Step;
}

void check(std::string_view map, std::string_view id, const Expected& expected) {
  const Played played = play(map, id);
  const SessionRecord& record = played.record;
  INFO(map, " ", id, ": frames ", record.frames, ", path ", record.path, ", offset ",
       record.maxOffset, ", player ", played.playerPath);
  CHECK(played.finished);
  CHECK(record.frames == expected.frames);
  CHECK(record.path >= expected.minPath);
  CHECK(record.path <= expected.maxPath + Rounding);
  CHECK(record.maxOffset <= expected.maxOffset + Rounding);
  if (expected.playerHeld) {
    CHECK(played.playerPath < 1.0F);
  }
  REQUIRE(record.award.has_value());
  CHECK(record.award->gains.at(static_cast<std::size_t>(expected.gains)) > 0);
  for (const std::int32_t gain : record.award->gains) {
    CHECK(gain >= 0);
  }
}

// trn_hp.c, trn2_hp_map99.c, trn2_hp_map107.c: the push holds its clip but for one frame on each
// of the 10 beats (and the first): 11 steps.
constexpr Expected HpPush{.frames = 1200,
                          .maxPath = 11 * Step,
                          .maxOffset = 11 * Step,
                          .gains = dwe::game::partner::Stat::Hp};

} // namespace

using dwe::game::partner::Stat;

TEST_CASE("training check: the town gym's boulder holds the partner but for a step on each beat") {
  check("trai00", "hp", HpPush);
}

TEST_CASE("training check: the town gym's meditation lets the clip carry the partner, no more") {
  // trn_mp.c never sets animFlag 2.
  check("trai00", "mp", {.maxPath = carried(1200), .maxOffset = carried(1200), .gains = Stat::Mp});
}

TEST_CASE("training check: the town gym's punching bag holds every punch but its first frame") {
  // trn_off.c: animFlag 2 on each punch; 22 punches start their clip in a session.
  check("trai00", "offense",
        {.maxPath = 23 * Step, .maxOffset = 23 * Step, .gains = Stat::Offense});
}

TEST_CASE("training check: the town gym's log pushes the partner back as its clip carries it") {
  // trn_def.c never sets animFlag 2; the log's swing pushes the partner back (movePartner), so it
  // stays near its spot. Its session also counts the swings' frames.
  check("trai00", "defense",
        {.frames = 1512, .maxPath = 2 * carried(1512), .maxOffset = 300, .gains = Stat::Defense});
}

TEST_CASE("training check: the town gym's course lets the run clip alone carry the partner") {
  // trn_speed.c never sets animFlag 2: one step a frame, laps aside, never two.
  check("trai00", "speed",
        {.minPath = carried(1200) * 95 / 100,
         .maxPath = carried(1200),
         .maxOffset = 3100,
         .gains = Stat::Speed});
}

TEST_CASE("training check: the town gym's blackboard holds the player, the partner listens") {
  // trn_brain.c holds the player's clip (animFlag 2); the partner's clip carries it.
  check("trai00", "brain",
        {.maxPath = carried(1200),
         .maxOffset = carried(1200),
         .gains = Stat::Brain,
         .playerHeld = true});
}

TEST_CASE("training check: the savanna's punches hold only the first, as trn_off.c instance 6") {
  check("gias09", "offense",
        {.maxPath = carried(1200), .maxOffset = carried(1200), .gains = Stat::Offense});
}

TEST_CASE("training check: the savanna's course lets the run clip alone carry the partner") {
  check("gias09", "speed",
        {.minPath = carried(1200) * 95 / 100,
         .maxPath = carried(1200),
         .maxOffset = 2500,
         .gains = Stat::Speed});
}

TEST_CASE("training check: the foggy wood's teacher teaches, nobody held") {
  // trn_brain.c, instance with a teacher: the player watches, neither clip is held.
  check("mist05", "brain",
        {.maxPath = carried(1200), .maxOffset = carried(1200), .gains = Stat::Brain});
}

TEST_CASE("training check: the ice field's boulder holds the partner but for a step on each beat") {
  check("icsa03", "hp", HpPush);
}

TEST_CASE("training check: the ice field's falling block holds the partner as it takes the hit") {
  // trn2_def_map99.c: only the fall's frames count toward the session's 1200, and the hit holds
  // the clip for its 30 frames; the made-up rows fall from 0, five frames a drop.
  check("icsa03", "defense",
        {.frames = 8371, .maxPath = carried(8371), .maxOffset = 13 * Step, .gains = Stat::Defense});
}

TEST_CASE(
    "training check: the battlefield's boulder holds the partner but for a step on each beat") {
  check("betl03", "hp", HpPush);
}

TEST_CASE("training check: the battlefield's punches hold every punch") {
  // trn2_off.c sets animFlag 2 on every punch frame: one step as each of 20 punches starts.
  check("betl03", "offense",
        {.maxPath = 21 * Step, .maxOffset = 21 * Step, .gains = Stat::Offense});
}

TEST_CASE("training check: the arena's treadmill runs the partner in place") {
  // trn2_reward.c: TRN2_tickSpeedTraining holds the run clip every frame.
  check("betl04", "speed", {.maxPath = 2 * Step, .maxOffset = 2 * Step, .gains = Stat::Speed});
}

TEST_CASE("training check: the arena's pulse guard lets the clip carry the partner, no more") {
  check("betl04", "defense",
        {.maxPath = carried(1201), .maxOffset = carried(1201), .gains = Stat::Defense});
}

TEST_CASE("training check: the dump's meditation lets the clip carry the partner, no more") {
  // trn2_mp.c starts the pose again every frame and never sets animFlag 2.
  check("gomi02", "mp", {.maxPath = carried(1200), .maxOffset = carried(1200), .gains = Stat::Mp});
}

TEST_CASE("training check: every course of the game pack has its check here") {
  const TestPacks packs;
  auto gym = std::make_unique<dwe::session::GymAdapter>();
  const dwe::session::GymAdapter& adapter = *gym;
  std::vector<std::unique_ptr<dwe::session::SessionSystem>> systems;
  systems.push_back(std::move(gym));
  const auto session = newGameWith(packs, std::move(systems));
  std::vector<std::string> courses;
  for (const CourseRules& course : adapter.rules().courses) {
    courses.push_back(course.map + " " + course.id);
  }
  // A new course needs a test above and a route in tests/autotest/training.
  CHECK(courses ==
        std::vector<std::string>{"trai00 hp", "trai00 mp", "trai00 offense", "trai00 defense",
                                 "trai00 speed", "trai00 brain", "gias09 offense", "gias09 speed",
                                 "mist05 brain", "icsa03 hp", "icsa03 defense", "betl03 hp",
                                 "betl03 offense", "betl04 speed", "betl04 defense", "gomi02 mp"});
}
