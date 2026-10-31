#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/session/story_lua.hpp>
#include <dwe/session/story_props.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "apps/session/src/story_routines/routines.hpp"
#include "apps/session/src/story_scenes_context.hpp"
#include "story_lua_fixture.hpp"

using dwe::session::BoulderShown;
using dwe::session::DirtCartShown;
using dwe::session::DirtPileShown;
using dwe::session::ScreenShakeStarted;
using dwe::session::SpriteAtCharacter;
using dwe::session::StoryPropRequest;
using story_play::Played;
using story_play::SectionRef;
using story_play::StoryPlay;

namespace {

// Records the props' orders by name, as the fixture's recorder records the story's.
class RecordedProps final : public dwe::session::StoryPropOrders {
public:
  explicit RecordedProps(story_play::Recorder& recorder) : recorder_{&recorder} {}

  void spawnBoulder() override { recorder_->record("spawn_boulder"); }
  void moveBoulder(std::int64_t down, std::int64_t forward) override {
    recorder_->record(std::format("move_boulder({},{})", down, forward));
  }
  void despawnBoulder() override { recorder_->record("despawn_boulder"); }
  void spriteAtCharacter(std::int64_t character, std::int64_t node, std::int64_t object) override {
    recorder_->record(std::format("sprite({},{},{})", character, node, object));
  }

private:
  story_play::Recorder* recorder_;
};

std::vector<StoryPropRequest> takeAll(dwe::session::StoryScenesContext& scenes) {
  std::vector<StoryPropRequest> taken(scenes.propRequests.begin(), scenes.propRequests.end());
  scenes.propRequests.clear();
  return taken;
}

} // namespace

TEST_CASE("the boulder's orders: each move waits a frame, the rest go on in the same one") {
  dwe::assets::MemoryFileSystem memory;
  memory.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
  memory.addFile("base/story/030.lua", R"lua(-- dwx lua 3: a module of the tests
local story <const> = story
local ENDED <const> = story.ENDED
local routines = {}
function routines.at_0005()
  story.spawn_boulder(0)
  story.move_boulder(0, 0, 50)
  story.move_boulder(0, 120, 100)
  story.despawn_boulder(0)
  story.spawn_sprite_at_entity(253, 10, 44)
  return ENDED
end
return { script = 30, sections = { [5] = routines.at_0005 } }
)lua");
  auto files = dwe::assets::VirtualFileSystem::mount(
      {dwe::assets::PackLocation{.fileSystem = &memory,
                                 .root = "base",
                                 .fillsGaps = false,
                                 .origin = dwe::assets::PackOrigin::Engine}});
  REQUIRE(files.has_value());
  StoryPlay play{*files};
  RecordedProps props{play.recorder()};
  REQUIRE(play.host().publishPropApi(props));
  const Played played = play.play(SectionRef{.script = 30, .section = 5}, 1);
  CHECK(played.status == "ended");
  CHECK(played.events == std::vector<std::string>{"spawn_boulder", "move_boulder(0,50)", "delay(1)",
                                                  "move_boulder(120,100)", "delay(1)",
                                                  "despawn_boulder", "sprite(253,10,44)"});
}

TEST_CASE("the boulder starts above the tunnel and each move lowers, pushes and turns it") {
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{}};
  const auto orders = dwe::session::makePropOrders(scenes);
  orders->spawnBoulder();
  orders->moveBoulder(10, 200);
  orders->moveBoulder(-5, 0);
  orders->despawnBoulder();
  orders->spriteAtCharacter(253, 10, 300);
  const auto taken = takeAll(scenes);
  REQUIRE(taken.size() == 5);
  // map_object.c: spawnBoulder (-300, -500, 2000); moveBoulder adds to y and z, pitch -10.
  const auto& spawned = std::get<BoulderShown>(taken[0]);
  CHECK(spawned.shown);
  CHECK(spawned.x == -300);
  CHECK(spawned.y == -500);
  CHECK(spawned.z == 2000);
  CHECK(spawned.pitch == 0);
  const auto& moved = std::get<BoulderShown>(taken[2]);
  CHECK(moved.y == -495);
  CHECK(moved.z == 2200);
  CHECK(moved.pitch == -20);
  CHECK_FALSE(std::get<BoulderShown>(taken[3]).shown);
  // The sprite is a byte operand (pollNextTwoScriptBytes).
  const auto& sprite = std::get<SpriteAtCharacter>(taken[4]);
  CHECK(sprite.character == 253);
  CHECK(sprite.node == 10);
  CHECK(sprite.object == 44);
}

TEST_CASE("the dirt pile shrinks a step each routine 20 and keeps its size for the next load") {
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{}};
  REQUIRE(dwe::session::registerDirtCartRoutines(scenes));
  REQUIRE(dwe::session::registerScreenShakeRoutine(scenes));
  using dwe::game::story::RoutineGoesOn;
  using dwe::game::story::RoutineYields;
  // 26 and 27 load the cart (no model) and the pile, and go on in the same frame.
  const auto loadCart = scenes.routines.call(26);
  const auto loadPile = scenes.routines.call(27);
  REQUIRE(loadCart);
  REQUIRE(loadPile);
  CHECK(std::holds_alternative<RoutineGoesOn>(*loadCart));
  CHECK(std::holds_alternative<RoutineGoesOn>(*loadPile));
  // tunn05's entry shrinks the pile ten times, a frame each (longjmp 2).
  for (int shrink = 0; shrink < 10; ++shrink) {
    const auto shrunk = scenes.routines.call(20);
    REQUIRE(shrunk);
    CHECK(std::holds_alternative<RoutineYields>(*shrunk));
  }
  const auto shake = scenes.routines.call(34);
  REQUIRE(shake);
  CHECK(std::holds_alternative<RoutineGoesOn>(*shake));
  const auto taken = takeAll(scenes);
  REQUIRE(taken.size() == 13);
  CHECK(std::get<DirtCartShown>(taken[0]).model == dwe::session::DirtCartHidden);
  CHECK(std::get<DirtPileShown>(taken[1]).size == dwe::session::DirtPileFullSize);
  CHECK(std::get<DirtPileShown>(taken[2]).size == 14);
  CHECK(std::get<DirtPileShown>(taken[11]).size == 5);
  CHECK(std::holds_alternative<ScreenShakeStarted>(taken[12]));
  // DIRT_PILE_SIZE_COUNTER is not reset: the next load shows the smaller pile (hidden under 6).
  REQUIRE(scenes.routines.call(27));
  CHECK(std::get<DirtPileShown>(takeAll(scenes).front()).size == 5);
}

TEST_CASE("Trash Mountain has the giant poop, at the original's place, and no other map has it") {
  // map.c: loadMap calls initializeTrainingPoop when mapId is 0xa5.
  const auto poop = dwe::session::trainingPoopOn("gomi02");
  REQUIRE(poop.has_value());
  CHECK(poop->x == 1922);
  CHECK(poop->y == 0);
  CHECK(poop->z == 503);
  CHECK_FALSE(dwe::session::trainingPoopOn("gomi01").has_value());
  CHECK_FALSE(dwe::session::trainingPoopOn("tunn05").has_value());
  CHECK_FALSE(dwe::session::trainingPoopOn("").has_value());
}

TEST_CASE("the Angemon pedestal starts at z 1500 and slides 10 a frame to 2000, shaking in x") {
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{}};
  REQUIRE(dwe::session::registerAngemonPedestalRoutines(scenes));
  using dwe::game::story::RoutineGoesOn;
  using dwe::game::story::RoutineWaits;
  using dwe::game::story::TaskRunning;
  using dwe::session::AngemonPedestalShown;
  // map_object.c: spawnAngemonPedestal sets X 0 and Z 1500 and goes on.
  const auto spawned = scenes.routines.call(49);
  REQUIRE(spawned);
  CHECK(std::holds_alternative<RoutineGoesOn>(*spawned));
  const auto start = takeAll(scenes);
  REQUIRE(start.size() == 1);
  CHECK(std::get<AngemonPedestalShown>(start[0]).x == 0);
  CHECK(std::get<AngemonPedestalShown>(start[0]).z == 1500);
  // moveAngemonPedestal ticks from the next frame: 50 frames to z 2000.
  const auto slid = scenes.routines.call(50);
  REQUIRE(slid);
  const auto* waits = std::get_if<RoutineWaits>(&*slid);
  REQUIRE(waits != nullptr);
  CHECK(std::holds_alternative<TaskRunning>(waits->task->tick()));
  int frames = 0;
  while (std::holds_alternative<TaskRunning>(waits->task->tick())) {
    ++frames;
    REQUIRE(frames < 100);
  }
  CHECK(frames + 1 == 50);
  const auto steps = takeAll(scenes);
  REQUIRE(steps.size() == 50);
  // z 1510 is not a multiple of 20: x -5; z 1520 is: x 5.
  CHECK(std::get<AngemonPedestalShown>(steps[0]).z == 1510);
  CHECK(std::get<AngemonPedestalShown>(steps[0]).x == -5);
  CHECK(std::get<AngemonPedestalShown>(steps[1]).x == 5);
  CHECK(std::get<AngemonPedestalShown>(steps[49]).z == 2000);
  CHECK(std::get<AngemonPedestalShown>(steps[49]).x == 5);
  CHECK(scenes.pedestal.z == 2000);
}

TEST_CASE("Toy Town's gearbox and boxes go on at once and the selected box's lid opens a quarter") {
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{}};
  REQUIRE(dwe::session::registerToyTownRoutines(scenes));
  using dwe::game::story::RoutineGoesOn;
  using dwe::session::ToyTownBoxSelected;
  // map_object.c: spawnGearbox selects no box; spawnToyTownBoxes resets the lids itself.
  const auto gearbox = scenes.routines.call(41);
  const auto boxes = scenes.routines.call(43);
  REQUIRE(gearbox);
  REQUIRE(boxes);
  CHECK(std::holds_alternative<RoutineGoesOn>(*gearbox));
  CHECK(std::holds_alternative<RoutineGoesOn>(*boxes));
  const auto taken = takeAll(scenes);
  REQUIRE(taken.size() == 3);
  CHECK(std::holds_alternative<dwe::session::ToyTownGearboxShown>(taken[0]));
  CHECK(std::get<ToyTownBoxSelected>(taken[1]).box == 0);
  CHECK(std::holds_alternative<dwe::session::ToyTownBoxesShown>(taken[2]));
  CHECK(scenes.routines.registered(42));
  CHECK(scenes.routines.registered(44));
  // The operand is a signed byte.
  CHECK(dwe::session::toyTownBoxOf(1) == dwe::session::ToyTownBigBox);
  CHECK(dwe::session::toyTownBoxOf(2) == dwe::session::ToyTownSmallBox);
  CHECK(dwe::session::toyTownBoxOf(255) == -1);
  // renderToyTownBoxes: -100 a frame, held at -0x400 from the 11th frame.
  std::int32_t lid = 0;
  int frames = 0;
  while (lid != dwe::session::ToyTownLidOpen) {
    lid = dwe::session::toyTownLidAfterFrame(lid);
    ++frames;
    REQUIRE(frames < 20);
  }
  CHECK(frames == 11);
  CHECK(dwe::session::toyTownLidAfterFrame(lid) == -0x400);
}

TEST_CASE("the Beetle Cup entry writes the cup, the partner and the entry day of routine 33") {
  // script_interp.c:1796: 0x95 on any day but 0x16, which writes 0x16.
  const auto before = dwe::session::beetleCupEntry(3, 21);
  CHECK(before.entryDay == 0x95);
  CHECK(before.cup == 0x16);
  CHECK(before.entrant == 21);
  CHECK(dwe::session::beetleCupEntry(0x16, 1).entryDay == 0x16);
  CHECK(dwe::session::beetleCupEntry(0x17, 1).entryDay == 0x95);
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{}};
  REQUIRE(dwe::session::registerBeetleCupEntryRoutine(scenes));
  // Without a game the routine only goes on.
  const auto entered = scenes.routines.call(33);
  REQUIRE(entered);
  CHECK(std::holds_alternative<dwe::game::story::RoutineGoesOn>(*entered));
}
