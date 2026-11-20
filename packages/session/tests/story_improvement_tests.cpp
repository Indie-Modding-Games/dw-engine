#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/story/story_runner.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_motion_api.hpp>
#include <dwe/session/story_scenes.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "packages/session/src/story_routines/routines.hpp"
#include "packages/session/src/story_scenes_context.hpp"
#include "session_fixture.hpp"
#include "story_lua_fixture.hpp"

namespace {

using dwe::session::StoryImprovement;
using dwe::session::StoryImprovementRoutine;
using dwe::session::testing::errorOf;
using story_play::Played;
using story_play::SectionRef;
using story_play::StoryPlay;

constexpr std::int64_t Operand = 254;
constexpr std::int64_t Answer = Operand;
constexpr auto AngemonZone = static_cast<std::int64_t>(StoryImprovement::AngemonZone);

// The Ice Sanctuary ground floor's script (93): the light freed (322), Angemon joined (220), the
// corridor band's section (0x34) and Angemon's event (0x05).
constexpr std::uint16_t IceSanctuaryScript = 93;
constexpr std::uint16_t CorridorBand = 0x34;
constexpr std::int64_t LightFreed = 322;
constexpr std::int64_t AngemonJoined = 220;

// A stand-in for story/093.lua: the band's section sets trigger 1 and the event sets trigger 2,
// so the tests tell which one ran.
constexpr std::string_view StandInModule = R"lua(-- dwx lua 3: a module of the tests
local story <const> = story
local ENDED <const> = story.ENDED
return { script = 93, sections = {
  [0x34] = function()
    story.set_trigger(1)
    return ENDED
  end,
  [0x05] = function()
    story.set_trigger(2)
    return ENDED
  end,
} }
)lua";

constexpr auto WhamonInWater = static_cast<std::int64_t>(StoryImprovement::WhamonInWater);

// The Secret Beach Cave's script (136): the Ogremon event's trigger (224), the map's entry section
// (0xFE) and the ride's arrival section (0x3C).
constexpr std::uint16_t BeachCaveScript = 136;
constexpr std::uint16_t MapEntry = 0xFE;
constexpr std::int64_t RideArrival = 0x3C;
constexpr std::int64_t NoArrival = 0xFF;
constexpr std::int64_t OgremonEvent = 224;

// A stand-in for story/136.lua: the entry section sets trigger 1, so the tests tell it ran.
constexpr std::string_view BeachCaveModule = R"lua(-- dwx lua 3: a module of the tests
local story <const> = story
local ENDED <const> = story.ENDED
return { script = 136, sections = {
  [0xfe] = function()
    story.set_trigger(1)
    return ENDED
  end,
} }
)lua";

std::string gamePackFile(std::string_view path) {
  const dwe::assets::DiskFileSystem disk;
  const auto bytes = disk.readFile(std::filesystem::path{DWE_GAME_PACK} / path);
  REQUIRE_MESSAGE(bytes.has_value(), errorOf(bytes));
  return {reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

// A stand-in script (base/story/<script>) under the game pack's own patch of it. Not movable: the
// VFS points into it.
class PatchPacks {
public:
  PatchPacks(std::string_view script, std::string_view standIn, std::string_view patch) {
    memory_.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
    memory_.addFile(std::format("base/story/{}", script), std::string{standIn});
    memory_.addFile("game/pack.yaml", "id: game\nversion: 0.1.0\nlayer: base\npriority: 1\n");
    memory_.addFile(std::format("game/{}", patch), gamePackFile(patch));
    std::vector<dwe::assets::PackLocation> locations;
    for (const std::string_view root : {"base", "game"}) {
      locations.push_back(dwe::assets::PackLocation{.fileSystem = &memory_,
                                                    .root = std::string{root},
                                                    .fillsGaps = false,
                                                    .origin = dwe::assets::PackOrigin::Engine});
    }
    auto vfs = dwe::assets::VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(vfs.has_value(), story_play::errorText(vfs));
    files_ = std::make_unique<dwe::assets::VirtualFileSystem>(std::move(*vfs));
  }
  PatchPacks(const PatchPacks&) = delete;
  PatchPacks& operator=(const PatchPacks&) = delete;
  PatchPacks(PatchPacks&&) = delete;
  PatchPacks& operator=(PatchPacks&&) = delete;
  ~PatchPacks() = default;

  [[nodiscard]] const dwe::assets::VirtualFileSystem& files() const { return *files_; }

private:
  dwe::assets::MemoryFileSystem memory_;
  std::unique_ptr<dwe::assets::VirtualFileSystem> files_;
};

// Steps on the corridor band with the light and Angemon's triggers so, the engine answering
// routine 261 with `on` for the zone (any other routine or improvement: 0).
Played stepOnBand(const PatchPacks& packs, bool on, bool lightFreed, bool angemonJoined) {
  StoryPlay play{packs.files()};
  REQUIRE(play.state().setTrigger(LightFreed, lightFreed));
  REQUIRE(play.state().setTrigger(AngemonJoined, angemonJoined));
  REQUIRE(play.vm().api().publish(
      dwe::session::StoryNamespace, dwe::session::story_api::CallRoutine,
      [&play, on](const dwe::script::ApiCall& call) -> dwe::core::Result<dwe::script::ApiValue> {
        const auto routine = call.integer(0);
        if (!routine) {
          return std::unexpected{routine.error()};
        }
        const auto asked = play.state().variable(Operand);
        if (!asked) {
          return std::unexpected{asked.error()};
        }
        const bool yes = *routine == StoryImprovementRoutine && *asked == AngemonZone && on;
        if (auto written = play.state().setVariable(Answer, yes ? 1 : 0); !written) {
          return std::unexpected{written.error()};
        }
        return dwe::script::ApiValue{};
      }));
  return play.play(SectionRef{.script = IceSanctuaryScript, .section = CorridorBand}, 1);
}

// Enters the Secret Beach Cave with the Ogremon event's trigger so, the engine answering routine
// 261 with `on` for Whamon in the water and routine 262 with `arrival`; the scripted moves are
// recorded.
Played enterBeachCave(const PatchPacks& packs, bool on, std::int64_t arrival, bool ogremonDone) {
  StoryPlay play{packs.files()};
  REQUIRE(play.state().setTrigger(OgremonEvent, ogremonDone));
  namespace motion = dwe::session::story_motion_api;
  for (const std::string_view move : {motion::MoveTo, motion::SetRotation, motion::WaitForEntity}) {
    REQUIRE(play.vm().api().publish(dwe::session::StoryNamespace, move,
                                    story_play::recordedCall(play.recorder(), std::string{move})));
  }
  REQUIRE(play.vm().api().publish(
      dwe::session::StoryNamespace, dwe::session::story_api::CallRoutine,
      [&play, on,
       arrival](const dwe::script::ApiCall& call) -> dwe::core::Result<dwe::script::ApiValue> {
        const auto routine = call.integer(0);
        if (!routine) {
          return std::unexpected{routine.error()};
        }
        const auto asked = play.state().variable(Operand);
        if (!asked) {
          return std::unexpected{asked.error()};
        }
        std::int64_t answer = 0;
        if (*routine == StoryImprovementRoutine) {
          answer = *asked == WhamonInWater && on ? 1 : 0;
        } else if (*routine == dwe::session::ArrivalSectionRoutine) {
          answer = arrival;
        }
        if (auto written = play.state().setVariable(Answer, answer); !written) {
          return std::unexpected{written.error()};
        }
        return dwe::script::ApiValue{};
      }));
  return play.play(SectionRef{.script = BeachCaveScript, .section = MapEntry}, 1);
}

} // namespace

TEST_CASE("routine 261 tells a story patch whether a story improvement is on") {
  const dwe::session::testing::TestPacks packs;
  auto owned = std::make_unique<dwe::session::StoryAdapter>(
      dwe::session::StoryAdapterDesc{.files = &packs.files()});
  dwe::session::StoryAdapter* story = owned.get();
  std::vector<std::unique_ptr<dwe::session::SessionSystem>> systems;
  systems.push_back(std::move(owned));
  const auto session = dwe::session::testing::newGameWith(packs, std::move(systems));
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{.story = story}};
  scenes.rules.operandVariable = Operand;
  REQUIRE(dwe::session::registerStoryImprovementRoutine(scenes));
  constexpr std::int32_t OperandVariable = Operand;
  const auto ask = [&](std::int32_t improvement) {
    REQUIRE(story->state().setVariable(OperandVariable, improvement));
    REQUIRE(scenes.routines.call(StoryImprovementRoutine));
    return story->state().variable(OperandVariable).value_or(-1);
  };

  // On until the options turn it off.
  constexpr auto Zone = static_cast<std::int32_t>(AngemonZone);
  CHECK(ask(Zone) == 1);
  scenes.improvementsOff.insert(StoryImprovement::AngemonZone);
  CHECK(ask(Zone) == 0);
  scenes.improvementsOff.erase(StoryImprovement::AngemonZone);
  CHECK(ask(Zone) == 1);
  constexpr auto Whamon = static_cast<std::int32_t>(WhamonInWater);
  CHECK(ask(Whamon) == 1);
  scenes.improvementsOff.insert(StoryImprovement::WhamonInWater);
  CHECK(ask(Whamon) == 0);
  CHECK(ask(Zone) == 1);
  // An improvement the engine does not know is off.
  CHECK(ask(0) == 0);
  CHECK(ask(99) == 0);
}

TEST_CASE("the Angemon zone patch plays the event on the corridor band once the light is freed") {
  const PatchPacks packs{"093.lua", StandInModule, "story/patches/angemon_zone_093.lua"};
  const auto ran = [](const Played& played, std::string_view trigger) {
    return std::ranges::contains(played.events, std::string{trigger});
  };

  // On, the light freed and Angemon not in the city yet: the band plays the event (section 5),
  // as the original's stairs up from the trapdoor passage do.
  const Played event = stepOnBand(packs, true, true, false);
  CHECK(event.status == "ended");
  CHECK(ran(event, "jump_to_file(93,5)"));
  CHECK(ran(event, "trigger 2 = 1"));
  CHECK_FALSE(ran(event, "trigger 1 = 1"));

  // The original's band in every other case: before the light, after Angemon joined, and with the
  // option off (vanilla).
  for (const auto& [on, light, joined] :
       {std::tuple{true, false, false}, std::tuple{true, true, true},
        std::tuple{false, true, false}}) {
    CAPTURE(on);
    CAPTURE(light);
    CAPTURE(joined);
    const Played original = stepOnBand(packs, on, light, joined);
    CHECK(original.status == "ended");
    CHECK(ran(original, "trigger 1 = 1"));
    CHECK_FALSE(ran(original, "trigger 2 = 1"));
    CHECK(original.fileJumps == 0);
  }
}

TEST_CASE("routine 262 tells a map's entry section the arrival section that runs after it") {
  const dwe::session::testing::TestPacks packs;
  auto owned = std::make_unique<dwe::session::StoryAdapter>(
      dwe::session::StoryAdapterDesc{.files = &packs.files()});
  dwe::session::StoryAdapter* story = owned.get();
  std::vector<std::unique_ptr<dwe::session::SessionSystem>> systems;
  systems.push_back(std::move(owned));
  const auto session = dwe::session::testing::newGameWith(packs, std::move(systems));
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{.story = story}};
  scenes.rules.operandVariable = Operand;
  REQUIRE(dwe::session::registerArrivalSectionRoutine(scenes));
  REQUIRE(story->runner() != nullptr);
  const auto ask = [&] {
    REQUIRE(scenes.routines.call(dwe::session::ArrivalSectionRoutine));
    return story->state().variable(static_cast<std::int32_t>(Operand)).value_or(-1);
  };

  // A walk in: none.
  CHECK(ask() == NoArrival);
  // A ride's warp_to the field took: its arrival section.
  story->runner()->arriveWith(static_cast<std::uint16_t>(RideArrival));
  CHECK(ask() == RideArrival);
}

TEST_CASE("the Whamon patch puts Whamon in the water before the Ogremon event") {
  const PatchPacks packs{"136.lua", BeachCaveModule, "story/patches/whamon_water_136.lua"};
  const auto has = [](const Played& played, std::string_view event) {
    return std::ranges::contains(played.events, std::string{event});
  };

  // On, a walk in before the Ogremon event: Whamon turns to the shore and slides into the water
  // off the confirm cell, and the original's entry runs after.
  const Played improved = enterBeachCave(packs, true, NoArrival, false);
  CHECK(improved.status == "ended");
  const auto at = [&improved](std::string_view event) {
    return std::ranges::find(improved.events, std::string{event}) - improved.events.begin();
  };
  CHECK(has(improved, "entity_set_rotation(5,3584)"));
  CHECK(has(improved, "entity_move_to(5,1500,2800,8,0)"));
  CHECK(has(improved, "wait_for_entity(5)"));
  CHECK(has(improved, "trigger 1 = 1"));
  // The turn before the slide, which would end it; the original's entry after both.
  CHECK(at("entity_set_rotation(5,3584)") < at("entity_move_to(5,1500,2800,8,0)"));
  CHECK(at("wait_for_entity(5)") < at("trigger 1 = 1"));

  // The original's entry alone: with the option off, on the ride's arrival (its own section
  // brings Whamon) and after the Ogremon event (the original's entry brings it).
  for (const auto& [on, arrival, done] :
       {std::tuple{false, NoArrival, false}, std::tuple{true, RideArrival, false},
        std::tuple{true, NoArrival, true}}) {
    CAPTURE(on);
    CAPTURE(arrival);
    CAPTURE(done);
    const Played original = enterBeachCave(packs, on, arrival, done);
    CHECK(original.status == "ended");
    CHECK(has(original, "trigger 1 = 1"));
    CHECK(std::ranges::none_of(original.events, [](const std::string& event) {
      return event.starts_with("entity_") || event.starts_with("wait_for_entity");
    }));
  }
}
