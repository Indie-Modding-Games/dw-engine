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
using dwe::session::slotFromName;
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

// Ticks until the session writes its pending autosave, for at most a few seconds past the coalescing window.
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
    return std::ranges::any_of(
        autosaves, [&](const Autosave& autosave) { return std::ranges::contains(autosave.reasons, reason); });
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
    CHECK(session->clock().elapsedFrames() == dwe::ruleset::GameClock{session->clock().rules()}.elapsedFrames());
    CHECK_FALSE(session->savedPlace().has_value());
}

TEST_CASE("a new game takes the partner's stats from what the new-game script sets, when the packs have it") {
    // dg 193: SET_STAT for the partner's type, or the run for any other; new_game.stats only without the document.
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
TEST_CASE("the user's disc, extracted: the partner starts with the stats the new-game script sets") {
    const char* folder = std::getenv("DWE_CONTENT");
    if (folder == nullptr || std::string_view{folder}.empty()) {
        MESSAGE("DWE_CONTENT is not set: skipping the new game's stats on extracted content");
        return;
    }
    const dwe::assets::DiskFileSystem disk;
    const std::array<dwe::assets::PackLocation, 2> locations{
        dwe::assets::PackLocation{.fileSystem = &disk, .root = std::filesystem::path{folder} / "base"},
        dwe::assets::PackLocation{.fileSystem = &disk, .root = DWE_GAME_PACK}};
    auto files = dwe::assets::VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(files.has_value(), errorOf(files));
    const auto table = dwe::assets::loadNewGameStats(*files);
    REQUIRE_MESSAGE(table.has_value(), errorOf(table));
    if (!table->has_value()) {
        MESSAGE("the extracted base pack has no data/new_game_stats (extracted before dwx wrote it): skipping");
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

TEST_CASE("a partner raised for days at fast-forward, saved and loaded, goes on as the one it was saved from") {
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

    const auto loaded = newGame(packs, &storage, 3);
    const auto read = loaded->load(slot);
    REQUIRE_MESSAGE(read.has_value(), errorOf(read));
    CHECK(loaded->partner().state() == session->partner().state());
    CHECK_FALSE(loaded->load({.kind = SlotKind::Manual, .index = 5}).has_value());

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
}
