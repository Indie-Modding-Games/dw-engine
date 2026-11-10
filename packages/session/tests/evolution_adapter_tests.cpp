#include <dwe/game/evolution/result.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/game/save/autosave.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/field_facts.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"

using dwe::game::evolution::EvolutionReason;
using dwe::game::evolution::testing::TableFixture;
using dwe::game::partner::EventKind;
using dwe::game::partner::PartnerState;
using dwe::game::partner::Stat;
using dwe::game::save::AutosaveReason;
using dwe::game::save::MemorySaveStorage;
using dwe::session::EvolutionAdapter;
using dwe::session::EvolutionAdapterDesc;
using dwe::session::FirstRaisedTrigger;
using dwe::session::GameSession;
using dwe::session::SessionSystem;
using dwe::session::StoryAdapter;
using dwe::session::StoryAdapterDesc;
using dwe::session::testing::errorOf;
using dwe::session::testing::evolutionAdapter;
using dwe::session::testing::GameHourPerSecond;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

// The baby's check hour (rules/original/evolution.yaml: check_hours.baby).
constexpr std::int32_t BabyHours = 6;
constexpr std::int32_t HighStat = 990;
constexpr std::int32_t HighHpMp = 9000;

// A session with the evolution adapter on the fixture's tables; `adapter` points to it.
std::unique_ptr<GameSession> sessionWithEvolution(const TestPacks& packs,
                                                  const TableFixture& fixture,
                                                  EvolutionAdapter*& adapter,
                                                  MemorySaveStorage* storage = nullptr) {
  auto made = evolutionAdapter(packs, fixture);
  adapter = made.get();
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(made));
  return newGameWith(packs, std::move(systems), storage);
}

// A session with the story and the evolution adapter on it, as the game builds them: the story owns
// the raised forms and a save without the story section takes them from the evolution section.
struct StoryEvolution {
  std::unique_ptr<GameSession> session;
  EvolutionAdapter* evolution = nullptr;
  StoryAdapter* story = nullptr;
};

StoryEvolution sessionWithStory(const TestPacks& packs, const TableFixture& fixture) {
  auto story = std::make_unique<StoryAdapter>(StoryAdapterDesc{.files = &packs.files()});
  auto evolution = std::make_unique<EvolutionAdapter>(EvolutionAdapterDesc{
      .files = &packs.files(), .tables = fixture.build(), .story = story.get()});
  story->setSavedRaised([adapter = evolution.get()] { return adapter->state().raised; });
  StoryEvolution made{.session = nullptr, .evolution = evolution.get(), .story = story.get()};
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(evolution));
  systems.push_back(std::move(story));
  made.session = newGameWith(packs, std::move(systems));
  return made;
}

// The partner's state changed by `change`, restored into it.
template <typename Change>
void changePartner(GameSession& session, Change change) {
  PartnerState state = session.partner().state();
  change(state);
  REQUIRE(session.partner().restore(state).has_value());
}

// Plays on past the save rules' coalescing seconds and says whether an autosave for `reason` was
// written.
bool autosavedFor(GameSession& session, AutosaveReason reason) {
  constexpr int CoalescingTicks = 3;
  session.clock().setSpeed(1.0);
  for (int tick = 0; tick < CoalescingTicks; ++tick) {
    session.tick(1.0);
    if (const auto autosave = session.takeAutosaved()) {
      return std::ranges::find(autosave->reasons, reason) != autosave->reasons.end();
    }
  }
  return false;
}

} // namespace

TEST_CASE("the evolution adapter marks the first partner as raised on a new game") {
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  CHECK(adapter->state().raised.contains(TableFixture::index("agum")));
  CHECK(adapter->state().raised.size() == 1);
}

TEST_CASE(
    "a baby evolves when the clock's hour brings its counter to the check hour, and the session autosaves") {
  const TestPacks packs;
  const TableFixture fixture;
  MemorySaveStorage storage;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter, &storage);
  changePartner(*session, [](PartnerState& state) {
    state.species = "bota";
    state.evolution.hours = BabyHours - 1;
  });
  session->clock().setSpeed(GameHourPerSecond);
  session->tick(1.0);
  CHECK(session->partner().state().species == "koro");
  // The level changed, so the counter starts again.
  CHECK(session->partner().state().evolution.hours == 0);
  CHECK(adapter->state().raised.contains(TableFixture::index("koro")));
  const auto evolutions = adapter->takeEvolutions();
  REQUIRE(evolutions.size() == 1);
  CHECK(evolutions.front().reason == EvolutionReason::Normal);
  CHECK(autosavedFor(*session, AutosaveReason::Evolution));
}

TEST_CASE("the natural check runs on every frame of the game clock, not only when an hour begins") {
  // partner_impl.c: tickConditions.
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  changePartner(*session, [](PartnerState& state) {
    state.species = "bota";
    state.evolution.hours = BabyHours;
  });
  session->clock().setSpeed(1.0);
  session->tick(0.1);
  CHECK(session->partner().state().species == "koro");
}

TEST_CASE("no natural evolution in the hour after waking") {
  // partner_impl.c: tickConditions returns early in the immortal hour.
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  const std::int32_t hour = session->clock().time().hour;
  changePartner(*session, [hour](PartnerState& state) {
    state.species = "bota";
    state.evolution.hours = BabyHours;
    state.life.protectedHour = hour;
  });
  session->clock().setSpeed(1.0);
  session->tick(0.1);
  CHECK(session->partner().state().species == "bota");
  CHECK(adapter->takeEvolutions().empty());
}

TEST_CASE("no natural evolution while the field's palette fades between times of day") {
  // partner_impl.c: tickConditions returns early while map.c: isInDaytimeTransition is 0.
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  changePartner(*session, [](PartnerState& state) {
    state.species = "bota";
    state.evolution.hours = BabyHours;
  });
  bool fading = true;
  adapter->setDaytimeFade([&fading] { return fading; });
  session->clock().setSpeed(1.0);
  session->tick(0.1);
  CHECK(session->partner().state().species == "bota");
  CHECK(adapter->takeEvolutions().empty());
  // Once the fade ends, the next frame's check takes the evolution.
  fading = false;
  session->tick(0.1);
  CHECK(session->partner().state().species == "koro");
}

TEST_CASE("no evolution before the check hour: the partner's counter keeps counting") {
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  changePartner(*session, [](PartnerState& state) {
    state.species = "bota";
    state.evolution.hours = 0;
  });
  session->clock().setSpeed(GameHourPerSecond);
  session->tick(1.0);
  CHECK(session->partner().state().species == "bota");
  CHECK(session->partner().state().evolution.hours == 1);
  CHECK(adapter->takeEvolutions().empty());
}

TEST_CASE("the full virus bar turns the partner into the virus form and keeps its form and stats") {
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  const PartnerState before = session->partner().state();
  adapter->routePartnerEvent(*session, {.kind = EventKind::VirusBarFull, .value = 0});
  const PartnerState& after = session->partner().state();
  CHECK(after.species == "scum");
  CHECK(after.virusBackup.species == "agum");
  CHECK(after.virusBackup.stats == before.stats.base);
  CHECK(after.toilet.virusBar == 0);
}

TEST_CASE(
    "the virus bar filling in the hour the counter reaches the check hour wins over the natural evolution") {
  // partner_impl.c: tickConditions checks the virus bar after the natural evolution, and its target
  // wins.
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  const std::int32_t barLimit = session->partnerRules().care.toilet.virusBarLimit;
  changePartner(*session, [&](PartnerState& state) {
    state.species = "bota";
    state.evolution.hours = BabyHours - 1;
    state.toilet.virusBar = barLimit;
  });
  session->clock().setSpeed(GameHourPerSecond);
  session->tick(1.0);
  const PartnerState& after = session->partner().state();
  CHECK(after.species == "scum");
  CHECK(after.virusBackup.species == "bota");
  const auto evolutions = adapter->takeEvolutions();
  REQUIRE(evolutions.size() == 1);
  CHECK(evolutions.front().reason == EvolutionReason::Virus);
}

TEST_CASE("back from the virus form, the rebuilt stats are set and clamped to each stat's limit") {
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  changePartner(*session, [](PartnerState& state) {
    state.species = "scum";
    for (const Stat stat : dwe::game::partner::AllStats) {
      const bool hpOrMp = stat == Stat::Hp || stat == Stat::Mp;
      state.stats.base[stat] = hpOrMp ? HighHpMp : HighStat;
    }
    state.virusBackup = {.species = "agum", .stats = state.stats.base};
  });
  REQUIRE(adapter->returnFromVirus(*session));
  const PartnerState& after = session->partner().state();
  CHECK(after.species == "agum");
  const auto& limits = session->partnerRules().care.limits;
  CHECK(after.stats.base[Stat::Hp] == limits.hpMpMax);
  CHECK(after.stats.base[Stat::Mp] == limits.hpMpMax);
  CHECK(after.stats.base[Stat::Offense] == limits.statMax);
  CHECK(after.stats.base[Stat::Brain] == limits.statMax);
  CHECK(after.stats.currentHp <= after.stats.base[Stat::Hp]);
}

TEST_CASE("without a kept form, there is no way back from the virus form") {
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  changePartner(*session, [](PartnerState& state) {
    state.species = "scum";
    state.virusBackup = {};
  });
  CHECK_FALSE(adapter->returnFromVirus(*session));
  CHECK(session->partner().state().species == "scum");
}

TEST_CASE(
    "an evolution item turns the partner into its form and adds the evolution's stat change") {
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  const PartnerState before = session->partner().state();
  REQUIRE(adapter->useItem(*session, "giga_hand"));
  const PartnerState& after = session->partner().state();
  CHECK(after.species == "ggdr");
  CHECK(adapter->state().evolutionItemUsed);
  const auto evolutions = adapter->takeEvolutions();
  REQUIRE(evolutions.size() == 1);
  CHECK(evolutions.front().reason == EvolutionReason::Item);
  const std::int32_t gain = std::min(evolutions.front().statChange.offense,
                                     session->partnerRules().care.limits.statGainMax);
  CHECK(after.stats.base[Stat::Offense] == before.stats.base[Stat::Offense] + gain);
  CHECK(after.weight == evolutions.front().weight);
}

TEST_CASE("an event script's evolution needs a form of the tables") {
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  CHECK_FALSE(adapter->evolveByScript(*session, "nobody").has_value());
  const auto evolved = adapter->evolveByScript(*session, "species_b");
  REQUIRE_MESSAGE(evolved.has_value(), errorOf(evolved));
  CHECK(*evolved);
  CHECK(session->partner().state().species == "species_b");
}

TEST_CASE(
    "the developer menu's evolution list has every form of the tables, and each one evolves") {
  // The dev menu's evolve page lists cheatTargets and runs evolve("<id>") (evolveByCheat) for the
  // one picked: every form with a row, the level-less one too, and none the command refuses.
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  const std::vector<std::string> targets = adapter->cheatTargets();
  CHECK(targets.size() == dwe::game::evolution::testing::FixtureForms.size());
  CHECK(std::ranges::contains(targets, "scum"));
  CHECK(std::ranges::contains(targets, "mtet"));
  CHECK(std::ranges::contains(targets, "species_g"));
  for (const std::string& target : targets) {
    const auto evolved = adapter->evolveByCheat(*session, target);
    REQUIRE_MESSAGE(evolved.has_value(), errorOf(evolved));
    CHECK_MESSAGE(*evolved, target);
    CHECK(session->partner().state().species == target);
  }
}

namespace {

// How many evolutions (Evolved) the partner reported since the last call, routed as after any
// command: each one starts the field's evolution scene and with it the evolving bubble (apps/game:
// evolution_scene.hpp).
std::size_t evolvedEvents(GameSession& session) {
  session.afterSystemCommand();
  return static_cast<std::size_t>(std::ranges::count(
      session.takePartnerEvents(), EventKind::Evolved, &dwe::game::partner::PartnerEvent::kind));
}

} // namespace

TEST_CASE("every way to evolve reports one evolution, which the field shows with its scene") {
  // partner_impl.c: tickConditions (natural and virus), item.c: handleEvoItems, partner.c (back
  // from the virus form) and the event scripts (and the console's evolve) all set the partner's
  // state 13: partnerTickEvolving, and its bubble (main/evl.c:47).
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  static_cast<void>(evolvedEvents(*session));

  SUBCASE("natural") {
    changePartner(*session, [](PartnerState& state) {
      state.species = "bota";
      state.evolution.hours = BabyHours;
    });
    static_cast<void>(evolvedEvents(*session));
    session->clock().setSpeed(1.0);
    session->tick(0.1);
    CHECK(session->partner().state().species == "koro");
  }
  SUBCASE("item") {
    REQUIRE(adapter->useItem(*session, "giga_hand"));
  }
  SUBCASE("event script or console") {
    REQUIRE(adapter->evolveByScript(*session, "species_b").value_or(false));
  }
  SUBCASE("virus and back") {
    adapter->routePartnerEvent(*session, {.kind = EventKind::VirusBarFull, .value = 0});
    CHECK(session->partner().state().species == "scum");
    CHECK(evolvedEvents(*session) == 1);
    REQUIRE(adapter->returnFromVirus(*session));
    CHECK(session->partner().state().species == "agum");
  }
  CHECK(evolvedEvents(*session) == 1);
}

TEST_CASE("the evolution state goes in the save and comes back") {
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* saving = nullptr;
  auto saved = sessionWithEvolution(packs, fixture, saving);
  REQUIRE(saving->useItem(*saved, "giga_hand"));
  auto file = saved->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  REQUIRE(file->sections.contains("evolution"));

  EvolutionAdapter* loading = nullptr;
  auto loaded = sessionWithEvolution(packs, fixture, loading);
  const auto read = loaded->loadFile(*file);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK(loading->state().raised == saving->state().raised);
  // Like the original, the item mark is not saved.
  CHECK_FALSE(loading->state().evolutionItemUsed);
  CHECK(loaded->partner().state().species == "ggdr");
}

TEST_CASE("the evolution adapter registers the evolution section's check") {
  const TestPacks packs;
  const TableFixture fixture;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  CHECK_FALSE(session->rules().setValue("evolution", "check_hours.baby", "soon").has_value());
}

TEST_CASE(
    "the story owns the raised forms: an evolution and a rebirth raise their triggers, and a guard sees them") {
  const TestPacks packs;
  const TableFixture fixture;
  const auto made = sessionWithStory(packs, fixture);
  const auto target = TableFixture::index("species_b");
  CHECK_FALSE(made.story->raised(target));

  const auto evolved = made.evolution->evolveByScript(*made.session, "species_b");
  REQUIRE_MESSAGE(evolved.has_value(), errorOf(evolved));
  CHECK(*evolved);
  CHECK(made.story->raised(target));
  const dwe::game::field::GuardFacts facts =
      dwe::session::fieldGuardFacts(*made.session, nullptr, *made.story);
  CHECK(facts.triggers(FirstRaisedTrigger + target) == std::optional<bool>{true});

  // evolution.c: reincarnatePartner raises the new baby.
  const auto baby = TableFixture::index("bota");
  CHECK_FALSE(made.story->raised(baby));
  changePartner(*made.session, [](PartnerState& state) { state.species = "bota"; });
  made.evolution->routePartnerEvent(*made.session, {.kind = EventKind::Reborn});
  CHECK(made.story->raised(baby));
  CHECK(facts.triggers(FirstRaisedTrigger + baby) == std::optional<bool>{true});

  // A form a script raised in the story is one the rules see as raised.
  const auto scripted = TableFixture::index("kune");
  REQUIRE(made.story->state().setTrigger(FirstRaisedTrigger + scripted, true).has_value());
  REQUIRE(made.evolution->evolveByScript(*made.session, "species_d").value_or(false));
  CHECK(made.evolution->state().raised.contains(scripted));
}

TEST_CASE("a species past the raised triggers is kept apart, and a new game clears it") {
  const TestPacks packs;
  const TableFixture fixture;
  const auto made = sessionWithStory(packs, fixture);
  // The hidden content's partners are numbered from 0x3f: no trigger of their own.
  constexpr std::int32_t Hidden = dwe::session::RaisedSpeciesLimit + 2;
  CHECK_FALSE(made.story->raised(Hidden));
  made.story->markRaised(Hidden);
  CHECK(made.story->raised(Hidden));
  CHECK(made.story->raisedSpecies().contains(Hidden));
  CHECK_FALSE(made.story->state().trigger(FirstRaisedTrigger + Hidden).value_or(false));
  REQUIRE(made.story->startNewGame(*made.session).has_value());
  CHECK_FALSE(made.story->raised(Hidden));
}

TEST_CASE("a save without the story section raises the forms its evolution section kept") {
  const TestPacks packs;
  const TableFixture fixture;
  const auto saved = sessionWithStory(packs, fixture);
  REQUIRE(saved.evolution->evolveByScript(*saved.session, "species_b").value_or(false));
  auto file = saved.session->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  file->sections.erase(std::string{dwe::session::StorySaveName});

  const auto loaded = sessionWithStory(packs, fixture);
  const auto read = loaded.session->loadFile(*file);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK(loaded.story->raised(TableFixture::index("species_b")));
  CHECK(loaded.story->raisedSpecies() == saved.story->raisedSpecies());
}

TEST_CASE("the chart's outlook names the form the next check evolves the partner into") {
  const TestPacks packs;
  TableFixture fixture;
  fixture.setPath("koro", {"agum", "kune"});
  fixture.requirement("agum").weight = 50;
  fixture.requirement("kune").offense = dwe::game::evolution::FavouredStat;
  EvolutionAdapter* adapter = nullptr;
  auto session = sessionWithEvolution(packs, fixture, adapter);
  changePartner(*session, [](PartnerState& state) {
    state.species = "koro";
    state.evolution.hours = 23;
    state.weight = 0;
    state.stats.base[Stat::Offense] = HighStat;
  });
  const auto outlook = adapter->outlook(*session);
  REQUIRE(outlook.has_value());
  CHECK_FALSE(outlook->due());
  REQUIRE(outlook->targets.size() == 2);
  CHECK_FALSE(outlook->targets.at(0).passes());
  CHECK(outlook->targets.at(1).passes());
  CHECK(outlook->next == TableFixture::index("kune"));
  session->clock().setSpeed(GameHourPerSecond);
  session->tick(1.0);
  CHECK(session->partner().state().species == "kune");
}
