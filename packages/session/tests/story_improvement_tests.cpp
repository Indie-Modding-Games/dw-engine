#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_scenes.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
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

std::string gamePackFile(std::string_view path) {
  const dwe::assets::DiskFileSystem disk;
  const auto bytes = disk.readFile(std::filesystem::path{DWE_GAME_PACK} / path);
  REQUIRE_MESSAGE(bytes.has_value(), errorOf(bytes));
  return {reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

// The stand-in script under the game pack's own patch. Not movable: the VFS points into it.
class ZonePacks {
public:
  ZonePacks() {
    memory_.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
    memory_.addFile("base/story/093.lua", std::string{StandInModule});
    memory_.addFile("game/pack.yaml", "id: game\nversion: 0.1.0\nlayer: base\npriority: 1\n");
    memory_.addFile("game/story/patches/angemon_zone_093.lua",
                    gamePackFile("story/patches/angemon_zone_093.lua"));
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
  ZonePacks(const ZonePacks&) = delete;
  ZonePacks& operator=(const ZonePacks&) = delete;
  ZonePacks(ZonePacks&&) = delete;
  ZonePacks& operator=(ZonePacks&&) = delete;
  ~ZonePacks() = default;

  [[nodiscard]] const dwe::assets::VirtualFileSystem& files() const { return *files_; }

private:
  dwe::assets::MemoryFileSystem memory_;
  std::unique_ptr<dwe::assets::VirtualFileSystem> files_;
};

// Steps on the corridor band with the light and Angemon's triggers so, the engine answering
// routine 261 with `on` for the zone (any other routine or improvement: 0).
Played stepOnBand(const ZonePacks& packs, bool on, bool lightFreed, bool angemonJoined) {
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
  // An improvement the engine does not know is off.
  CHECK(ask(0) == 0);
  CHECK(ask(99) == 0);
}

TEST_CASE("the Angemon zone patch plays the event on the corridor band once the light is freed") {
  const ZonePacks packs;
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
