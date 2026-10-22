#include <dwe/game/story/story_state_yaml.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <format>
#include <memory>
#include <string>
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

// The new game sets trigger 9; its opening, which field/warps' start (twna01 exit 1) skips, sets 54, 55 and 75; game
// code may raise either starting partner.
const std::string newGameStory =
    std::format("map: twna01\nscript: 1\ntriggers_set: [{}]\ntriggers_unknown: [{}, {}]\n"
                "opening: {{exit: 9, section: 54, to: {{map: topn01, exit: 9}}, sets: [54, 55, 75]}}\n",
                NewGameTrigger, FirstRaisedTrigger + FirstPartnerNumber, FirstRaisedTrigger + OtherPartnerNumber);

struct StorySession {
    std::unique_ptr<dwe::session::GameSession> session;
    StoryAdapter* story = nullptr;
};

StorySession storySession(const TestPacks& packs) {
    auto adapter = std::make_unique<StoryAdapter>(dwe::session::StoryAdapterDesc{.files = &packs.files()});
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
    const TestPacks packs{{"data/digimon.yaml", digimonTable}, {"data/new_game_story.yaml", newGameStory}};
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

TEST_CASE("the story is saved in its section and loaded back") {
    const TestPacks packs{{"data/digimon.yaml", digimonTable}, {"data/new_game_story.yaml", newGameStory}};
    const auto saved = storySession(packs);
    REQUIRE(saved.story->state().setTrigger(PlayedTrigger, true).has_value());
    auto file = saved.session->saveFile();
    REQUIRE_MESSAGE(file.has_value(), errorOf(file));
    REQUIRE(file->sections.contains(std::string{StorySaveName}));
    CHECK(file->sections.at(std::string{StorySaveName}).version == dwe::game::story::StoryStateVersion);

    const auto loaded = storySession(packs);
    CHECK_FALSE(isSet(*loaded.story, PlayedTrigger));
    const auto read = loaded.session->loadFile(*file);
    REQUIRE_MESSAGE(read.has_value(), errorOf(read));
    CHECK(loaded.story->state() == saved.story->state());
}

TEST_CASE("a save made before the story section loads, with the story as a new game leaves it") {
    const TestPacks packs{{"data/digimon.yaml", digimonTable}, {"data/new_game_story.yaml", newGameStory}};
    const auto saved = storySession(packs);
    auto file = saved.session->saveFile();
    REQUIRE_MESSAGE(file.has_value(), errorOf(file));
    file->sections.erase(std::string{StorySaveName});

    const auto loaded = storySession(packs);
    REQUIRE(loaded.story->state().setTrigger(PlayedTrigger, true).has_value());
    const auto read = loaded.session->loadFile(*file);
    REQUIRE_MESSAGE(read.has_value(), errorOf(read));
    // What the new game sets, and nothing the loaded session had played; which partner came first is not saved.
    CHECK(isSet(*loaded.story, NewGameTrigger));
    CHECK(isSet(*loaded.story, 55));
    CHECK_FALSE(isSet(*loaded.story, PlayedTrigger));
    CHECK_FALSE(isSet(*loaded.story, FirstRaisedTrigger + FirstPartnerNumber));
    // The rest of the save loads as it was.
    CHECK(loaded.session->partner().state() == saved.session->partner().state());
}
