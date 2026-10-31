#include <dwe/assets/file_system.hpp>
#include <dwe/assets/new_game_stats.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/partner_view.hpp>
#include <dwe/session/slot_names.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "session_fixture.hpp"

using dwe::game::partner::Condition;
using dwe::game::partner::EventKind;
using dwe::game::partner::LifePhase;
using dwe::game::partner::PartnerEvent;
using dwe::game::partner::PartnerState;
using dwe::game::partner::Stat;
using dwe::game::save::AutosaveReason;
using dwe::game::save::MemorySaveStorage;
using dwe::game::save::SaveFile;
using dwe::game::save::SlotId;
using dwe::game::save::SlotKind;
using dwe::session::Autosave;
using dwe::session::GameSession;
using dwe::session::manualSlotFromName;
using dwe::session::slotFromName;
using dwe::session::slotName;
using dwe::session::summaryFromSections;
using dwe::session::testing::careFor;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGame;
using dwe::session::testing::SixtyGameHoursPerSecond;
using dwe::session::testing::TestPacks;
using dwe::session::testing::Tick;

namespace {

constexpr double SecondsPerGameDay = 24.0;
constexpr std::int32_t RaisedDays = 2;
constexpr std::int32_t ContinuedDays = 1;

// The save file of a session, written and read back as the disk would keep it.
SaveFile throughText(const SaveFile& file) {
  const auto text = dwe::game::save::writeSaveFile(file);
  REQUIRE_MESSAGE(text.has_value(), errorOf(text));
  auto parsed = dwe::game::save::parseSaveFile(*text);
  REQUIRE_MESSAGE(parsed.has_value(), errorOf(parsed));
  return *parsed;
}

// The text of a session's save sections, with the header of `header`, to compare two playthroughs.
std::string sectionsText(const GameSession& session, const SaveFile& header) {
  auto file = session.saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  file->header = header.header;
  const auto text = dwe::game::save::writeSaveFile(*file);
  REQUIRE_MESSAGE(text.has_value(), errorOf(text));
  return *text;
}

// Plays game time until the partner gets sleepy, and returns the autosaves written meanwhile.
std::vector<Autosave> playUntilSleepy(GameSession& session) {
  std::vector<Autosave> autosaves;
  const auto ticks = static_cast<std::int64_t>(SecondsPerGameDay / Tick);
  for (std::int64_t step = 0; step < ticks && !session.partner().canSleep(); ++step) {
    session.tick(Tick);
    if (const std::optional<Autosave> autosave = session.takeAutosaved()) {
      autosaves.push_back(*autosave);
    }
  }
  return autosaves;
}

// Plays game time without tending the partner, and returns the autosaves written meanwhile.
std::vector<Autosave> play(GameSession& session, double realSeconds) {
  std::vector<Autosave> autosaves;
  const auto ticks = static_cast<std::int64_t>(realSeconds / Tick);
  for (std::int64_t step = 0; step < ticks; ++step) {
    session.tick(Tick);
    if (const std::optional<Autosave> autosave = session.takeAutosaved()) {
      autosaves.push_back(*autosave);
    }
  }
  return autosaves;
}

// Ticks until the session writes its pending autosave, for at most a few seconds past the
// coalescing window.
std::optional<Autosave> waitForAutosave(GameSession& session) {
  constexpr double MaxWaitSeconds = 3.0;
  const auto ticks = static_cast<std::int64_t>(MaxWaitSeconds / Tick);
  for (std::int64_t step = 0; step < ticks; ++step) {
    session.tick(Tick);
    if (std::optional<Autosave> autosave = session.takeAutosaved()) {
      return autosave;
    }
  }
  return std::nullopt;
}

// Leaves the partner almost no life left, starving and unrelieved, so that it dies soon.
void makePartnerDie(GameSession& session) {
  PartnerState dying = session.partner().state();
  dying.life.remainingLifetime = 1;
  dying.hunger.foodLevel = 1'000'000;
  dying.toilet.poopLevel = 1'000'000;
  REQUIRE(session.partner().restore(dying).has_value());
}

bool savedFor(const std::vector<Autosave>& autosaves, AutosaveReason reason) {
  return std::ranges::any_of(autosaves, [&](const Autosave& autosave) {
    return std::ranges::contains(autosave.reasons, reason);
  });
}

bool has(const std::vector<PartnerEvent>& events, EventKind kind) {
  return std::ranges::any_of(events, [&](const PartnerEvent& event) { return event.kind == kind; });
}

} // namespace

TEST_CASE("a new game starts the partner, the tamer and the clock from the new_game section") {
  const TestPacks packs;
  MemorySaveStorage storage;
  const auto session = newGame(packs, &storage);
  CHECK(session->playing());
  const PartnerState& partner = session->partner().state();
  // main.c: main starts a new game with the partner of id 3.
  CHECK(partner.species == session->sessionRules().newGame.partner);
  CHECK(partner.species == "agum");
  CHECK(partner.stats.base[Stat::Hp] == session->sessionRules().newGame.stats[Stat::Hp]);
  CHECK(partner.stats.currentHp == partner.stats.base[Stat::Hp]);
  CHECK(session->tamer().level == 0);
  // main_menu.c: initializeDefaultSavegame starts the day at 8:00.
  CHECK(session->clock().time().hour == session->clock().rules().start.hour);
  CHECK(session->clock().elapsedFrames() ==
        dwe::ruleset::GameClock{session->clock().rules()}.elapsedFrames());
  CHECK_FALSE(session->savedPlace().has_value());
}

TEST_CASE(
    "a new game takes the partner's stats from what the new-game script sets, when the packs have it") {
  // dg 193: SET_STAT for the partner's type, or the run for any other; new_game.stats only without
  // the document.
  const TestPacks packs{{{"data/new_game_stats.yaml", R"(script: 193
at: 2328
partners:
- digimon: "agum"
  stats: {offense: 11, defense: 12, speed: 13, brains: 14, max_hp: 150, max_mp: 160, hp: 140, mp: 130}
others: {offense: 1, defense: 2, speed: 3, brains: 4, max_hp: 50, max_mp: 60, hp: 50, mp: 60}
)"}}};
  MemorySaveStorage storage;
  const auto session = newGame(packs, &storage);
  const PartnerState& partner = session->partner().state();
  CHECK(partner.species == "agum");
  CHECK(partner.stats.base[Stat::Offense] == 11);
  CHECK(partner.stats.base[Stat::Defense] == 12);
  CHECK(partner.stats.base[Stat::Speed] == 13);
  CHECK(partner.stats.base[Stat::Brain] == 14);
  CHECK(partner.stats.base[Stat::Hp] == 150);
  CHECK(partner.stats.base[Stat::Mp] == 160);
  CHECK(partner.stats.currentHp == 140);
  CHECK(partner.stats.currentMp == 130);

  const TestPacks broken{{{"data/new_game_stats.yaml", "script: 193\n"}}};
  CHECK_FALSE(GameSession::create({.files = &broken.files(),
                                   .rulesetId = "original",
                                   .rules = std::nullopt,
                                   .species = dwe::session::testing::madeUpTable(),
                                   .seed = 7,
                                   .storage = nullptr,
                                   .gameVersion = "test",
                                   .wallClock = [] { return dwe::session::testing::SaveDate; },
                                   .systems = {}})
                  .has_value());
}

// Runs only when DWE_CONTENT names a content folder (scripts/dev.sh mounts it).
TEST_CASE(
    "the user's disc, extracted: the partner starts with the stats the new-game script sets") {
  const std::optional<std::filesystem::path> mountedFolder = dwe::test_support::userContent();
  if (!mountedFolder) {
    MESSAGE("DWE_CONTENT is not set: skipping the new game's stats on extracted content");
    return;
  }
  const std::filesystem::path& folder = *mountedFolder;
  const dwe::assets::DiskFileSystem disk;
  const std::array<dwe::assets::PackLocation, 2> locations{
      dwe::assets::PackLocation{.fileSystem = &disk,
                                .root = folder / "base",
                                .fillsGaps = false,
                                .origin = dwe::assets::PackOrigin::Engine},
      dwe::assets::PackLocation{.fileSystem = &disk,
                                .root = DWE_GAME_PACK,
                                .fillsGaps = false,
                                .origin = dwe::assets::PackOrigin::Engine}};
  auto files = dwe::assets::VirtualFileSystem::mount(locations);
  REQUIRE_MESSAGE(files.has_value(), errorOf(files));
  const auto table = dwe::assets::loadNewGameStats(*files);
  REQUIRE_MESSAGE(table.has_value(), errorOf(table));
  if (!table->has_value()) {
    MESSAGE(
        "the extracted base pack has no data/new_game_stats (extracted before dwx wrote it): skipping");
    return;
  }
  auto session = GameSession::create({.files = &*files,
                                      .rulesetId = "original",
                                      .rules = std::nullopt,
                                      .species = nullptr,
                                      .seed = 7,
                                      .storage = nullptr,
                                      .gameVersion = "test",
                                      .wallClock = [] { return dwe::session::testing::SaveDate; },
                                      .systems = {}});
  REQUIRE_MESSAGE(session.has_value(), errorOf(session));
  const auto started = (*session)->startNewGame();
  REQUIRE_MESSAGE(started.has_value(), errorOf(started));
  const PartnerState& partner = (*session)->partner().state();
  // The script asks about the partner a new game starts with (main.c: main, initializePartner(3)).
  REQUIRE((*table)->partners.size() == 1);
  CHECK((*table)->partners.front().digimon == partner.species);
  const dwe::assets::StartingStats& set = (*table)->statsOf(partner.species);
  CHECK(partner.stats.base[Stat::Hp] == set.maxHp);
  CHECK(partner.stats.base[Stat::Brain] == set.brains);
  // It starts with HP and MP full.
  CHECK(partner.stats.currentHp == partner.stats.base[Stat::Hp]);
  CHECK(partner.stats.currentMp == partner.stats.base[Stat::Mp]);
}

TEST_CASE("the same seed plays the same partner; a new game starts it again") {
  const TestPacks packs;
  const auto first = newGame(packs, nullptr, 11);
  const auto second = newGame(packs, nullptr, 11);
  first->clock().setSpeed(SixtyGameHoursPerSecond);
  second->clock().setSpeed(SixtyGameHoursPerSecond);
  careFor(*first, SecondsPerGameDay);
  careFor(*second, SecondsPerGameDay);
  CHECK(first->partner().state() == second->partner().state());
  CHECK(first->partner().randomState() == second->partner().randomState());

  REQUIRE(first->startNewGame().has_value());
  const auto fresh = newGame(packs, nullptr, 11);
  CHECK(first->partner().state() == fresh->partner().state());
  CHECK(first->clock().elapsedFrames() == fresh->clock().elapsedFrames());
  CHECK(first->clock().speed() == doctest::Approx(SixtyGameHoursPerSecond));
}

TEST_CASE(
    "a partner raised for days at fast-forward, saved and loaded, goes on as the one it was saved from") {
  const TestPacks packs;
  MemorySaveStorage storage;
  const auto raised = newGame(packs, &storage);
  raised->clock().setSpeed(SixtyGameHoursPerSecond);
  careFor(*raised, RaisedDays * SecondsPerGameDay);
  REQUIRE(raised->partner().alive());
  CHECK(raised->partner().state().life.age >= RaisedDays);

  const auto saved = raised->saveFile();
  REQUIRE_MESSAGE(saved.has_value(), errorOf(saved));
  // Another seed: everything the loaded game plays comes from the save.
  MemorySaveStorage otherStorage;
  const auto loaded = newGame(packs, &otherStorage, 99);
  loaded->clock().setSpeed(SixtyGameHoursPerSecond);
  const auto read = loaded->loadFile(throughText(*saved));
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK(loaded->partner().state() == raised->partner().state());
  CHECK(loaded->partner().randomState() == raised->partner().randomState());
  CHECK(loaded->clock().elapsedFrames() == raised->clock().elapsedFrames());
  CHECK(loaded->clock().pendingFrames() == doctest::Approx(raised->clock().pendingFrames()));
  CHECK(loaded->tamer() == raised->tamer());

  careFor(*raised, ContinuedDays * SecondsPerGameDay);
  careFor(*loaded, ContinuedDays * SecondsPerGameDay);
  CHECK(loaded->partner().state() == raised->partner().state());
  CHECK(loaded->clock().time() == raised->clock().time());
  CHECK(sectionsText(*loaded, *saved) == sectionsText(*raised, *saved));
}

TEST_CASE("a game saved to a slot loads from it") {
  const TestPacks packs;
  MemorySaveStorage storage;
  const auto session = newGame(packs, &storage);
  session->clock().setSpeed(SixtyGameHoursPerSecond);
  careFor(*session, SecondsPerGameDay);
  const SlotId slot{.kind = SlotKind::Manual, .index = 2};
  const auto saved = session->save(slot);
  REQUIRE_MESSAGE(saved.has_value(), errorOf(saved));
  REQUIRE(storage.read("slot_02.yaml").value().has_value());

  CHECK(session->homeSlot() == slot);

  const auto loaded = newGame(packs, &storage, 3);
  CHECK_FALSE(loaded->homeSlot().has_value());
  const auto read = loaded->load(slot);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK(loaded->partner().state() == session->partner().state());
  CHECK(loaded->homeSlot() == slot);
  // The playthrough is the same one, which a save in the game checks its slot against.
  CHECK(loaded->playthroughStarted() == session->playthroughStarted());
  REQUIRE(loaded->slots() != nullptr);
  CHECK(loaded->slots()->list().front().state == dwe::game::save::SlotInfo::State::Empty);
  const auto file = loaded->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  CHECK(file->header.summary.slot == "slot_02");
  CHECK(file->header.summary.species == session->partner().state().species);
  CHECK_FALSE(file->header.summary.species.empty());
  CHECK_FALSE(loaded->load({.kind = SlotKind::Manual, .index = 5}).has_value());

  // A second save keeps the first as the backup, which a damaged slot loads from.
  REQUIRE(session->save(slot).has_value());
  storage.put("slot_02.yaml", "format: dwe-save\n");
  const auto restored = newGame(packs, &storage, 4);
  CHECK_FALSE(restored->load(slot).has_value());
  const auto fromBackup = restored->load(slot, true);
  REQUIRE_MESSAGE(fromBackup.has_value(), errorOf(fromBackup));
  CHECK(restored->homeSlot() == slot);
  CHECK(errorOf(restored->load({.kind = SlotKind::Manual, .index = 1}, true))
            .contains("slot_01.yaml.bak"));

  const auto unsaved = newGame(packs, nullptr);
  CHECK_FALSE(unsaved->save(slot).has_value());
}

TEST_CASE("the partner going to sleep autosaves") {
  const TestPacks packs;
  MemorySaveStorage storage;
  const auto session = newGame(packs, &storage);
  session->clock().setSpeed(SixtyGameHoursPerSecond);
  const std::vector<Autosave> before = playUntilSleepy(*session);
  REQUIRE(session->partner().canSleep());
  CHECK_FALSE(savedFor(before, AutosaveReason::PartnerSleep));

  const auto slept = session->sleep({});
  REQUIRE_MESSAGE(slept.has_value(), errorOf(slept));
  CHECK(has(session->takePartnerEvents(), EventKind::Slept));
  // The autosave waits out the coalescing window, so that the new day the sleep brings joins it.
  const std::optional<Autosave> autosave = waitForAutosave(*session);
  if (!autosave) {
    FAIL("no autosave");
    return;
  }
  CHECK(std::ranges::contains(autosave->reasons, AutosaveReason::PartnerSleep));
  CHECK(autosave->slot.kind == SlotKind::Autosave);

  // The autosave is a save like any other.
  const auto loaded = newGame(packs, &storage, 5);
  const auto read = loaded->load(autosave->slot);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK(loaded->partner().state() == session->partner().state());
}

TEST_CASE("an autosave keeps its playthrough's slot, and loading it goes on saving there") {
  const TestPacks packs;
  MemorySaveStorage storage;
  const auto session = newGame(packs, &storage);
  const SlotId third{.kind = SlotKind::Manual, .index = 3};
  REQUIRE(session->save(third).has_value());
  session->clock().setSpeed(SixtyGameHoursPerSecond);
  playUntilSleepy(*session);
  REQUIRE(session->sleep({}).has_value());
  const std::optional<Autosave> autosave = waitForAutosave(*session);
  if (!autosave) {
    FAIL("no autosave");
    return;
  }
  const auto loaded = newGame(packs, &storage, 5);
  REQUIRE(loaded->load(autosave->slot).has_value());
  CHECK(loaded->homeSlot() == third);

  // A new game has no slot until it is saved to one; an autosave of it has none either.
  const auto fresh = newGame(packs, &storage, 7);
  CHECK_FALSE(fresh->homeSlot().has_value());
  CHECK(fresh->saveFile().value().header.summary.slot.empty());
  // A failed save does not move the playthrough to the slot.
  CHECK_FALSE(fresh->save({.kind = SlotKind::Manual, .index = 99}).has_value());
  CHECK_FALSE(fresh->homeSlot().has_value());
}

TEST_CASE("a save from before the summary gets one from its sections") {
  const TestPacks packs;
  const auto session = newGame(packs, nullptr);
  SaveFile file = session->saveFile().value();
  file.header.summary = {};
  dwe::core::YamlNode field = dwe::core::YamlNode::mapping();
  field.set("map", dwe::core::YamlNode::scalar("base:map_01"));
  file.sections.at("field").state = field;

  const dwe::game::save::SaveSummary summary = summaryFromSections(throughText(file));
  CHECK(summary.species == session->partner().state().species);
  CHECK(summary.map == "base:map_01");
  CHECK(summary.slot.empty());

  // A section missing or of another shape gives an empty text, never an error.
  file.sections.erase("partner");
  file.sections.at("field").state = dwe::core::YamlNode::sequence();
  CHECK(summaryFromSections(file) == dwe::game::save::SaveSummary{});
}

TEST_CASE("a finished game saves where it continues and loads finished until a story save") {
  // main_menu.c: gameClearSave sets MAIN_STATE 2 and CURRENT_SCREEN getFileCityTopMap, keeping the
  // player's position; writeSavegame and loadSavegame keep MAIN_STATE; openSaveMachine sets 1.
  const TestPacks packs;
  const auto session = newGame(packs, nullptr);
  SaveFile placed = session->saveFile().value();
  dwe::core::YamlNode field = dwe::core::YamlNode::mapping();
  field.set("map", dwe::core::YamlNode::scalar("tend"));
  dwe::core::YamlNode position = dwe::core::YamlNode::sequence();
  for (const char* axis : {"1", "2", "3"}) {
    position.append(dwe::core::YamlNode::scalar(axis));
  }
  field.set("position", position);
  field.set("yaw", dwe::core::YamlNode::scalar("0.5"));
  placed.sections.at("field").state = field;
  REQUIRE(session->loadFile(throughText(placed)));
  CHECK_FALSE(session->gameCleared());

  session->markGameCleared("fila");
  const SaveFile finished = throughText(session->saveFile().value());
  CHECK(finished.header.summary.map == "fila");
  const auto loaded = newGame(packs, nullptr, 99);
  REQUIRE(loaded->loadFile(finished));
  CHECK(loaded->gameCleared());
  REQUIRE(loaded->savedPlace().has_value());
  CHECK(loaded->savedPlace()->map == "fila");
  CHECK(loaded->savedPlace()->spawn.position.z == doctest::Approx(3.0F));
  CHECK(loaded->savedPlace()->spawn.yaw == doctest::Approx(0.5F));

  // A finished game saved again stays finished; once the story plays on it saves as one.
  REQUIRE(loaded->loadFile(throughText(loaded->saveFile().value())));
  CHECK(loaded->gameCleared());
  loaded->resumeStory();
  REQUIRE(loaded->loadFile(throughText(loaded->saveFile().value())));
  CHECK_FALSE(loaded->gameCleared());
  // A new game is never finished, and a game saved off the field keeps the mark.
  session->markGameCleared(std::nullopt);
  REQUIRE(session->startNewGame());
  CHECK_FALSE(session->gameCleared());
  session->markGameCleared(std::nullopt);
  SaveFile placeless = session->saveFile().value();
  CHECK(placeless.sections.at("field").state.find("map") == nullptr);
  REQUIRE(loaded->loadFile(throughText(placeless)));
  CHECK(loaded->gameCleared());
  CHECK_FALSE(loaded->savedPlace().has_value());
}

TEST_CASE("a new day autosaves only when the ruleset says so") {
  const TestPacks packs;
  // The original ruleset turns it on (save.yaml: autosave.on_new_day).
  MemorySaveStorage onStorage;
  const auto on = newGame(packs, &onStorage);
  on->clock().setSpeed(SixtyGameHoursPerSecond);
  CHECK(savedFor(play(*on, SecondsPerGameDay), AutosaveReason::NewDay));

  MemorySaveStorage offStorage;
  const auto off = newGame(packs, &offStorage);
  REQUIRE(off->rules().setValue("save", "autosave.on_new_day", "false").has_value());
  off->clock().setSpeed(SixtyGameHoursPerSecond);
  CHECK_FALSE(savedFor(play(*off, SecondsPerGameDay), AutosaveReason::NewDay));
}

TEST_CASE("a tuned value reaches the running session") {
  const TestPacks packs;
  const auto session = newGame(packs, nullptr);
  CHECK(session->sessionRules().tamer.levelMax == 10);
  REQUIRE(session->rules().setValue("tamer", "level_max", "5").has_value());
  session->tick(Tick);
  CHECK(session->sessionRules().tamer.levelMax == 5);
  // A value the session cannot use is refused and nothing changes.
  CHECK_FALSE(session->rules().setValue("tamer", "level_max", "-1").has_value());
  CHECK(session->sessionRules().tamer.levelMax == 5);
}

TEST_CASE("a partner that dies is reborn as a baby") {
  const TestPacks packs;
  const auto session = newGame(packs, nullptr);
  makePartnerDie(*session);
  session->clock().setSpeed(SixtyGameHoursPerSecond);
  std::vector<PartnerEvent> events;
  for (std::int32_t step = 0; step < 120 && !has(events, EventKind::Reborn); ++step) {
    session->tick(Tick);
    const std::vector<PartnerEvent> tickEvents = session->takePartnerEvents();
    events.insert(events.end(), tickEvents.begin(), tickEvents.end());
  }
  CHECK(has(events, EventKind::Died));
  CHECK(has(events, EventKind::Reborn));
  CHECK(session->partner().alive());
  constexpr std::array<std::string_view, 4> Babies{"bota", "puni", "poyo", "yura"};
  CHECK(std::ranges::find(Babies, session->partner().state().species) != Babies.end());
}

TEST_CASE("a partner whose death the story plays waits for the story's rebirth and its egg") {
  const TestPacks packs;
  const auto session = newGame(packs, nullptr);
  session->setRebirthByStory(true);
  makePartnerDie(*session);
  session->clock().setSpeed(SixtyGameHoursPerSecond);
  std::vector<PartnerEvent> events;
  for (std::int32_t step = 0; step < 120 && !has(events, EventKind::Died); ++step) {
    session->tick(Tick);
    const std::vector<PartnerEvent> tickEvents = session->takePartnerEvents();
    events.insert(events.end(), tickEvents.begin(), tickEvents.end());
  }
  REQUIRE(has(events, EventKind::Died));
  session->tick(Tick);
  CHECK_FALSE(session->partner().alive());
  constexpr std::size_t LastEgg = 3;
  session->rebirthPartner(LastEgg);
  CHECK(session->partner().alive());
  CHECK(session->partner().state().species == "yura");
}

TEST_CASE("the player counts the first partner raised and one more for each partner reborn") {
  const TestPacks packs;
  const auto session = newGame(packs, nullptr);
  // main_menu.c: initializeDefaultSavegame sets raisedCount to 1.
  CHECK(session->tamer().raised == 1);
  session->setRebirthByStory(true);
  makePartnerDie(*session);
  session->clock().setSpeed(SixtyGameHoursPerSecond);
  for (std::int32_t step = 0; step < 120 && session->partner().alive(); ++step) {
    session->tick(Tick);
    (void)session->takePartnerEvents();
  }
  REQUIRE_FALSE(session->partner().alive());
  session->rebirthPartner(0);
  REQUIRE(session->partner().alive());
  // partner_impl.c: initializeReincarnatedPartner adds one.
  CHECK(session->tamer().raised == 2);

  const auto saved = session->saveFile();
  REQUIRE_MESSAGE(saved.has_value(), errorOf(saved));
  const auto loaded = newGame(packs, nullptr, 99);
  const auto read = loaded->loadFile(throughText(*saved));
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK(loaded->tamer().raised == 2);
}

TEST_CASE("a partner reborn after dying autosaves") {
  const TestPacks packs;
  MemorySaveStorage storage;
  const auto session = newGame(packs, &storage);
  makePartnerDie(*session);
  session->clock().setSpeed(SixtyGameHoursPerSecond);
  constexpr double DeathAndCoalesceSeconds = 30.0;
  CHECK(savedFor(play(*session, DeathAndCoalesceSeconds), AutosaveReason::PartnerReborn));
}

TEST_CASE("the partner view shows the partner's state within the care limits") {
  const TestPacks packs;
  const auto session = newGame(packs, nullptr);
  session->clock().setSpeed(SixtyGameHoursPerSecond);
  careFor(*session, SecondsPerGameDay / 2.0);
  const dwe::session::PartnerView view = session->partnerView();
  const PartnerState& state = session->partner().state();
  CHECK(view.species == state.species);
  CHECK(view.alive);
  CHECK(view.happiness.value == state.mood.happiness);
  CHECK(view.happiness.min == session->partnerRules().care.limits.happinessMin);
  CHECK(view.happiness.fill() >= 0.0F);
  CHECK(view.happiness.fill() <= 1.0F);
  CHECK(view.energy.max > 0);
  CHECK(view.virusBar.max == session->partnerRules().care.toilet.virusBarLimit);
}

TEST_CASE("slot names read as slots") {
  CHECK(slotFromName("slot_01") == SlotId{.kind = SlotKind::Manual, .index = 1});
  CHECK(slotFromName("autosave_02.yaml") == SlotId{.kind = SlotKind::Autosave, .index = 2});
  CHECK(slotFromName("slot_15") == SlotId{.kind = SlotKind::Manual, .index = 15});
  CHECK_FALSE(slotFromName("slot_00").has_value());
  CHECK_FALSE(slotFromName("slot_100").has_value());
  CHECK_FALSE(slotFromName("slot_x1").has_value());
  CHECK_FALSE(slotFromName("save_01").has_value());
  CHECK_FALSE(slotFromName("").has_value());

  CHECK(slotName({.kind = SlotKind::Manual, .index = 3}) == "slot_03");
  CHECK(slotName({.kind = SlotKind::Autosave, .index = 12}) == "autosave_12");
  CHECK(manualSlotFromName("slot_03") == SlotId{.kind = SlotKind::Manual, .index = 3});
  CHECK_FALSE(manualSlotFromName("autosave_01").has_value());
  CHECK_FALSE(manualSlotFromName("").has_value());
}
