#include <dwe/game/field/npcs.hpp>
#include <dwe/game/save/migration.hpp>
#include <dwe/game/story/story_state_yaml.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_scenes.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "session_fixture.hpp"

using dwe::session::FirstRaisedTrigger;
using dwe::session::SessionSystem;
using dwe::session::StoryAdapter;
using dwe::session::StorySaveName;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

// Made-up numbers, not the original's: the first partner is number 3, the other starting one 17.
constexpr std::int32_t FirstPartnerNumber = 3;
constexpr std::int32_t OtherPartnerNumber = 17;
constexpr std::int32_t NewGameTrigger = 9;
constexpr std::int32_t PlayedTrigger = 300;

std::string digimonRow(std::string_view id, std::int32_t index) {
  return std::format(R"(- id: "{}"
  index: {}
  name: "{}"
  file_name: null
  bone_count: 1
  radius: 100
  height: 200
  type: 1
  level: 3
  special: [0, null, null]
  drop_item: null
  drop_chance: 0
  moves: [null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null]
)",
                     id, index, id);
}

const std::string digimonTable =
    "digimon:\n" + digimonRow("agum", FirstPartnerNumber) + digimonRow("other", OtherPartnerNumber);

// The new game sets trigger 9; its opening, which field/warps' start (twna01 exit 1) skips, sets
// 54, 55 and 75; game code may raise either starting partner.
const std::string newGameStory = std::format(
    "map: twna01\nscript: 1\ntriggers_set: [{}]\ntriggers_unknown: [{}, {}]\n"
    "opening: {{exit: 9, section: 54, to: {{map: topn01, exit: 9}}, sets: [54, 55, 75]}}\n",
    NewGameTrigger, FirstRaisedTrigger + FirstPartnerNumber,
    FirstRaisedTrigger + OtherPartnerNumber);

struct StorySession {
  std::unique_ptr<dwe::session::GameSession> session;
  StoryAdapter* story = nullptr;
};

StorySession storySession(const TestPacks& packs) {
  auto adapter =
      std::make_unique<StoryAdapter>(dwe::session::StoryAdapterDesc{.files = &packs.files()});
  StoryAdapter* story = adapter.get();
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(adapter));
  return {.session = newGameWith(packs, std::move(systems)), .story = story};
}

bool isSet(const StoryAdapter& story, std::int32_t trigger) {
  return story.state().trigger(trigger).value_or(false);
}

} // namespace

TEST_CASE(
    "a new game's story sets what data/new_game_story and its skipped opening set, and raises the first partner") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable},
                        {"data/new_game_story.yaml", newGameStory}};
  const auto [session, story] = storySession(packs);
  CHECK(isSet(*story, NewGameTrigger));
  for (const std::int32_t opening : {54, 55, 75}) {
    CHECK(isSet(*story, opening));
  }
  // main.c: main raises the partner the game starts with, and no other.
  CHECK(isSet(*story, FirstRaisedTrigger + FirstPartnerNumber));
  CHECK_FALSE(isSet(*story, FirstRaisedTrigger + OtherPartnerNumber));
  CHECK(story->state().triggersSet() == 5);
}

TEST_CASE("without data/new_game_story a new game's story has only the first partner raised") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable}};
  const auto [session, story] = storySession(packs);
  CHECK(story->state().triggersSet() == 1);
  CHECK(isSet(*story, FirstRaisedTrigger + FirstPartnerNumber));
}

TEST_CASE("the player page counts the medals the story's triggers hold") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable}};
  const auto [session, story] = storySession(packs);
  CHECK(story->medalCount() == 0);
  using dwe::session::FirstMedalTrigger;
  using dwe::session::MedalCount;
  REQUIRE(story->state().setTrigger(FirstMedalTrigger, true).has_value());
  REQUIRE(story->state().setTrigger(FirstMedalTrigger + MedalCount - 1, true).has_value());
  // script_common.c: hasMedal; renderPlayerInfoView counts medals 0 to 17 only.
  REQUIRE(story->state().setTrigger(FirstMedalTrigger + MedalCount, true).has_value());
  CHECK(story->medalCount() == 2);
}

TEST_CASE("the player page shows the medals and the item boxes the story's triggers hold") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable}};
  const auto [session, story] = storySession(packs);
  using dwe::session::FirstMedalTrigger;
  using dwe::session::FirstPlayerItemTrigger;
  // script_common.c: hasMedal is trigger 0x16c + medal.
  REQUIRE(story->state().setTrigger(FirstMedalTrigger + 3, true).has_value());
  CHECK(story->hasMedal(3));
  CHECK_FALSE(story->hasMedal(2));
  CHECK_FALSE(story->hasMedal(-1));
  // renderPlayerInfoView: the item boxes are triggers 45 (the old rod) to 48.
  CHECK(FirstPlayerItemTrigger == 45);
  REQUIRE(story->state().setTrigger(FirstPlayerItemTrigger + 1, true).has_value());
  CHECK(story->hasPlayerItem(1));
  CHECK_FALSE(story->hasPlayerItem(0));
  CHECK_FALSE(story->hasPlayerItem(4));
}

TEST_CASE("the story is saved in its section and loaded back") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable},
                        {"data/new_game_story.yaml", newGameStory}};
  const auto saved = storySession(packs);
  REQUIRE(saved.story->state().setTrigger(PlayedTrigger, true).has_value());
  auto file = saved.session->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  REQUIRE(file->sections.contains(std::string{StorySaveName}));
  CHECK(file->sections.at(std::string{StorySaveName}).version ==
        dwe::game::story::StoryStateVersion);

  const auto loaded = storySession(packs);
  CHECK_FALSE(isSet(*loaded.story, PlayedTrigger));
  const auto read = loaded.session->loadFile(*file);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK(loaded.story->state() == saved.story->state());
}

TEST_CASE("a save neither keeps nor brings back the trigger of a fight that cannot be fled") {
  constexpr std::int32_t FixedFightTrigger = 1;
  const TestPacks packs{{"data/digimon.yaml", digimonTable},
                        {"data/new_game_story.yaml", newGameStory}};
  const auto unaware = storySession(packs);
  REQUIRE(unaware.story->state().setTrigger(FixedFightTrigger, true).has_value());
  REQUIRE(unaware.story->state().setTrigger(PlayedTrigger, true).has_value());
  auto stale = unaware.session->saveFile();
  REQUIRE_MESSAGE(stale.has_value(), errorOf(stale));

  const auto loaded = storySession(packs);
  loaded.story->setFixedFightTrigger(FixedFightTrigger);
  const auto read = loaded.session->loadFile(*stale);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK_FALSE(isSet(*loaded.story, FixedFightTrigger));
  CHECK(isSet(*loaded.story, PlayedTrigger));

  auto adapter =
      std::make_unique<StoryAdapter>(dwe::session::StoryAdapterDesc{.files = &packs.files()});
  StoryAdapter* story = adapter.get();
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(adapter));
  systems.push_back(
      std::make_unique<dwe::session::StoryScenes>(dwe::session::StoryScenesDesc{.story = story}));
  const auto fighting = newGameWith(packs, std::move(systems));
  REQUIRE(story->state().setTrigger(FixedFightTrigger, true).has_value());
  REQUIRE(story->state().setTrigger(PlayedTrigger, true).has_value());
  auto midFight = fighting->saveFile();
  REQUIRE_MESSAGE(midFight.has_value(), errorOf(midFight));
  CHECK(isSet(*story, FixedFightTrigger));
  const auto reloaded = storySession(packs);
  const auto again = reloaded.session->loadFile(*midFight);
  REQUIRE_MESSAGE(again.has_value(), errorOf(again));
  CHECK_FALSE(isSet(*reloaded.story, FixedFightTrigger));
  CHECK(isSet(*reloaded.story, PlayedTrigger));
}

TEST_CASE("a save made before the story section loads, with the story as a new game leaves it") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable},
                        {"data/new_game_story.yaml", newGameStory}};
  const auto saved = storySession(packs);
  auto file = saved.session->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  file->sections.erase(std::string{StorySaveName});

  const auto loaded = storySession(packs);
  REQUIRE(loaded.story->state().setTrigger(PlayedTrigger, true).has_value());
  const auto read = loaded.session->loadFile(*file);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  // What the new game sets, and nothing the loaded session had played; which partner came first is
  // not saved.
  CHECK(isSet(*loaded.story, NewGameTrigger));
  CHECK(isSet(*loaded.story, 55));
  CHECK_FALSE(isSet(*loaded.story, PlayedTrigger));
  CHECK_FALSE(isSet(*loaded.story, FirstRaisedTrigger + FirstPartnerNumber));
  // The rest of the save loads as it was.
  CHECK(loaded.session->partner().state() == saved.session->partner().state());
}

namespace {

// A made-up ruleset that extends the original with room for a mod's story: 64 extra triggers, 4
// extra variables up to 100000, and raised triggers from 800 on for 32 species past the base's 63.
constexpr std::string_view ModdedRuleset =
    "id: modded\nname: \"Modded\"\nversion: 0.1.0\nextends: original\nsections: [story]\n";
constexpr std::string_view ModdedStory = R"(ticks_per_frame: 3
variable_max: 255
frozen_variable: 0
frozen_value: 3
talk_facing_trigger: 44
show_name_variable: 6
show_name_by_event: 0
show_name_always: 1
show_name_never: 2
capacity:
  extra_triggers: 64
  extra_variables: 4
  extra_variable_max: 100000
  extra_raised: {first_trigger: 800, species: 32}
)";

StorySession moddedStorySession(const TestPacks& packs) {
  auto adapter =
      std::make_unique<StoryAdapter>(dwe::session::StoryAdapterDesc{.files = &packs.files()});
  StoryAdapter* story = adapter.get();
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(adapter));
  return {.session = newGameWith(packs, std::move(systems), nullptr, 7, "modded"), .story = story};
}

} // namespace

TEST_CASE("a ruleset's capacity sizes the story and gives more species raised triggers") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable},
                        {"data/new_game_story.yaml", newGameStory},
                        {"rules/modded/ruleset.yaml", ModdedRuleset},
                        {"rules/modded/story.yaml", ModdedStory}};
  const auto saved = moddedStorySession(packs);
  StoryAdapter& story = *saved.story;
  CHECK(story.state().triggerCount() == 864);
  CHECK(story.state().variableCount() == 260);
  CHECK(story.statePort()->triggerCount() == 864);

  // Species 63 + 7 is raised as trigger 807; one past the range is kept apart, as before.
  story.markRaised(70);
  CHECK(isSet(story, 807));
  CHECK(story.raised(70));
  story.markRaised(95);
  CHECK_FALSE(story.state().trigger(832).value());
  CHECK(story.raised(95));
  CHECK(story.raisedSpecies().contains(70));
  CHECK(story.raisedSpecies().contains(95));

  REQUIRE(story.state().setVariable(259, 90'000).has_value());
  auto file = saved.session->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  const auto loaded = moddedStorySession(packs);
  const auto read = loaded.session->loadFile(*file);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK(isSet(*loaded.story, 807));
  CHECK(loaded.story->state().variable(259).value() == 90'000);
}

TEST_CASE("co-op keeps a player's own story facts out of the shared world") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable},
                        {"data/new_game_story.yaml", newGameStory},
                        {"rules/modded/ruleset.yaml", ModdedRuleset},
                        {"rules/modded/story.yaml", ModdedStory}};
  const auto session = moddedStorySession(packs);
  const StoryAdapter& story = *session.story;
  // The raised species (the base's and the ruleset's), the medals, the item boxes and the talk's
  // facing are the player's own.
  for (const std::int32_t own :
       {FirstRaisedTrigger, FirstRaisedTrigger + 62, 800, 831, dwe::session::FirstMedalTrigger,
        dwe::session::FirstMedalTrigger + 17, dwe::session::FirstPlayerItemTrigger, 48, 44}) {
    CHECK_MESSAGE(story.playerOwnTrigger(own), own);
  }
  // Chests, recruits, doors and story events are the world's.
  for (const std::int32_t world : {0, 43, 49, 100, FirstRaisedTrigger + 63, 832, 863}) {
    CHECK_FALSE_MESSAGE(story.playerOwnTrigger(world), world);
  }
  // The freeze and the next map's name follow each player's scripts.
  CHECK(story.playerOwnVariable(0));
  CHECK(story.playerOwnVariable(6));
  CHECK_FALSE(story.playerOwnVariable(1));
  CHECK_FALSE(story.playerOwnVariable(259));
}

TEST_CASE("a story section saved at version 1 still loads") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable},
                        {"data/new_game_story.yaml", newGameStory}};
  const auto saved = storySession(packs);
  REQUIRE(saved.story->state().setTrigger(PlayedTrigger, true).has_value());
  REQUIRE(saved.story->state().setVariable(20, 9).has_value());
  auto file = saved.session->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  auto& section = file->sections.at(std::string{StorySaveName});
  // What a version 1 save holds: the 100 trigger bytes and the 35 variables, nothing else.
  section.version = 1;
  section.state = dwe::game::save::withoutKey(section.state, "extra_variables");

  const auto loaded = storySession(packs);
  const auto read = loaded.session->loadFile(*file);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK(isSet(*loaded.story, PlayedTrigger));
  CHECK(loaded.story->state().variable(20).value() == 9);
}

TEST_CASE("a confirm cell pressed between frames is asked for at the next frame") {
  // tamer.c: checkMapInteraction reads the press once a frame; the field reads it on any tick.
  dwe::session::FrameCells cells;
  const std::vector<std::int32_t> step{51};
  const std::vector<std::int32_t> pressed{81};
  CHECK(cells.take(pressed, 81, false).empty());
  CHECK(cells.take({}, std::nullopt, false).empty());
  CHECK(cells.take({}, std::nullopt, true) == std::vector<std::int32_t>{81});
  // Asked once: the next frame has it no more.
  CHECK(cells.take({}, std::nullopt, true).empty());
  // A step cell asks on the frame's own tick only, and a press on the frame tick is not doubled.
  CHECK(cells.take(step, std::nullopt, false).empty());
  CHECK(cells.take({}, std::nullopt, true).empty());
  CHECK(cells.take(pressed, 81, true) == std::vector<std::int32_t>{81});
  // A new map forgets a press its frame did not reach.
  CHECK(cells.take(pressed, 81, false).empty());
  cells.clear();
  CHECK(cells.take({}, std::nullopt, true).empty());
}

namespace {

// A stand-in for the plains' map entry (MAPHEAD 0x13, map 19): its roll places Mamemon (trigger 1
// here) until Mamemon joined (213), then a later roll that the tests compare. Map 20's entry rolls
// the same, but no roaming rare roll is listed for it.
constexpr std::string_view MapHeadModule = R"lua(-- dwx lua 3: a module of the tests
local story <const> = story
local ENDED <const> = story.ENDED
local function plains()
  story.store_random(100, 99)
  if story.var(100) <= 2 and not story.trigger(213) then
    story.set_trigger(1)
  end
  story.store_random(101, 99)
  return ENDED
end
return { script = 0, sections = { [19] = plains, [20] = plains } }
)lua";
constexpr std::int32_t Plains = 19;
constexpr std::int32_t Unlisted = 20;
constexpr std::int32_t Placed = 1;
constexpr std::int32_t MamemonJoined = 213;
constexpr int Entries = 2000;

struct MapEntries {
  int placed = 0;
  std::vector<std::int32_t> laterRolls;
};

MapEntries enterRepeatedly(std::optional<std::int32_t> percent, std::int32_t map,
                           bool joined = false) {
  const TestPacks packs{{"data/digimon.yaml", digimonTable},
                        {"data/new_game_story.yaml", newGameStory},
                        {"story/000.lua", MapHeadModule}};
  const auto [session, story] = storySession(packs);
  story->setRoamingRareChance(percent);
  REQUIRE(story->state().setTrigger(MamemonJoined, joined));
  MapEntries entries;
  for (int entry = 0; entry < Entries; ++entry) {
    REQUIRE(story->state().setTrigger(Placed, false));
    story->mapEntered(map);
    for (int tick = 0; tick < 30 && story->scriptRunning(); ++tick) {
      session->tick(dwe::session::testing::Tick);
    }
    REQUIRE_FALSE(story->scriptRunning());
    entries.placed += isSet(*story, Placed) ? 1 : 0;
    entries.laterRolls.push_back(story->state().variable(101).value_or(-1));
  }
  return entries;
}

} // namespace

TEST_CASE("a roaming rare's tuned chance places it about that often, with the original's draws") {
  const MapEntries original = enterRepeatedly(std::nullopt, Plains);
  const MapEntries tenPercent = enterRepeatedly(10, Plains);
  CHECK(original.placed > Entries * 15 / 1000);
  CHECK(original.placed < Entries * 45 / 1000);
  CHECK(tenPercent.placed > Entries * 7 / 100);
  CHECK(tenPercent.placed < Entries * 13 / 100);
  CHECK(tenPercent.laterRolls == original.laterRolls);
}

TEST_CASE("the tuned chance leaves the original's conditions and other rolls alone") {
  CHECK(enterRepeatedly(50, Plains, true).placed == 0);
  const MapEntries unlisted = enterRepeatedly(50, Unlisted);
  const MapEntries original = enterRepeatedly(std::nullopt, Unlisted);
  CHECK(unlisted.placed == original.placed);
  CHECK(unlisted.laterRolls == original.laterRolls);
}

namespace {

// A map whose entry names script 1, whose section 7 (an NPC's) runs a few frames.
constexpr std::string_view EventMapHead = R"lua(-- dwx lua 3: a module of the tests
local story <const> = story
return { script = 0, sections = { [19] = function()
  story.set_script(1, 19)
  return story.ENDED
end } }
)lua";
constexpr std::string_view EventScript = R"lua(-- dwx lua 3: a module of the tests
local story <const> = story
return { script = 1, sections = {
  [0] = function() return story.ENDED end,
  [7] = function()
    story.delay(5)
    return story.ENDED
  end,
} }
)lua";

} // namespace

TEST_CASE("co-op: an NPC in another player's event is left be, and this game's event names its "
          "NPC while it runs (#417)") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable},
                        {"data/new_game_story.yaml", newGameStory},
                        {"story/000.lua", EventMapHead},
                        {"story/001.lua", EventScript}};
  const auto [session, story] = storySession(packs);
  const auto settle = [&] {
    for (int tick = 0; tick < 120 && story->scriptRunning(); ++tick) {
      session->tick(dwe::session::testing::Tick);
    }
  };
  story->mapEntered(19);
  settle();
  REQUIRE_FALSE(story->scriptRunning());
  const std::vector<dwe::game::field::NpcContact> touch{
      {.scriptId = 7, .entity = {}, .cause = dwe::game::field::NpcContactCause::Touch}};
  const std::vector<std::int32_t> otherEvent{7};
  story->runNpcContacts(touch, otherEvent);
  CHECK_FALSE(story->scriptRunning());
  CHECK_FALSE(story->eventNpc().has_value());

  story->runNpcContacts(touch);
  REQUIRE(story->scriptRunning());
  CHECK(story->eventNpc() == std::optional<std::int32_t>{7});
  settle();
  CHECK_FALSE(story->scriptRunning());
  CHECK_FALSE(story->eventNpc().has_value());
}
