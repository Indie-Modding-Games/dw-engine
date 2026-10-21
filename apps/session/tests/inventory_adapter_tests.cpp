#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/inventory/shop.hpp>
#include <dwe/game/inventory/use.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/game/save/autosave.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "game/inventory/tests/fixture.hpp"

namespace items = dwe::game::inventory::testing;
using dwe::game::evolution::testing::TableFixture;
using dwe::game::inventory::UseOutcome;
using dwe::game::partner::PartnerState;
using dwe::game::partner::Stat;
using dwe::game::save::AutosaveReason;
using dwe::game::save::MemorySaveStorage;
using dwe::session::EvolutionAdapter;
using dwe::session::GameSession;
using dwe::session::InventoryAdapter;
using dwe::session::InventoryAdapterDesc;
using dwe::session::SessionSystem;
using dwe::session::testing::errorOf;
using dwe::session::testing::evolutionAdapter;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

constexpr std::int32_t FullDiscipline = 100;
constexpr std::int32_t Bits = 10'000;

// The shop triggers of a story where every item is in stock.
class AllInStock final : public dwe::game::inventory::StockTriggers {
public:
    [[nodiscard]] bool isSet(std::int32_t /*trigger*/) const override { return true; }
};

struct Adapters {
    EvolutionAdapter* evolution = nullptr;
    InventoryAdapter* inventory = nullptr;
};

// A session with the evolution and inventory adapters on the made-up tables, its partner disciplined enough to take
// every item the rules let it.
std::unique_ptr<GameSession> sessionWithItems(const TestPacks& packs, Adapters& adapters,
                                              MemorySaveStorage* storage = nullptr) {
    const TableFixture forms;
    auto evolution = evolutionAdapter(packs, forms);
    auto inventory = std::make_unique<InventoryAdapter>(InventoryAdapterDesc{.files = &packs.files(),
                                                                             .items = items::fixtureItems(),
                                                                             .effects = items::fixtureEffects(),
                                                                             .evolution = evolution.get()});
    adapters = {.evolution = evolution.get(), .inventory = inventory.get()};
    std::vector<std::unique_ptr<SessionSystem>> systems;
    systems.push_back(std::move(evolution));
    systems.push_back(std::move(inventory));
    auto session = newGameWith(packs, std::move(systems), storage);
    PartnerState state = session->partner().state();
    state.mood.discipline = FullDiscipline;
    REQUIRE(session->partner().restore(state).has_value());
    return session;
}

bool autosaveRequested(GameSession& session, AutosaveReason reason) {
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

TEST_CASE("a new game's bag is the original's: 10 slots, no money and no items, as script dg 193 leaves it") {
    const TestPacks packs;
    Adapters adapters;
    auto session = sessionWithItems(packs, adapters);
    const auto& bag = adapters.inventory->inventory();
    CHECK(bag.slotCount() == 10);
    CHECK(bag.money() == 0);
    CHECK(std::ranges::none_of(bag.slots(), [](const auto& slot) { return slot.has_value(); }));
}

TEST_CASE("the session puts a ruleset's new-game items in the bag, and refuses an unknown one") {
    const TestPacks packs;
    Adapters adapters;
    auto session = sessionWithItems(packs, adapters);
    REQUIRE(session->rules()
                .setValue("inventory", "bag.new_game_items", "[{item: 0, count: 3}, {item: 38, count: 2}]")
                .has_value());
    session->tick(dwe::session::testing::Tick);
    REQUIRE(session->startNewGame().has_value());
    CHECK(adapters.inventory->inventory().countOf(items::Tonic) == 3);
    CHECK(adapters.inventory->inventory().countOf(items::Bun) == 2);

    REQUIRE(session->rules().setValue("inventory", "bag.new_game_items", "[{item: 77, count: 1}]").has_value());
    session->tick(dwe::session::testing::Tick);
    CHECK_FALSE(session->startNewGame().has_value());
}

TEST_CASE("a healing item heals the partner within its maximum and is used up") {
    const TestPacks packs;
    Adapters adapters;
    auto session = sessionWithItems(packs, adapters);
    PartnerState state = session->partner().state();
    state.stats.currentHp = 1;
    REQUIRE(session->partner().restore(state).has_value());
    adapters.inventory->inventory().add(items::Tonic, 1);
    const auto used = adapters.inventory->use(*session, items::Tonic);
    REQUIRE_MESSAGE(used.has_value(), errorOf(used));
    CHECK(used->outcome == UseOutcome::Used);
    const auto& stats = session->partner().state().stats;
    CHECK(stats.currentHp == std::min(1 + 500, stats.base[Stat::Hp]));
    CHECK(adapters.inventory->inventory().countOf(items::Tonic) == 0);
}

TEST_CASE("food feeds the partner: energy, weight and the training boost of its flag's stats") {
    const TestPacks packs;
    Adapters adapters;
    auto session = sessionWithItems(packs, adapters);
    const PartnerState before = session->partner().state();
    adapters.inventory->inventory().add(items::Steak, 1);
    const auto used = adapters.inventory->use(*session, items::Steak);
    REQUIRE_MESSAGE(used.has_value(), errorOf(used));
    REQUIRE(used->outcome == UseOutcome::Used);
    const PartnerState& after = session->partner().state();
    CHECK(after.weight == before.weight + 2);
    // Flag 1 is the offense (trn_reward.c), for 4 hours.
    CHECK(after.training.boost.stats.contains(Stat::Offense));
    CHECK_FALSE(after.training.boost.stats.contains(Stat::Brain));
    CHECK(after.training.boost.value == 2);
}

TEST_CASE("a chip raises the stats through the partner's gains and may lower the tamer level") {
    const TestPacks packs;
    Adapters adapters;
    auto session = sessionWithItems(packs, adapters);
    const PartnerState before = session->partner().state();
    adapters.inventory->inventory().add(items::BrainChip, 1);
    const auto used = adapters.inventory->use(*session, items::BrainChip);
    REQUIRE_MESSAGE(used.has_value(), errorOf(used));
    REQUIRE(used->outcome == UseOutcome::Used);
    CHECK(session->partner().state().stats.base[Stat::Brain] == before.stats.base[Stat::Brain] + 50);
}

TEST_CASE("the partner never takes some items, and nothing is used up") {
    const TestPacks packs;
    Adapters adapters;
    auto session = sessionWithItems(packs, adapters);
    adapters.inventory->inventory().add(items::Charm, 1);
    const auto used = adapters.inventory->use(*session, items::Charm);
    REQUIRE_MESSAGE(used.has_value(), errorOf(used));
    CHECK(used->outcome == UseOutcome::Refused);
    CHECK(adapters.inventory->inventory().countOf(items::Charm) == 1);
}

TEST_CASE("a fixed evolution item is refused away from its level and evolves the partner at it") {
    const TestPacks packs;
    Adapters adapters;
    auto session = sessionWithItems(packs, adapters);
    // The partner starts as a rookie (level 3); item 125 asks for a champion (level 4).
    adapters.inventory->inventory().add(items::BigCrest, 1);
    const auto refused = adapters.inventory->use(*session, items::BigCrest);
    REQUIRE_MESSAGE(refused.has_value(), errorOf(refused));
    CHECK(refused->outcome == UseOutcome::Refused);

    PartnerState state = session->partner().state();
    state.species = "grey";
    REQUIRE(session->partner().restore(state).has_value());
    const auto used = adapters.inventory->use(*session, items::BigCrest);
    REQUIRE_MESSAGE(used.has_value(), errorOf(used));
    CHECK(used->outcome == UseOutcome::Used);
}

TEST_CASE("buying a key item asks for the key item autosave; a baby buys at the discount") {
    const TestPacks packs;
    MemorySaveStorage storage;
    Adapters adapters;
    auto session = sessionWithItems(packs, adapters, &storage);
    adapters.inventory->inventory().addMoney(Bits);
    const AllInStock stock;
    const auto bought = adapters.inventory->buy(*session, items::Rod, 1, stock);
    REQUIRE_MESSAGE(bought.has_value(), errorOf(bought));
    CHECK(bought->keyItem);
    CHECK(bought->saved == 0);
    CHECK(autosaveRequested(*session, AutosaveReason::KeyItem));

    PartnerState state = session->partner().state();
    state.species = "bota";
    REQUIRE(session->partner().restore(state).has_value());
    const auto discounted = adapters.inventory->buy(*session, items::Tonic, 1, stock);
    REQUIRE_MESSAGE(discounted.has_value(), errorOf(discounted));
    CHECK(discounted->saved > 0);
}

TEST_CASE("the bag goes in the save and comes back") {
    const TestPacks packs;
    Adapters saving;
    auto saved = sessionWithItems(packs, saving);
    saving.inventory->inventory().add(items::Bun, 7);
    saving.inventory->inventory().addMoney(Bits);
    auto file = saved->saveFile();
    REQUIRE_MESSAGE(file.has_value(), errorOf(file));
    REQUIRE(file->sections.contains("inventory"));

    Adapters loading;
    auto loaded = sessionWithItems(packs, loading);
    const auto read = loaded->loadFile(*file);
    REQUIRE_MESSAGE(read.has_value(), errorOf(read));
    CHECK(loading.inventory->inventory().state() == saving.inventory->inventory().state());
}

TEST_CASE("a defeat takes stacks with the session's random numbers: the same seed loses the same ones") {
    const TestPacks packs;
    Adapters first;
    Adapters second;
    auto one = sessionWithItems(packs, first);
    auto two = sessionWithItems(packs, second);
    for (const std::int32_t item : {items::Tonic, items::Bun, items::Steak, items::BrainChip, items::Toilet}) {
        first.inventory->inventory().add(item, 1);
        second.inventory->inventory().add(item, 1);
    }
    const auto lost = first.inventory->loseOnDefeat(*one);
    CHECK_FALSE(lost.empty());
    CHECK(lost == second.inventory->loseOnDefeat(*two));
}

TEST_CASE("the inventory adapter registers the inventory and item_use sections' checks") {
    const TestPacks packs;
    Adapters adapters;
    auto session = sessionWithItems(packs, adapters);
    CHECK_FALSE(session->rules().setValue("inventory", "bag.new_game_items", "[{item: 0, count: 0}]").has_value());
    CHECK_FALSE(session->rules().setValue("item_use", "meals", "[{from: 70, to: 38}]").has_value());
}
