#include <dwe/game/gym/course_flow.hpp>
#include <dwe/game/gym/growing_box.hpp>
#include <dwe/game/gym/stat_countup.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using dwe::game::gym::Actor;
using dwe::game::gym::Award;
using dwe::game::gym::Axis;
using dwe::game::gym::BrainCourse;
using dwe::game::gym::CourseDetail;
using dwe::game::gym::CourseRules;
using dwe::game::gym::CourseRun;
using dwe::game::gym::DefenseCourse;
using dwe::game::gym::DefenseSwing;
using dwe::game::gym::GymInput;
using dwe::game::gym::GymPorts;
using dwe::game::gym::GymRules;
using dwe::game::gym::MpCourse;
using dwe::game::gym::ObjectGroup;
using dwe::game::gym::ObjectMove;
using dwe::game::gym::Point;
using dwe::game::gym::Point3;
using dwe::game::gym::SpeedCourse;
using dwe::game::gym::TreadmillCourse;

// The field as the course sees it: every walk arrives at once, the partner stays on screen, and an
// award gives one point of speed.
class FakePorts final : public GymPorts {
public:
  void setTraining(bool /*training*/) override {}
  void animate(Actor actor, std::int32_t animation) override {
    if (actor == Actor::Tamer) {
      tamerClips.push_back(animation);
    }
    // startAnimation resets animFlag: a new clip carries its actor again.
    if (actor == Actor::Partner) {
      partnerHeld = false;
    }
  }
  void holdInPlace(Actor actor, bool held) override {
    if (actor == Actor::Partner) {
      partnerHeld = held;
    }
  }
  void face(Actor /*actor*/, std::int32_t /*angle*/) override {}
  bool animationEnded(Actor /*actor*/) override { return false; }
  void faceEachOther() override {}
  void lookAt(Actor actor, Actor target) override {
    if (actor == Actor::Tamer && target == Actor::Partner) {
      ++tamerLooks;
    }
  }
  bool walk(Actor /*actor*/, std::optional<Point> spot) override {
    walkedTo.push_back(spot);
    return true;
  }
  void moveCamera(std::optional<Point> /*spot*/, std::int32_t /*speed*/) override {}
  void playSound(std::int32_t /*slot*/, std::int32_t sound) override { sounds.push_back(sound); }
  std::optional<std::int64_t> loopSound(std::int32_t /*slot*/, std::int32_t /*sound*/) override {
    ++loops;
    playing.push_back(loops);
    return loops;
  }
  void stopSound(std::int64_t handle) override {
    ++stops;
    std::erase(playing, handle);
  }
  void storeObjects(const ObjectGroup& /*group*/) override {}
  void restoreObjects(const ObjectGroup& /*group*/) override { row = 0; }
  void moveObjects(const ObjectGroup& /*group*/, ObjectMove move) override { row += move.y; }
  void showObjects(const ObjectGroup& group, bool shown) override {
    shows.emplace_back(group.first, shown);
  }
  void restartObjects(const ObjectGroup& group) override { restarts.push_back(group.first); }
  std::optional<std::int32_t> objectRow(std::int32_t /*object*/) override { return row; }
  std::optional<std::int32_t> partnerHeadRow() override { return headRow; }
  std::optional<Point> partnerPosition() override { return Point{}; }
  void movePartner(Point /*delta*/) override {}
  void runOn(Actor /*actor*/) override { ++runs; }
  bool partnerOnScreen() override { return onScreen; }
  void placePartner(Axis axis, std::int32_t value) override { placed.emplace_back(axis, value); }
  std::string partnerSpecies() override { return "agumon"; }
  void addCloud(Point /*at*/) override {}
  void addParticle(Point3 /*at*/) override {}
  void removeClouds() override {}
  void closeTextbox() override { ++textboxesClosed; }
  std::int32_t randomBelow(std::int32_t /*limit*/) override { return 0; }
  Award award(const CourseRules& /*course*/, std::int32_t /*slotTenths*/) override {
    ++awards;
    return Award{.before = {}, .gains = {0, 0, 0, 0, 1, 0}};
  }

  int awards = 0;
  int textboxesClosed = 0;
  std::int64_t loops = 0;
  // The looping voices still sounding.
  std::vector<std::int64_t> playing;
  int stops = 0;
  int runs = 0;
  int tamerLooks = 0;
  // Whether the partner's clip plays in place (animFlag 2).
  bool partnerHeld = false;
  std::vector<std::int32_t> tamerClips;
  bool onScreen = true;
  std::optional<std::int32_t> headRow;
  std::int32_t row = 0;
  std::vector<std::int32_t> sounds;
  std::vector<std::optional<Point>> walkedTo;
  std::vector<std::pair<std::int32_t, bool>> shows;
  std::vector<std::int32_t> restarts;
  std::vector<std::pair<Axis, std::int32_t>> placed;
};

GymRules gymRules() {
  GymRules gym;
  gym.count = {.delayFrames = 100, .soundSlot = 0, .sound = 22, .hpMpMax = 9999, .statMax = 999};
  CourseRules speed;
  speed.id = "speed";
  speed.map = "trai00";
  speed.cycleFrames = 1200;
  speed.detail = SpeedCourse{};
  gym.courses.push_back(speed);
  return gym;
}

// Steps whole frames: one tick of one frame's length.
void frames(CourseRun& run, FakePorts& ports, int count, const GymInput& input = {}) {
  for (int frame = 0; frame < count; ++frame) {
    run.tick(ports, 1.0, 1.0, input);
  }
}

} // namespace

TEST_CASE("gym.md cancel: holding the original's cancel ends the session with its gains") {
  const GymRules gym = gymRules();
  FakePorts ports;
  CourseRun run{gym, gym.courses.front(), false};
  frames(run, ports, 10);
  CHECK(ports.awards == 0);
  // trn_speed.c: POLLED_INPUT & CANCEL_BUTTON: the button held, not a new press, ends it.
  frames(run, ports, 1, GymInput{.cancelHeld = true});
  CHECK(ports.awards == 1);
  REQUIRE(run.countup().has_value());
  // Held on into the box, it does not skip the count (trn_hud.c: a new press only).
  frames(run, ports, 5, GymInput{.cancelHeld = true});
  CHECK_FALSE(run.countup()->done());
}

TEST_CASE("gym.md cancel: without cancel the session runs its 1200 frames") {
  const GymRules gym = gymRules();
  FakePorts ports;
  CourseRun run{gym, gym.courses.front(), false};
  frames(run, ports, 1200);
  CHECK(ports.awards == 0);
  frames(run, ports, 2);
  CHECK(ports.awards == 1);
}

TEST_CASE("gym.md stats box: it grows five frames, waits 100, then counts a point a frame") {
  using dwe::game::gym::CountRules;
  using dwe::game::gym::StatCountup;
  const CountRules rules{
      .delayFrames = 100, .soundSlot = 0, .sound = 22, .hpMpMax = 9999, .statMax = 999};
  StatCountup countup{rules, {800, 600, 80, 60, 70, 70}, {0, 0, 0, 8, 0, 2}};
  // The creating frame draws the first outline; four more frames grow it, and confirm waits.
  for (int frame = 0; frame < 4; ++frame) {
    CHECK_FALSE(countup.standing());
    countup.finish();
    CHECK_FALSE(countup.tickFrame());
  }
  CHECK(countup.standing());
  CHECK(countup.left()[3] == 8);
  // trn_hud.c: the tick counts the 100 down and the render of the 100th frame counts.
  for (int frame = 0; frame < 99; ++frame) {
    CHECK_FALSE(countup.tickFrame());
  }
  CHECK(countup.tickFrame());
  CHECK(countup.shown()[3] == 61);
  CHECK(countup.left()[3] == 7);
}

TEST_CASE("gym.md stats box: the outline moves a quarter of the way a frame (ui.c)") {
  using dwe::game::gym::BoxRect;
  using dwe::game::gym::GrowingBox;
  GrowingBox box;
  box.open(BoxRect{.x = -3, .y = -20, .w = 10, .h = 10},
           BoxRect{.x = -94, .y = -78, .w = 188, .h = 96});
  box.tickFrame();
  CHECK(box.outline() == BoxRect{.x = -3, .y = -20, .w = 10, .h = 10});
  box.tickFrame();
  // -3 + (-91 / 4), 7 + (87 / 4): whole pixels, truncated.
  CHECK(box.outline() == BoxRect{.x = -25, .y = -34, .w = 53, .h = 31});
  box.tickFrame();
  box.tickFrame();
  box.tickFrame();
  CHECK(box.outline() == BoxRect{.x = -94, .y = -78, .w = 188, .h = 96});
  CHECK(box.standing());
  box.tickFrame();
  CHECK_FALSE(box.outline().has_value());
  box.close();
  for (int frame = 0; frame < 5; ++frame) {
    CHECK_FALSE(box.closed());
    box.tickFrame();
  }
  CHECK(box.outline() == BoxRect{.x = -3, .y = -20, .w = 10, .h = 10});
  CHECK(box.closed());
}

TEST_CASE("gym.md slots: the box grows before the reels start and the rate waits for it to "
          "shrink away, with the textbox closed") {
  using dwe::game::gym::SlotMachine;
  using dwe::game::gym::SlotRules;
  SlotRules rules;
  rules.reels = {{1, 2, 3}, {1, 2, 3}, {1, 2, 3}};
  rules.assistByRoll = {0};
  rules.jackpotSymbol = 7;
  rules.spinSpeed = 1024;
  rules.cellPixels = 32;
  rules.resultFrames = 30;
  rules.payouts = {.jackpot = 100, .ownSymbol = 40, .triple = 20, .miss = 5};
  FakePorts ports;
  SlotMachine slots{rules, 1, ports};
  slots.startSpin();
  // Four frames of growing: the auto start waits for the box to stand.
  for (int frame = 0; frame < 4; ++frame) {
    CHECK_FALSE(slots.standing());
    slots.tick(ports, false);
  }
  CHECK(slots.standing());
  // Three presses stop the reels; the line then shows until confirm.
  int frames = 0;
  while (!slots.closing() && frames < 2000) {
    slots.tick(ports, frames % 20 == 0);
    ++frames;
  }
  REQUIRE(slots.closing());
  CHECK(ports.textboxesClosed == 1);
  CHECK_FALSE(slots.result().has_value());
  for (int frame = 0; frame < 4; ++frame) {
    slots.tick(ports, false);
    CHECK_FALSE(slots.result().has_value());
  }
  slots.tick(ports, false);
  CHECK(slots.result().has_value());
  CHECK(ports.textboxesClosed == 1);
}

TEST_CASE("gym.md sites: routine 7 picks the course of the map the player is on") {
  GymRules gym = gymRules();
  CourseRules savanna = gym.courses.front();
  savanna.map = "gias09";
  savanna.choice = 2;
  savanna.gains = "speed_alt";
  gym.courses.front().choice = 2;
  gym.courses.push_back(savanna);
  // script_interp.c:1682: the same choice is another course on another training map.
  REQUIRE(gym.courseOfChoice("gias09", 2) != nullptr);
  CHECK(gym.courseOfChoice("gias09", 2)->gains == "speed_alt");
  CHECK(gym.courseOfChoice("trai00", 2)->map == "trai00");
  CHECK(gym.courseOfChoice("gias09", 0) == nullptr);
  CHECK(gym.findCourse("twna01", "speed") == nullptr);
  CHECK(gym.hasMap("gias09"));
}

TEST_CASE("gym.md sites: the savanna's laps start again along z (trn_speed.c, instance 6)") {
  GymRules gym = gymRules();
  gym.courses.front().detail = SpeedCourse{
      .facing = 0, .runAnimation = 4, .lapFrames = 60, .restartAxis = Axis::Z, .restart = 2000};
  FakePorts ports;
  ports.onScreen = false;
  CourseRun run{gym, gym.courses.front(), false};
  frames(run, ports, 62);
  REQUIRE_FALSE(ports.placed.empty());
  CHECK(ports.placed.front() == std::pair{Axis::Z, 2000});
}

TEST_CASE("gym.md sites: the treadmill runs in place, loops its sound and leaves to its spot") {
  using dwe::game::gym::TreadmillCourse;
  GymRules gym = gymRules();
  gym.courses.front().detail = TreadmillCourse{.waypoint = {.x = 247, .z = 550},
                                               .facing = 2640,
                                               .runAnimation = 4,
                                               .loopSound = 14,
                                               .shown = {.first = 56, .count = 3},
                                               .leaveSpot = {.x = 247, .z = 965}};
  FakePorts ports;
  CourseRun run{gym, gym.courses.front(), false};
  frames(run, ports, 1300);
  CHECK(ports.runs == 0);
  CHECK(ports.loops == 1);
  CHECK(ports.stops == 1);
  // The count and the rest, then the walk off to (0xf7, 0x3c5), not to the tamer.
  frames(run, ports, 400, GymInput{.confirm = true});
  CHECK(run.finished());
  REQUIRE_FALSE(ports.walkedTo.empty());
  CHECK(ports.walkedTo.back() == std::optional{Point{.x = 247, .z = 965}});
}

TEST_CASE("gym.md sites: only the falling frames count towards the ice's defense session") {
  using dwe::game::gym::DefenseDropCourse;
  GymRules gym = gymRules();
  gym.courses.front().detail = DefenseDropCourse{.workAnimation = 37,
                                                 .falling = {.first = 14, .count = 1},
                                                 .drop = 40,
                                                 .untilRow = 200,
                                                 .hit = {.first = 33, .count = 1},
                                                 .particles = {},
                                                 .hitSound = 16,
                                                 .recoverFrames = 30};
  FakePorts ports;
  CourseRun run{gym, gym.courses.front(), false};
  // A drop of 5 frames (0 to 200 by 40) and 30 to recover: 1200 falling frames take 240 drops.
  frames(run, ports, 2 + 1200);
  CHECK(ports.awards == 0);
  CHECK(ports.restarts.size() >= 30);
}

TEST_CASE("gym.md sites: an offense hit swaps the objects and undoes it 46 frames later") {
  using dwe::game::gym::OffenseCourse;
  using dwe::game::gym::OffenseSwap;
  GymRules gym = gymRules();
  gym.defaultPunch = {.animation = 46, .frames = 10};
  OffenseCourse offense;
  offense.hitSound = 16;
  offense.swap = OffenseSwap{.hitShown = {.first = 43, .count = 30},
                             .hitHidden = {.first = 39, .count = 4},
                             .hitRestarted = {.first = 43, .count = 43},
                             .undoShown = {.first = 39, .count = 4},
                             .undoHidden = {.first = 43, .count = 30},
                             .frames = 46};
  gym.courses.front().detail = offense;
  FakePorts ports;
  CourseRun run{gym, gym.courses.front(), false};
  // Two frames to arrive, ten to the hit.
  frames(run, ports, 12);
  CHECK(ports.shows == std::vector<std::pair<std::int32_t, bool>>{{39, false}, {43, true}});
  CHECK(ports.restarts == std::vector<std::int32_t>{43});
  frames(run, ports, 46);
  CHECK(ports.shows.size() == 4);
  CHECK(ports.shows.at(2) == std::pair<std::int32_t, bool>{39, true});
  CHECK(ports.shows.at(3) == std::pair<std::int32_t, bool>{43, false});
}

TEST_CASE("gym.md HP: the push plays in place but for one frame on each beat (trn_hp.c)") {
  using dwe::game::gym::HpCourse;
  GymRules gym = gymRules();
  HpCourse hp;
  hp.facing = 2560;
  hp.workAnimation = 27;
  hp.beatFrames = 120;
  hp.beatSoundEvery = 10;
  gym.courses.front().detail = hp;
  FakePorts ports;
  CourseRun run{gym, gym.courses.front(), false};
  // One frame to start, one to arrive and start the push.
  frames(run, ports, 2);
  CHECK_FALSE(ports.partnerHeld);
  // The session's frames 0 to 1198 (the 1200th ends it and starts the done clip): animFlag 2 is
  // cleared on frames 0, 120, ..., 1080 only.
  int free = 0;
  for (int counter = 0; counter < 1199; ++counter) {
    frames(run, ports, 1);
    CHECK(ports.partnerHeld == (counter % 120 != 0));
    free += ports.partnerHeld ? 0 : 1;
  }
  CHECK(free == 10);
}

TEST_CASE("gym.md offense: the punches play in place, the first one too (trn_off.c)") {
  using dwe::game::gym::OffenseCourse;
  GymRules gym = gymRules();
  gym.defaultPunch = {.animation = 46, .frames = 10};
  OffenseCourse offense;
  offense.swingFrames = 41;
  gym.courses.front().detail = offense;
  FakePorts ports;
  CourseRun run{gym, gym.courses.front(), false};
  // Arrived: the first punch is held from its first frame.
  frames(run, ports, 2);
  CHECK(ports.partnerHeld);
  // Through the punch and the bag's swing, the punch stays held; a new punch is held from the
  // frame after it starts.
  int free = 0;
  for (int frame = 0; frame < 200; ++frame) {
    frames(run, ports, 1);
    free += ports.partnerHeld ? 0 : 1;
  }
  CHECK(free <= 4);
  CHECK(ports.partnerHeld);
}

TEST_CASE("gym.md sites: the second battlefield holds every punch, the savanna only the first") {
  using dwe::game::gym::OffenseCourse;
  using dwe::game::gym::OffenseSwap;
  for (const bool holdEachPunch : {true, false}) {
    GymRules gym = gymRules();
    gym.defaultPunch = {.animation = 46, .frames = 10};
    OffenseCourse offense;
    OffenseSwap swap;
    swap.frames = 46;
    swap.holdEachPunch = holdEachPunch;
    offense.swap = swap;
    gym.courses.front().detail = offense;
    FakePorts ports;
    CourseRun run{gym, gym.courses.front(), false};
    // Arrive, punch (10), swap (46): the second punch starts; one more frame into it.
    frames(run, ports, 2 + 10 + 46 + 1);
    CHECK(ports.partnerHeld == holdEachPunch);
  }
}

TEST_CASE("gym.md watching: the player plays its watch clip and turns to the partner every frame") {
  GymRules gym = gymRules();
  gym.watchAnimation = 10;
  FakePorts ports;
  CourseRun run{gym, gym.courses.front(), false};
  frames(run, ports, 5);
  // tamer.c: tamerTickTraining: clip 10 once, then entityLookAtLocation every frame.
  CHECK(ports.tamerClips == std::vector<std::int32_t>{10});
  CHECK(ports.tamerLooks == 5);
}

TEST_CASE("gym.md watching: the brains course leads the player itself (tamerSetState(-1))") {
  GymRules gym = gymRules();
  gym.watchAnimation = 10;
  gym.courses.front().detail = BrainCourse{};
  FakePorts ports;
  CourseRun run{gym, gym.courses.front(), false};
  frames(run, ports, 1);
  CHECK(ports.tamerLooks == 0);
  CHECK(std::ranges::find(ports.tamerClips, 10) == ports.tamerClips.end());
}

TEST_CASE("gym.md sounds: no looping voice is left once a session ends, however it ends") {
  // trn_mp.c, trn_def.c and trn2_reward.c: playSound2 at the start, thunkStopSoundMask at the end.
  MpCourse waterfall;
  waterfall.loopSound = 2;
  DefenseCourse log;
  log.loopSound = 4;
  log.swings = {DefenseSwing{.dx = 50, .limit = 50, .push = false}};
  TreadmillCourse treadmill;
  treadmill.loopSound = 14;
  const auto courses =
      std::array{CourseDetail{waterfall}, CourseDetail{log}, CourseDetail{treadmill}};
  for (const CourseDetail& detail : courses) {
    for (const bool cancelled : {false, true}) {
      GymRules gym = gymRules();
      gym.courses.front().detail = detail;
      gym.courses.front().cycleFrames = 60;
      FakePorts ports;
      CourseRun run{gym, gym.courses.front(), false};
      frames(run, ports, 5);
      CHECK(ports.loops >= 1);
      frames(run, ports, 400, GymInput{.cancelHeld = cancelled});
      CHECK(run.finished());
      CHECK(ports.playing.empty());
    }
  }
}

TEST_CASE("gym.md MP: the lit water drops to 20 pixels above the partner's head row") {
  // trn_mp.c: moveMapObjects(.., 0, screen.vy - 0x14), screen from getDrawPosition of the head.
  MpCourse waterfall;
  waterfall.objects = ObjectGroup{.first = 164, .count = 8};
  waterfall.headLift = 20;
  GymRules gym = gymRules();
  gym.courses.front().detail = waterfall;
  FakePorts ports;
  ports.headRow = 54; // the original's row, measured with the partner under the waterfall
  CourseRun run{gym, gym.courses.front(), false};
  frames(run, ports, 4);
  CHECK(ports.row == 34);
}
