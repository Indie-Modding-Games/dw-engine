#include <dwe/game/partner/state.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/session/city_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/medals_adapter.hpp>
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

using dwe::game::save::MemorySaveStorage;
using dwe::game::save::SlotId;
using dwe::game::save::SlotKind;
using dwe::session::CityAdapter;
using dwe::session::FirstMedalTrigger;
using dwe::session::MedalsAdapter;
using dwe::session::SessionSystem;
using dwe::session::StoryAdapter;
using dwe::session::TamerLevelChange;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

std::string digimonRow(std::string_view id, std::int32_t index, std::int32_t level) {
  return std::format(R"(  - id: {}
    index: {}
    name: {}
    file_name: X
    bone_count: 1
    radius: 100
    height: 200
    type: 1
    level: {}
    special: [0, null, null]
    drop_item: null
    drop_chance: 0
    moves: [null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null]
)",
                     id, index, id, level);
}

// The first partner (3, a rookie), a champion (5) and Numemon's number (11), a champion too.
const std::string digimonTable =
    "digimon:\n" + digimonRow("agum", 3, 3) + digimonRow("champ", 5, 4) + digimonRow("nume", 11, 4);

struct ProgressSession {
  std::unique_ptr<dwe::session::GameSession> session;
  StoryAdapter* story = nullptr;
  CityAdapter* city = nullptr;
  MedalsAdapter* medals = nullptr;
};

ProgressSession progressSession(const TestPacks& packs, MemorySaveStorage* storage = nullptr) {
  auto story =
      std::make_unique<StoryAdapter>(dwe::session::StoryAdapterDesc{.files = &packs.files()});
  auto city = std::make_unique<CityAdapter>(*story);
  auto medals = std::make_unique<MedalsAdapter>(dwe::session::MedalsAdapterDesc{
      .story = story.get(), .battle = nullptr, .inventory = nullptr, .cards = nullptr});
  ProgressSession made{
      .session = nullptr, .story = story.get(), .city = city.get(), .medals = medals.get()};
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(story));
  systems.push_back(std::move(city));
  systems.push_back(std::move(medals));
  made.session = newGameWith(packs, std::move(systems), storage);
  return made;
}

std::int32_t prosperity(const StoryAdapter& story) {
  return story.state().variable(1).value_or(0xff);
}

void set(StoryAdapter& story, std::int32_t trigger) {
  REQUIRE(story.state().setTrigger(trigger, true).has_value());
}

} // namespace

TEST_CASE("a save of the player's rebuilds the prosperity from the recruit triggers first") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable}};
  MemorySaveStorage storage;
  const auto [session, story, city, medals] = progressSession(packs, &storage);
  // A new game starts with none (main.c: main runs recalculatePPandArena before the game loop).
  CHECK(prosperity(*story) == 0);
  // The recruit scripts added their own points; the save counts the triggers by level.
  REQUIRE(story->state().setVariable(1, 9).has_value());
  set(*story, 200 + 5);
  set(*story, 200 + 11);
  REQUIRE(story->state().setVariable(3, 0xff).has_value());
  set(*story, 38);
  const auto saved = session->save(SlotId{.kind = SlotKind::Manual, .index = 1});
  REQUIRE_MESSAGE(saved.has_value(), errorOf(saved));
  // The champion adds 4 - 2, Numemon 1.
  CHECK(prosperity(*story) == 3);
  // No cup registered: the registration is cleared too.
  CHECK_FALSE(story->state().trigger(38).value());
}

TEST_CASE("a load rebuilds the prosperity, whatever the save held") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable}};
  const auto saved = progressSession(packs);
  set(*saved.story, 200 + 5);
  REQUIRE(saved.story->state().setVariable(1, 40).has_value());
  auto file = saved.session->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));

  const auto loaded = progressSession(packs);
  const auto read = loaded.session->loadFile(*file);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK(prosperity(*loaded.story) == 2);
}

TEST_CASE("a finished game continues on the File City screen of how the city grew") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable}};
  const auto [session, story, city, medals] = progressSession(packs);
  CHECK(city->clearedMap() == 168);
  set(*story, 0xdc);
  set(*story, 0xd6);
  set(*story, 0xe1);
  CHECK(city->clearedMap() == 0xb0);
}

TEST_CASE("the city lists who can live in it, who joined and the prosperity the scripts see") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable}};
  const auto [session, story, city, medals] = progressSession(packs);
  set(*story, 200 + 11);
  REQUIRE(story->state().setVariable(1, 7).has_value());
  const auto residents = city->residents();
  REQUIRE(residents.size() == 3);
  CHECK(residents[0].species == 3);
  CHECK_FALSE(residents[0].recruited);
  CHECK(residents[1].species == 5);
  CHECK(residents[1].points == 2);
  CHECK(residents[2].species == 11);
  CHECK(residents[2].points == 1);
  CHECK(residents[2].recruited);
  // The variable as the scripts left it, until the next load or save rebuilds it.
  CHECK(city->prosperity() == 7);
}

TEST_CASE("the field's medal check gives a medal once and leaves its notice due") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable}};
  const auto [session, story, city, medals] = progressSession(packs);
  medals->check(*session);
  CHECK_FALSE(medals->awardPending());
  auto counters = session->partner().state().evolution;
  counters.fishCaught = 100;
  session->partner().setEvolutionCounters(counters);
  medals->check(*session);
  CHECK(medals->awardPending());
  CHECK(story->state().trigger(FirstMedalTrigger + 9).value());
  // The player page reads it (StoryAdapter::hasMedal).
  CHECK(story->medalCount() == 1);
  medals->clearAward();
  medals->check(*session);
  CHECK_FALSE(medals->awardPending());
}

TEST_CASE("a tamer level change is announced only when it stays within the limits") {
  const TestPacks packs{{"data/digimon.yaml", digimonTable}};
  const auto [session, story, city, medals] = progressSession(packs);
  CHECK_FALSE(session->tamerLevelAward().has_value());
  session->addTamerLevel(100, 1);
  CHECK(session->tamerLevelAward() == TamerLevelChange::Up);
  session->clearTamerLevelAward();
  session->addTamerLevel(100, -1);
  CHECK(session->tamerLevelAward() == TamerLevelChange::Down);
  session->clearTamerLevelAward();
  // tamer.c: addTamerLevel: below the limit the level stays and nothing is announced.
  session->addTamerLevel(100, -1);
  CHECK(session->tamer().level == 0);
  CHECK_FALSE(session->tamerLevelAward().has_value());
}
