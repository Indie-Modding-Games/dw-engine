#include <dwe/assets/map_document.hpp>
#include <dwe/game/story/script_motion.hpp>

#include <doctest/doctest.h>

#include <map>
#include <numbers>

namespace {

using dwe::game::story::Glide;
using dwe::game::story::MotionRules;
using dwe::game::story::PlayerScriptId;
using dwe::game::story::ScriptActorsPort;
using dwe::game::story::ScriptMotion;

constexpr std::int64_t Vegiemon = 6;
constexpr float FrameSeconds = 1.0F / 30.0F;

// The player and one NPC on an open floor; drive moves a character by its velocity each frame.
class Floor final : public ScriptActorsPort {
public:
  struct Actor {
    std::int64_t slot = 0;
    glm::vec3 position{0.0F};
    float yaw = 0.0F;
    glm::vec3 velocity{0.0F};
    std::int64_t animation = -1;
  };
  std::map<std::int64_t, Actor> actors;
  // What wouldTouchCharacter answers for everyone.
  bool touching = false;

  void step() {
    for (auto& [id, actor] : actors) {
      actor.position += actor.velocity * FrameSeconds;
    }
  }

  [[nodiscard]] std::optional<std::int64_t> slotOf(std::int64_t character) const override {
    const auto found = actors.find(character);
    return found == actors.end() ? std::nullopt : std::optional{found->second.slot};
  }
  [[nodiscard]] glm::vec3 position(std::int64_t character) const override {
    return actors.at(character).position;
  }
  [[nodiscard]] float yaw(std::int64_t character) const override {
    return actors.at(character).yaw;
  }
  void turn(std::int64_t character, float yaw) override { actors.at(character).yaw = yaw; }
  void place(std::int64_t character, glm::vec3 position) override {
    actors.at(character).position = position;
  }
  void drive(std::int64_t character, glm::vec3 velocity, Glide /*glide*/) override {
    actors.at(character).velocity = velocity;
  }
  void animate(std::int64_t character, std::int64_t animation) override {
    actors.at(character).animation = animation;
  }
  [[nodiscard]] float groundSpeed(std::int64_t /*character*/,
                                  std::int64_t /*animation*/) const override {
    return 900.0F;
  }
  [[nodiscard]] bool wouldTouchCharacter(std::int64_t /*character*/) const override {
    return touching;
  }
  void panCamera(glm::vec3 /*focus*/, float /*seconds*/) override {}
  [[nodiscard]] bool cameraPanning() const override { return false; }
  void shiftCamera(glm::vec3 /*by*/) override {}
  void resetOrigin(std::int64_t /*character*/) override {}
  [[nodiscard]] bool turnMapObject(std::int64_t /*object*/, std::int64_t /*direction*/) override {
    return true;
  }
  [[nodiscard]] std::optional<glm::ivec2>
  mapSpritePosition(std::int64_t /*object*/) const override {
    return std::nullopt;
  }
  void placeMapSprite(std::int64_t /*object*/, glm::ivec2 /*position*/) override {}
};

} // namespace

TEST_CASE("a turn that takes a walking character's slot stops the walk when it ends") {
  // trop03's Vegiemon (script 15, section 0x33): the player walks off toward (-1024, -1820) and,
  // ten frames later, entity_look_at_entity takes slot 0 to face Vegiemon. tickScriptedMovement
  // starts the player's resting animation when the turn is done, which carries nobody: the
  // player stops there instead of running on into the jungle.
  Floor floor;
  floor.actors[PlayerScriptId] = {.slot = 0, .position = {-708.0F, 0.0F, 2323.0F}};
  floor.actors[Vegiemon] = {.slot = 3, .position = {911.0F, 0.0F, 2266.0F}};
  ScriptMotion motion{floor, MotionRules{.frameSeconds = FrameSeconds,
                                         .lookTurnPerFrame = std::numbers::pi_v<float> / 8.0F}};
  motion.walkToWithCamera(PlayerScriptId, -1024, -1820, 1);
  for (int frame = 0; frame < 10; ++frame) {
    motion.scriptFrame();
    floor.step();
  }
  CHECK(glm::length(floor.actors[PlayerScriptId].velocity) > 0.0F);
  motion.lookAt(PlayerScriptId, Vegiemon);
  const auto done = motion.waitFor(PlayerScriptId);
  for (int frame = 0; frame < 30 && !done(); ++frame) {
    motion.scriptFrame();
    floor.step();
  }
  REQUIRE(done());
  CHECK(floor.actors[PlayerScriptId].velocity == glm::vec3{0.0F});
  CHECK(floor.actors[PlayerScriptId].animation == 0);
  const glm::vec3 stopped = floor.actors[PlayerScriptId].position;
  for (int frame = 0; frame < 30; ++frame) {
    motion.scriptFrame();
    floor.step();
  }
  CHECK(floor.actors[PlayerScriptId].position == stopped);
}

TEST_CASE("a walk to a character takes its first frame whatever stands ahead") {
  // gias04's ambush (script 70, section 0x33): the partner, standing against the player, walks to
  // the bird it hunts while the player walks off. tickEntityWalkTo's first frame tests no step, as
  // the walk's clip has no momentum yet; from the next a character ahead ends the walk.
  constexpr std::int64_t Prey = 5;
  Floor floor;
  floor.actors[PlayerScriptId] = {.slot = 0, .position = {0.0F, 0.0F, 0.0F}};
  floor.actors[Prey] = {.slot = 3, .position = {2000.0F, 0.0F, 0.0F}};
  floor.touching = true;
  ScriptMotion motion{floor, MotionRules{.frameSeconds = FrameSeconds,
                                         .lookTurnPerFrame = std::numbers::pi_v<float> / 8.0F}};
  motion.walkToCharacter(PlayerScriptId, 1, Prey);
  const auto done = motion.waitFor(PlayerScriptId);
  motion.scriptFrame();
  floor.step();
  CHECK_FALSE(done());
  CHECK(glm::length(floor.actors[PlayerScriptId].velocity) > 0.0F);
  motion.scriptFrame();
  floor.step();
  CHECK(done());
  CHECK(floor.actors[PlayerScriptId].velocity == glm::vec3{0.0F});
}

TEST_CASE("a walk faces its point as it starts, before the clip's first step") {
  // The same ambush: the player and the partner face each other when the walks start, and the walk
  // clip's first step must already go toward each walk's point (tickEntityWalkTo faces the target
  // on the walk's first frame), not toward the other one.
  constexpr std::int64_t Prey = 5;
  Floor floor;
  floor.actors[PlayerScriptId] = {.slot = 0, .position = {0.0F, 0.0F, 0.0F}, .yaw = 1.0F};
  floor.actors[Prey] = {.slot = 3, .position = {0.0F, 0.0F, -2000.0F}};
  ScriptMotion motion{floor, MotionRules{.frameSeconds = FrameSeconds,
                                         .lookTurnPerFrame = std::numbers::pi_v<float> / 8.0F}};
  motion.walkToCharacter(PlayerScriptId, 1, Prey);
  CHECK(floor.actors[PlayerScriptId].yaw ==
        doctest::Approx(dwe::assets::yawOfDirection({0.0F, 0.0F, -1.0F})));
  motion.walkTo(PlayerScriptId, 2000, 0, 1);
  CHECK(floor.actors[PlayerScriptId].yaw ==
        doctest::Approx(dwe::assets::yawOfDirection({1.0F, 0.0F, 0.0F})));
}

TEST_CASE("a walk at a point on its cell's corner from outside the cell stops on the point") {
  // mgen13's way into the back dimension (script 63, section 0x3c): after the fixed fight the
  // boss walks back to (-100, 800) from wherever the fight left it. That point is its cell's
  // corner, and from below and to the right the walk crosses the corner without ever being in the
  // cell (tickEntityWalkTo ends on the cell only): back and forth for ever, and wait_for_entity(6)
  // never ended. The original's whole-unit steps land in the cell after a few passes.
  constexpr std::int64_t Walker = 6;
  Floor floor;
  floor.actors[Walker] = {.slot = 3, .position = {-500.0F, 0.0F, -24.0F}};
  ScriptMotion motion{floor, MotionRules{.frameSeconds = FrameSeconds}};
  motion.walkTo(Walker, -100, 800, 0);
  const auto done = motion.waitFor(Walker);
  for (int frame = 0; frame < 120 && !done(); ++frame) {
    motion.scriptFrame();
    floor.step();
  }
  REQUIRE(done());
  CHECK(floor.actors[Walker].position.x == doctest::Approx(-100.0F));
  CHECK(floor.actors[Walker].position.z == doctest::Approx(-800.0F));
  CHECK(floor.actors[Walker].velocity == glm::vec3{0.0F});
}

TEST_CASE("a walk still ends as it steps into its point's cell, short of the point") {
  // The common case stays the original's: the walk ends on the first step in the cell.
  constexpr std::int64_t Walker = 6;
  Floor floor;
  floor.actors[Walker] = {.slot = 3, .position = {0.0F, 0.0F, -150.0F}};
  ScriptMotion motion{floor, MotionRules{.frameSeconds = FrameSeconds}};
  motion.walkTo(Walker, 340, 150, 0);
  const auto done = motion.waitFor(Walker);
  for (int frame = 0; frame < 120 && !done(); ++frame) {
    motion.scriptFrame();
    floor.step();
  }
  REQUIRE(done());
  CHECK(floor.actors[Walker].position.x == doctest::Approx(300.0F));
}

TEST_CASE("a move to a point ordered while another runs goes on with that one's step") {
  // mgen06's boss (script 194, section 0x35): play_animation(10, 28), entity_move_to(10, -4562,
  // 41, 20), delay(20), and again. Measured in the original: the second order comes while the
  // first slide is still on its way, and the slide goes on with the first step (-107, -12 there)
  // until it is past the point (tamer.c: tickEntityMoveTo keeps one step, MOVE_TO_DELTA_X/Z, while
  // PREVIOUS_CAMERA_POS_INITIALIZED is set). Taking a new step from where it stands would make a
  // short one that the clip's own steps beat, and wait_for_entity(10) would never end.
  constexpr std::int64_t Boss = 10;
  Floor floor;
  floor.actors[Boss] = {.slot = 7, .position = {-4000.0F, 0.0F, 600.0F}};
  ScriptMotion motion{floor, MotionRules{.frameSeconds = FrameSeconds}};
  motion.moveTo(Boss, -4562, 41, 20);
  for (int frame = 0; frame < 11; ++frame) {
    motion.scriptFrame();
    floor.step();
  }
  // Whole units, divided toward zero: (-562 / 20, -641 / 20).
  CHECK(floor.actors[Boss].velocity.x * FrameSeconds == doctest::Approx(-28.0F));
  CHECK(floor.actors[Boss].velocity.z * FrameSeconds == doctest::Approx(-32.0F));
  motion.moveTo(Boss, -4562, 41, 20);
  const auto done = motion.waitFor(Boss);
  int frame = 0;
  for (; frame < 40 && !done(); ++frame) {
    motion.scriptFrame();
    floor.step();
    // The clip's own steps carry it on past the point once it is nearly there.
    floor.actors[Boss].position.x -= 60.0F;
  }
  REQUIRE(done());
  CHECK(frame <= 12);
  // The script's z runs the other way: twenty steps of 32 from 600 leave it at -40, short of the
  // point (-41), and the 21st takes it past and leaves it there.
  CHECK(floor.actors[Boss].position.z == doctest::Approx(600.0F - (21.0F * 32.0F)));
}

TEST_CASE("a move to a point ordered after one ended takes a new step") {
  constexpr std::int64_t Boss = 10;
  Floor floor;
  floor.actors[Boss] = {.slot = 7, .position = {-4000.0F, 0.0F, 600.0F}};
  ScriptMotion motion{floor, MotionRules{.frameSeconds = FrameSeconds}};
  motion.moveTo(Boss, -4562, 41, 20);
  const auto first = motion.waitFor(Boss);
  for (int frame = 0; frame < 40 && !first(); ++frame) {
    motion.scriptFrame();
    floor.step();
  }
  REQUIRE(first());
  floor.actors[Boss].position = {-5487.0F, 0.0F, 47.0F};
  motion.moveTo(Boss, -4562, 41, 20);
  motion.scriptFrame();
  // Measured in the original (third order of section 0x35): (925 / 20, 88 / 20) = (46, 4).
  CHECK(floor.actors[Boss].velocity.x * FrameSeconds == doctest::Approx(46.0F));
  CHECK(floor.actors[Boss].velocity.z * FrameSeconds == doctest::Approx(-4.0F));
}

TEST_CASE("a move to a point closer than its count ends at once where it stands") {
  // tickEntityMoveTo's step is 0 on an axis shorter than the count, and a 0 step is there once
  // at or past the point the way up. It sets the axis on the point, but the animation puts it
  // back where it stands on its next tick (anim.c: applyRootMomentum).
  constexpr std::int64_t Boss = 10;
  Floor floor;
  floor.actors[Boss] = {.slot = 7, .position = {-4552.0F, 0.0F, -41.0F}};
  ScriptMotion motion{floor, MotionRules{.frameSeconds = FrameSeconds}};
  motion.moveTo(Boss, -4562, 41, 20);
  const auto done = motion.waitFor(Boss);
  motion.scriptFrame();
  CHECK_FALSE(done());
  motion.scriptFrame();
  REQUIRE(done());
  CHECK(floor.actors[Boss].position.x == -4552.0F);
}

TEST_CASE("a move to a point ends where the step that takes it past the point leaves it") {
  // The item shop keeper with the crate as the town's market opens (script 163, map entry):
  // entity_move_to(7, -1232, 2903, 10) from (225, -143). Measured in the original: steps of
  // (-1457 / 10, 3046 / 10) = (-145, 304) and it stops at (-1370, 3201), eleven steps on, not on
  // the point: tickEntityMoveTo sets each axis on the point once past it, but the animation's own
  // position, which the step moved too, overwrites it on its next tick (anim.c:
  // applyRootMomentum).
  constexpr std::int64_t Keeper = 7;
  Floor floor;
  floor.actors[Keeper] = {.slot = 4, .position = {225.0F, 0.0F, 143.0F}};
  ScriptMotion motion{floor, MotionRules{.frameSeconds = FrameSeconds}};
  motion.moveTo(Keeper, -1232, 2903, 10);
  const auto done = motion.waitFor(Keeper);
  int frame = 0;
  for (; frame < 40 && !done(); ++frame) {
    motion.scriptFrame();
    floor.step();
  }
  REQUIRE(done());
  CHECK(frame == 12);
  CHECK(floor.actors[Keeper].position.x == doctest::Approx(-1370.0F));
  CHECK(floor.actors[Keeper].position.z == doctest::Approx(-3201.0F)); // z runs the other way
}

TEST_CASE("a move to an axis position ends where the step that takes it past leaves it") {
  // tickEntityMoveToAxis sets the axis on the position the same way, and the animation puts it
  // back where the step left it.
  constexpr std::int64_t Keeper = 7;
  Floor floor;
  floor.actors[Keeper] = {.slot = 4, .position = {225.0F, 0.0F, 0.0F}};
  ScriptMotion motion{floor, MotionRules{.frameSeconds = FrameSeconds}};
  motion.moveToAxis(Keeper, -1232, 0, 10);
  const auto done = motion.waitFor(Keeper);
  for (int frame = 0; frame < 40 && !done(); ++frame) {
    motion.scriptFrame();
    floor.step();
  }
  REQUIRE(done());
  CHECK(floor.actors[Keeper].position.x == doctest::Approx(225.0F - (11.0F * 145.0F)));
}

TEST_CASE("a move to a point still ends on its own count when nothing holds it back") {
  constexpr std::int64_t Boss = 10;
  Floor floor;
  floor.actors[Boss] = {.slot = 7, .position = {-4000.0F, 0.0F, 600.0F}};
  ScriptMotion motion{floor, MotionRules{.frameSeconds = FrameSeconds}};
  motion.moveTo(Boss, -4562, 41, 20);
  const auto done = motion.waitFor(Boss);
  int frame = 0;
  for (; frame < 200 && !done(); ++frame) {
    motion.scriptFrame();
    floor.step();
  }
  REQUIRE(done());
  CHECK(frame <= 23);
}
