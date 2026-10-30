#include <dwe/game/gym/course_flow.hpp>
#include <dwe/game/gym/growing_box.hpp>
#include <dwe/game/gym/stat_countup.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>

namespace {

using dwe::game::gym::Actor;
using dwe::game::gym::Award;
using dwe::game::gym::CourseRules;
using dwe::game::gym::CourseRun;
using dwe::game::gym::GymInput;
using dwe::game::gym::GymPorts;
using dwe::game::gym::GymRules;
using dwe::game::gym::ObjectGroup;
using dwe::game::gym::ObjectMove;
using dwe::game::gym::Point;
using dwe::game::gym::Point3;
using dwe::game::gym::SpeedCourse;

// The field as the course sees it: every walk arrives at once, the partner stays on screen, and an
// award gives one point of speed.
class FakePorts final : public GymPorts {
public:
  void setTraining(bool /*training*/) override {}
  void animate(Actor /*actor*/, std::int32_t /*animation*/) override {}
  void holdAnimation(Actor /*actor*/, bool /*hold*/) override {}
  void face(Actor /*actor*/, std::int32_t /*angle*/) override {}
  void faceEachOther() override {}
  bool walk(Actor /*actor*/, std::optional<Point> /*spot*/) override { return true; }
  void moveCamera(std::optional<Point> /*spot*/, std::int32_t /*speed*/) override {}
  void playSound(std::int32_t /*slot*/, std::int32_t /*sound*/) override {}
  std::int32_t loopSound(std::int32_t /*slot*/, std::int32_t /*sound*/) override { return 1; }
  void stopSound(std::int32_t /*handle*/) override {}
  void storeObjects(const ObjectGroup& /*group*/) override {}
  void restoreObjects(const ObjectGroup& /*group*/) override {}
  void moveObjects(const ObjectGroup& /*group*/, ObjectMove /*move*/) override {}
  void showObjects(const ObjectGroup& /*group*/, bool /*shown*/) override {}
  std::optional<std::int32_t> partnerHeadRow() override { return std::nullopt; }
  std::optional<Point> partnerPosition() override { return Point{}; }
  void movePartner(Point /*delta*/) override {}
  void runOn(Actor /*actor*/) override {}
  bool partnerOnScreen() override { return true; }
  void placePartnerX(std::int32_t /*x*/) override {}
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
};

GymRules gymRules() {
  GymRules gym;
  gym.count = {.delayFrames = 100, .soundSlot = 0, .sound = 22, .hpMpMax = 9999, .statMax = 999};
  CourseRules speed;
  speed.id = "speed";
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
