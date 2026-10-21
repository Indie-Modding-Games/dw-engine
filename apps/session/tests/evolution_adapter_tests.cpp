#include <dwe/game/evolution/result.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/game/save/autosave.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_session.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <ostream>
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
using dwe::session::GameSession;
using dwe::session::SessionSystem;
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
std::unique_ptr<GameSession> sessionWithEvolution(const TestPacks& packs, const TableFixture& fixture,
                                                  EvolutionAdapter*& adapter, MemorySaveStorage* storage = nullptr) {
    auto made = evolutionAdapter(packs, fixture);
    adapter = made.get();
    std::vector<std::unique_ptr<SessionSystem>> systems;
    systems.push_back(std::move(made));
    return newGameWith(packs, std::move(systems), storage);
}

// The partner's state changed by `change`, restored into it.
template <typename Change>
void changePartner(GameSession& session, Change change) {
    PartnerState state = session.partner().state();
    change(state);
    REQUIRE(session.partner().restore(state).has_value());
}

// Plays on past the save rules' coalescing seconds and says whether an autosave for `reason` was written.
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

TEST_CASE("a baby evolves when the clock's hour brings its counter to the check hour, and the session autosaves") {
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

TEST_CASE("the virus bar filling in the hour the counter reaches the check hour wins over the natural evolution") {
    // partner_impl.c: tickConditions checks the virus bar after the natural evolution, and its target wins.
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

TEST_CASE("an evolution item turns the partner into its form and adds the evolution's stat change") {
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
    const std::int32_t gain =
        std::min(evolutions.front().statChange.offense, session->partnerRules().care.limits.statGainMax);
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
