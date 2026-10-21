#include <dwe/assets/content_id.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/inventory/inventory_yaml.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fixture.hpp"

using namespace dwe::game::inventory;
using namespace dwe::game::inventory::testing;

TEST_CASE("an inventory written as YAML reads back the same, through YAML text") {
    Inventory inventory = originalInventory();
    REQUIRE(inventory.setSlotCount(20).has_value());
    inventory.add(handleOf(Tonic), 12);
    inventory.add(handleOf(Bun), 3);
    inventory.remove(handleOf(Tonic), 12);
    inventory.addMoney(4321);
    const auto text = dwe::core::emitYaml(toYaml(inventory, fixtureItems()));
    REQUIRE_MESSAGE(text.has_value(), errorOf(text));
    const auto node = dwe::core::parseYaml(*text);
    REQUIRE_MESSAGE(node.has_value(), errorOf(node));

    Inventory loaded = originalInventory();
    const auto result = fromYaml(loaded, fixtureItems(), *node);
    REQUIRE_MESSAGE(result.has_value(), errorOf(result));
    CHECK(loaded.state() == inventory.state());
    CHECK(loaded.countOf(handleOf(Bun)) == 3);
}

TEST_CASE("a state with impossible values, is refused and changes nothing") {
    const Inventory inventory = originalInventory();
    const auto text = dwe::core::emitYaml(toYaml(inventory, fixtureItems()));
    REQUIRE(text.has_value());
    for (const auto& [from, to] : {std::pair<std::string, std::string>{"slot_count: 10", "slot_count: 12"},
                                   {"money: 0", "money: 1000000"},
                                   {"- null", "- {item: bun, count: 0}"},
                                   {"- null", "- {item: nothing, count: x}"}}) {
        CAPTURE(to);
        std::string changed = *text;
        changed.replace(changed.find(from), from.size(), to);
        const auto node = dwe::core::parseYaml(changed);
        REQUIRE(node.has_value());
        Inventory target = originalInventory();
        target.add(handleOf(Bun), 1);
        CHECK_FALSE(fromYaml(target, fixtureItems(), *node).has_value());
        CHECK(target.countOf(handleOf(Bun)) == 1);
    }
}

TEST_CASE("a stack whose item is in no table mounted is dropped, named with its count, and the rest is read") {
    Inventory inventory = originalInventory();
    inventory.add(handleOf(Bun), 2);
    const auto text = dwe::core::emitYaml(toYaml(inventory, fixtureItems()));
    REQUIRE(text.has_value());
    std::string changed = *text;
    const std::string_view empty = "- null";
    changed.replace(changed.find(empty), empty.size(), "- {item: \"my_mod:pie\", count: 4}");
    changed.replace(changed.find(empty), empty.size(), "- {item: 38, count: 1}");
    const auto node = dwe::core::parseYaml(changed);
    REQUIRE(node.has_value());
    Inventory loaded = originalInventory();
    const auto dropped = fromYaml(loaded, fixtureItems(), *node);
    REQUIRE_MESSAGE(dropped.has_value(), errorOf(dropped));
    CHECK(*dropped == std::vector<DroppedStack>{{.item = "my_mod:pie", .count = 4}, {.item = "38", .count = 1}});
    CHECK(loaded.countOf(handleOf(Bun)) == 2);
    CHECK(std::ranges::count_if(loaded.slots(), [](const auto& slot) { return slot.has_value(); }) == 1);
}

TEST_CASE("a saved inventory names its items by id, never by a number") {
    Inventory inventory = originalInventory();
    inventory.add(handleOf(Steak), 2);
    inventory.add(handleOf(GateKey), 1);
    const auto text = dwe::core::emitYaml(toYaml(inventory, fixtureItems()));
    REQUIRE_MESSAGE(text.has_value(), errorOf(text));
    CHECK(text->find("item: \"steak\"") != std::string::npos);
    CHECK(text->find("item: \"gate_key\"") != std::string::npos);
    const auto node = dwe::core::parseYaml(*text);
    REQUIRE_MESSAGE(node.has_value(), errorOf(node));
    const dwe::core::YamlNode* slots = node->find("slots");
    REQUIRE(slots != nullptr);
    for (const dwe::core::YamlNode& slot : slots->items()) {
        const dwe::core::YamlNode* item = slot.isMapping() ? slot.find("item") : nullptr;
        if (item != nullptr) {
            CHECK(dwe::assets::ContentId::parse(item->text()).has_value());
            CHECK(fixtureItems().find(item->text()) != nullptr);
        }
    }
}

namespace {

// A saved inventory whose first `used` slots `text` lists, with the rest of the 30 empty.
std::string withEmptySlots(std::string text, std::int32_t used) {
    for (std::int32_t slot = used; slot < 30; ++slot) {
        text += "- null\n";
    }
    return text;
}

} // namespace

TEST_CASE("a version 1 inventory, which named its items by number, migrates to their ids") {
    // Version 1 as the session wrote it (30 slots, the rest empty): the second slot holds the original's item 39, the
    // fourth item 123.
    constexpr std::string_view VersionOne = R"(slot_count: 10
money: 250
slots:
- null
- {item: 39, count: 2}
- null
- {item: 123, count: 1}
)";
    std::string text{VersionOne};
    for (std::int32_t slot = 4; slot < 30; ++slot) {
        text += "- null\n";
    }
    const auto old = dwe::core::parseYaml(text);
    REQUIRE_MESSAGE(old.has_value(), errorOf(old));
    const auto migrated = itemNumbersToIds(*old, fixtureItems());
    REQUIRE_MESSAGE(migrated.has_value(), errorOf(migrated));
    Inventory loaded = originalInventory();
    const auto result = fromYaml(loaded, fixtureItems(), *migrated);
    REQUIRE_MESSAGE(result.has_value(), errorOf(result));
    CHECK(loaded.money() == 250);
    CHECK(loaded.countOf(handleOf(Steak)) == 2);
    CHECK(loaded.countOf(handleOf(GateKey)) == 1);
    REQUIRE(loaded.slots()[1].has_value());
    CHECK(idOf(loaded.slots()[1].value_or(Stack{handleOf(Tonic), 0}).item) == Steak);

    SUBCASE("a number the original does not have is dropped when read, and the rest of the save loads") {
        const auto unknown = dwe::core::parseYaml(
            withEmptySlots("slot_count: 10\nmoney: 7\nslots:\n- {item: 150, count: 1}\n- {item: 39, count: 1}\n", 2));
        REQUIRE(unknown.has_value());
        const auto kept = itemNumbersToIds(*unknown, fixtureItems());
        REQUIRE_MESSAGE(kept.has_value(), errorOf(kept));
        Inventory partial = originalInventory();
        const auto dropped = fromYaml(partial, fixtureItems(), *kept);
        REQUIRE_MESSAGE(dropped.has_value(), errorOf(dropped));
        CHECK(*dropped == std::vector<DroppedStack>{{.item = "150", .count = 1}});
        CHECK(partial.money() == 7);
        CHECK(partial.countOf(handleOf(Steak)) == 1);
        CHECK_FALSE(itemNumbersToIds(*dwe::core::parseYaml("slot_count: 10\nmoney: 0\nslots:\n- {item: x, count: 1}\n"),
                                     fixtureItems())
                        .has_value());
    }
    SUBCASE("an item a pack added never takes an old number") {
        // filler_0 has number 200, but the engine numbered it: a version 1 save could not name it.
        std::vector<dwe::assets::ItemEntry> entries{std::from_range, fixtureItems().entries()};
        entries.back().originalIndex = false;
        const dwe::assets::ItemTable added{std::move(entries)};
        const auto number = FillerBase + FillerCount - 1;
        const auto last = dwe::core::parseYaml(
            withEmptySlots(std::format("slot_count: 10\nmoney: 0\nslots:\n- {{item: {}, count: 1}}\n", number), 1));
        REQUIRE(last.has_value());
        const auto kept = itemNumbersToIds(*last, added);
        REQUIRE_MESSAGE(kept.has_value(), errorOf(kept));
        Inventory partial = originalInventory();
        const auto dropped = fromYaml(partial, added, *kept);
        REQUIRE_MESSAGE(dropped.has_value(), errorOf(dropped));
        CHECK(*dropped == std::vector<DroppedStack>{{.item = std::to_string(number), .count = 1}});
    }
}
