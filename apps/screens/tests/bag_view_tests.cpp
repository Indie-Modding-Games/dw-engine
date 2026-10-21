#include <dwe/screens/bag_view.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>

#include "game/inventory/tests/fixture.hpp"

using namespace dwe::game::inventory::testing;

TEST_CASE("the bag view lists the usable slots with the items' names, counts and colour") {
    auto inventory = originalInventory();
    const auto& items = fixtureItems();
    inventory.add(handleOf(Tonic), 3);
    inventory.add(handleOf(GateKey), 1);
    inventory.addMoney(250);
    const dwe::screens::NameKeys names{nullptr, &items, nullptr};

    const auto view = dwe::screens::bagViewOf(inventory, items, names);
    CHECK(view.money == 250);
    REQUIRE(view.slots.size() == static_cast<std::size_t>(inventory.slotCount()));
    CHECK(view.slots[0] == dwe::screens::BagSlotView{.empty = false,
                                                     .item = std::string{Tonic},
                                                     .nameKey = "main.item_names.0000",
                                                     .descriptionKey = "main.item_descs.0000",
                                                     .count = 3,
                                                     .dimmed = false,
                                                     .usable = true});
    CHECK(view.slots[1].item == GateKey);
    CHECK(view.slots[1].dimmed);
    CHECK(view.slots[2].empty);

    const auto settings = dwe::screens::settingsOf(view);
    CHECK(std::ranges::any_of(settings, [](const dwe::screens::ScreenSetting& setting) {
        return setting.name == "bag.slot.0.count" && setting.value == dwe::ui::UiValue{3.0};
    }));
}

TEST_CASE("the bag view says which items are used where the bag is open, by their item colour") {
    auto inventory = originalInventory();
    const auto& items = fixtureItems();
    inventory.add(handleOf(Tonic), 1);
    inventory.add(handleOf(Ether), 1);
    inventory.add(handleOf(Bun), 1);
    const dwe::screens::NameKeys names{nullptr, &items, nullptr};
    using dwe::game::inventory::UseContext;

    // inventory.c: selectOptionMenu: the field takes item_color 0 and 2, the battle 1 and 2.
    const auto field = dwe::screens::bagViewOf(inventory, items, names, UseContext::Field);
    CHECK(field.slots[0].usable);
    CHECK_FALSE(field.slots[1].usable);
    CHECK(field.slots[2].usable);
    const auto battle = dwe::screens::bagViewOf(inventory, items, names, UseContext::Battle);
    CHECK(battle.slots[0].usable);
    CHECK(battle.slots[1].usable);
    CHECK_FALSE(battle.slots[2].usable);
}
