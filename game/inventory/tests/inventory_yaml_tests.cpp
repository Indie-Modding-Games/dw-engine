#include <dwe/core/yaml.hpp>
#include <dwe/game/inventory/inventory_yaml.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "fixture.hpp"

using namespace dwe::game::inventory;
using namespace dwe::game::inventory::testing;

TEST_CASE("an inventory written as YAML reads back the same, through YAML text") {
    Inventory inventory = originalInventory();
    REQUIRE(inventory.setSlotCount(20).has_value());
    inventory.add(Tonic, 12);
    inventory.add(Bun, 3);
    inventory.remove(Tonic, 12);
    inventory.addMoney(4321);
    const auto text = dwe::core::emitYaml(toYaml(inventory));
    REQUIRE_MESSAGE(text.has_value(), errorOf(text));
    const auto node = dwe::core::parseYaml(*text);
    REQUIRE_MESSAGE(node.has_value(), errorOf(node));

    Inventory loaded = originalInventory();
    const auto result = fromYaml(loaded, *node);
    REQUIRE_MESSAGE(result.has_value(), errorOf(result));
    CHECK(loaded.state() == inventory.state());
    CHECK(loaded.countOf(Bun) == 3);
}

TEST_CASE("a state with impossible values, is refused and changes nothing") {
    const Inventory inventory = originalInventory();
    const auto text = dwe::core::emitYaml(toYaml(inventory));
    REQUIRE(text.has_value());
    for (const auto& [from, to] : {std::pair<std::string, std::string>{"slot_count: 10", "slot_count: 12"},
                                   {"money: 0", "money: 1000000"},
                                   {"- null", "- {item: 3, count: 0}"}}) {
        CAPTURE(to);
        std::string changed = *text;
        changed.replace(changed.find(from), from.size(), to);
        const auto node = dwe::core::parseYaml(changed);
        REQUIRE(node.has_value());
        Inventory target = originalInventory();
        target.add(Bun, 1);
        CHECK_FALSE(fromYaml(target, *node).has_value());
        CHECK(target.countOf(Bun) == 1);
    }
}
