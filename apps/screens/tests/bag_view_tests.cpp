#include <dwe/screens/bag_view.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>

#include "game/inventory/tests/fixture.hpp"

using namespace dwe::game::inventory::testing;

TEST_CASE("the bag view lists the usable slots with the items' names, counts and colour") {
    auto inventory = originalInventory();
    const auto items = fixtureItems();
    inventory.add(Tonic, 3);
    inventory.add(GateKey, 1);
    inventory.addMoney(250);
    const dwe::screens::NameKeys names{nullptr, &items, nullptr};

    const auto view = dwe::screens::bagViewOf(inventory, items, names);
    CHECK(view.money == 250);
    REQUIRE(view.slots.size() == static_cast<std::size_t>(inventory.slotCount()));
    CHECK(view.slots[0] == dwe::screens::BagSlotView{.empty = false,
                                                     .item = Tonic,
                                                     .nameKey = "main.item_names.0000",
                                                     .descriptionKey = "main.item_descs.0000",
                                                     .count = 3,
                                                     .dimmed = false});
    CHECK(view.slots[1].item == GateKey);
    CHECK(view.slots[1].dimmed);
    CHECK(view.slots[2].empty);

    const auto settings = dwe::screens::settingsOf(view);
    CHECK(std::ranges::any_of(settings, [](const dwe::screens::ScreenSetting& setting) {
        return setting.name == "bag.slot.0.count" && setting.value == dwe::ui::UiValue{3.0};
    }));
}
