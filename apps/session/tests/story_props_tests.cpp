#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/story/story_state.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_lua.hpp>
#include <dwe/session/story_props.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "adapter_fixture.hpp"
#include "apps/session/src/story_routines/routines.hpp"
#include "apps/session/src/story_scenes_context.hpp"
#include "story_lua_fixture.hpp"

using dwe::session::BoulderShown;
using dwe::session::DirtCartShown;
using dwe::session::DirtPileShown;
using dwe::session::MoodBubbleShown;
using dwe::session::ScreenShakeStarted;
using dwe::session::SpriteAtCharacter;
using dwe::session::StoryPropRequest;
using dwe::session::WhiteFadeStarted;
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

TEST_CASE("map_object.c loadWarpCrystals: the ice sanctuary's three maps have their crystals") {
  const auto icsa05 = dwe::session::warpCrystalsOn("icsa05");
  REQUIRE(icsa05.size() == 4);
  CHECK(icsa05[0].x == 2237);
  CHECK(icsa05[0].z == -1667);
  CHECK(icsa05[3].x == -2358);
  CHECK(icsa05[3].z == 419);
  REQUIRE(dwe::session::warpCrystalsOn("icsa06").size() == 1);
  const auto icsa07 = dwe::session::warpCrystalsOn("icsa07");
  REQUIRE(icsa07.size() == 2);
  CHECK(icsa07[1].z == -3358);
  // loadMap calls it only on maps 101 to 103: the case for 104 (icsa08) never runs.
  CHECK(dwe::session::warpCrystalsOn("icsa08").empty());
  CHECK(dwe::session::warpCrystalsOn("gomi02").empty());
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

TEST_CASE("the mist goes in 7 frames from the boot palettes and in 1 once the trigger cleared it") {
  using dwe::session::MistPalettes;
  // overworld.c: MIST_CLUT_Y starts at 0x50 and 0x10; tickRemoveMist adds 0x10 a frame.
  MistPalettes boot;
  int frames = 1;
  while (!dwe::session::removeMistOneFrame(boot)) {
    ++frames;
    REQUIRE(frames < 20);
  }
  CHECK(frames == 7);
  CHECK(boot.mist05 == 0xc0);
  CHECK(boot.others == 0x80);
  // renderMist already put the palettes of no mist: the first tick ends it.
  MistPalettes cleared{.mist05 = dwe::session::MistClearedMist05,
                       .others = dwe::session::MistClearedOthers};
  CHECK(dwe::session::removeMistOneFrame(cleared));
  CHECK(cleared.mist05 == 0xc0);
  CHECK(cleared.others == 0x80);
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{}};
  REQUIRE(dwe::session::registerRemoveMistRoutine(scenes));
  CHECK(scenes.routines.registered(37));
}

TEST_CASE("routines 51 and 52 load the boss's and the ending's sound banks and go on") {
  // script_interp.c:1846, 1853: loadMapSounds2(0x15) and loadMapSounds2(0x14).
  CHECK(dwe::session::loadRoutineSounds(51) == 0x15);
  CHECK(dwe::session::loadRoutineSounds(52) == 0x14);
  CHECK_FALSE(dwe::session::loadRoutineSounds(50).has_value());
  // script_interp.c:1845: 51 also loads the boss's own bank: readVBALLSection(5, 0x73).
  CHECK(dwe::session::loadRoutineSpeciesBank(51) ==
        dwe::session::RoutineSpeciesBank{.slot = 5, .type = 0x73});
  CHECK_FALSE(dwe::session::loadRoutineSpeciesBank(52).has_value());
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{}};
  REQUIRE(dwe::session::registerBossAndEndingLoadRoutines(scenes));
  for (const std::int64_t routine : {51, 52}) {
    const auto loaded = scenes.routines.call(routine);
    REQUIRE(loaded);
    CHECK(std::holds_alternative<dwe::game::story::RoutineGoesOn>(*loaded));
  }
  // Without a game nothing holds slot 8.
  CHECK_FALSE(scenes.routineSoundsMap.has_value());
}

TEST_CASE("routine 53 waits for the ending scene's sync and then ends the game") {
  using dwe::game::story::RoutineWaits;
  using dwe::game::story::TaskDone;
  using dwe::game::story::TaskRunning;
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{}};
  REQUIRE(dwe::session::registerStartEndingRoutine(scenes));
  const auto started = scenes.routines.call(53);
  REQUIRE(started);
  const auto* waits = std::get_if<RoutineWaits>(&*started);
  REQUIRE(waits != nullptr);
  // The scene is playing: tamerTickEnding has not set SOME_SCRIPT_SYNC_BIT yet.
  scenes.sync.begin();
  CHECK(std::holds_alternative<TaskRunning>(waits->task->tick()));
  CHECK(std::holds_alternative<TaskRunning>(waits->task->tick()));
  CHECK_FALSE(scenes.gameEnded);
  scenes.sync.finish();
  CHECK(std::holds_alternative<TaskDone>(waits->task->tick()));
  CHECK(scenes.gameEnded);
}

TEST_CASE("routine 54 asks for the ending's save and waits for it to close") {
  // script_interp.c:1833: gameClearSave, then scriptTickMainMenu until CURRENT_MENU is -1.
  const dwe::session::testing::TestPacks packs;
  auto story = std::make_unique<dwe::session::StoryAdapter>(
      dwe::session::StoryAdapterDesc{.files = &packs.files()});
  dwe::session::StoryAdapter& state = *story;
  std::vector<std::unique_ptr<dwe::session::SessionSystem>> systems;
  systems.push_back(std::move(story));
  const auto session = dwe::session::testing::newGameWith(packs, std::move(systems));
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{.story = &state}};
  REQUIRE(dwe::session::registerClearSaveRoutine(scenes));
  const auto started = scenes.routines.call(54);
  REQUIRE(started);
  REQUIRE(scenes.screenRequests.size() == 1);
  CHECK(scenes.screenRequests.front().screen == dwe::session::StoryScreen::ClearSave);
  CHECK(scenes.screenRequests.front().waits);
}

TEST_CASE("the game pack's ending gives record 39 as the ending's species with three techniques") {
  // main_menu.c: awardMachinedramonData: species 0x73 and the animations 0x2E, 0x30 and 0x32,
  // its moves[0], [2] and [4] (utils2.c: entityGetTechFromAnim).
  std::ifstream in{std::filesystem::path{DWE_GAME_PACK} / "rules/original/story_scenes.yaml"};
  REQUIRE(in);
  const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
  auto node = dwe::core::parseYaml(text);
  REQUIRE(node);
  const auto rules = dwe::session::readStoryScenesRules(
      {.name = "story_scenes", .owner = "original", .file = "story_scenes.yaml", .node = *node});
  REQUIRE_MESSAGE(rules.has_value(), rules.error().message);
  REQUIRE(rules->ending.has_value());
  CHECK(rules->ending->slot == 39);
  CHECK(rules->ending->species == "muge");
  CHECK(rules->ending->techniques ==
        std::vector<std::string>{"megaton_punch", "thunder_justice", "all_range_beam"});
}

TEST_CASE("routines 45 and 46 fade to white and back over the frames variable 254 holds") {
  const dwe::session::testing::TestPacks packs;
  auto story = std::make_unique<dwe::session::StoryAdapter>(
      dwe::session::StoryAdapterDesc{.files = &packs.files()});
  dwe::session::StoryAdapter& state = *story;
  std::vector<std::unique_ptr<dwe::session::SessionSystem>> systems;
  systems.push_back(std::move(story));
  const auto session = dwe::session::testing::newGameWith(packs, std::move(systems));
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{.story = &state}};
  scenes.rules.operandVariable = 254;
  REQUIRE(dwe::session::registerWhiteFadeRoutines(scenes));
  // ogre03's script 48: set_pstat(254, 40), call_routine(45) ... call_routine(46).
  REQUIRE(state.state().setVariable(254, 40));
  const auto out = scenes.routines.call(45);
  REQUIRE(out);
  CHECK(std::holds_alternative<dwe::game::story::RoutineGoesOn>(*out));
  REQUIRE(state.state().setVariable(254, 20));
  const auto in = scenes.routines.call(46);
  REQUIRE(in);
  CHECK(std::holds_alternative<dwe::game::story::RoutineGoesOn>(*in));
  const auto taken = takeAll(scenes);
  REQUIRE(taken.size() == 2);
  CHECK(std::get<WhiteFadeStarted>(taken[0]).toWhite);
  CHECK(std::get<WhiteFadeStarted>(taken[0]).frames == 40);
  CHECK_FALSE(std::get<WhiteFadeStarted>(taken[1]).toWhite);
  CHECK(std::get<WhiteFadeStarted>(taken[1]).frames == 20);
}

TEST_CASE(
    "routine 28 puts the mood of variable 248 over the character of 247, and 5 takes it away") {
  const dwe::session::testing::TestPacks packs;
  auto story = std::make_unique<dwe::session::StoryAdapter>(
      dwe::session::StoryAdapterDesc{.files = &packs.files()});
  dwe::session::StoryAdapter& state = *story;
  std::vector<std::unique_ptr<dwe::session::SessionSystem>> systems;
  systems.push_back(std::move(story));
  const auto session = dwe::session::testing::newGameWith(packs, std::move(systems));
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{.story = &state}};
  REQUIRE(dwe::session::registerMoodBubbleRoutine(scenes));
  // Script 199 (the canyon shop): set_pstat(247, 6), set_pstat(248, 3), call_routine(28); 248 = 5
  // before the next question takes it away.
  REQUIRE(state.state().setVariable(247, 6));
  REQUIRE(state.state().setVariable(248, 3));
  const auto shown = scenes.routines.call(28);
  REQUIRE(shown);
  CHECK(std::holds_alternative<dwe::game::story::RoutineGoesOn>(*shown));
  REQUIRE(state.state().setVariable(248, 5));
  REQUIRE(scenes.routines.call(28));
  const auto taken = takeAll(scenes);
  REQUIRE(taken.size() == 2);
  CHECK(std::get<MoodBubbleShown>(taken[0]).character == 6);
  CHECK(std::get<MoodBubbleShown>(taken[0]).mood == 3);
  CHECK(std::get<MoodBubbleShown>(taken[1]).character == 6);
  CHECK_FALSE(std::get<MoodBubbleShown>(taken[1]).mood.has_value());
}

TEST_CASE("routine 40 starts the final boss's charge on character 5 and goes on") {
  // script_interp.c:1856: EAB_startBuildup(ENTITY_TABLE[scriptIdToEntityId(5)]); mgen99's script
  // calls it right after set_digimon(115, slot 0).
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{}};
  REQUIRE(dwe::session::registerBossBuildupRoutine(scenes));
  const auto started = scenes.routines.call(40);
  REQUIRE(started);
  CHECK(std::holds_alternative<dwe::game::story::RoutineGoesOn>(*started));
  const auto taken = takeAll(scenes);
  REQUIRE(taken.size() == 1);
  CHECK(std::get<dwe::session::BossBuildupStarted>(taken[0]).character == 5);
}
