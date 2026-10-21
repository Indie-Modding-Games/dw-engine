#include <dwe/core/yaml.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/inventory/rules.hpp>
#include <dwe/game/inventory/stacking.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "fixture.hpp"

using namespace dwe::game::inventory;
using namespace dwe::game::inventory::testing;

namespace {

// Another stacking rule, to show that the section picks one: it fills every stack of the item and then new slots.
class SpillStacking final : public StackingRule {
public:
    [[nodiscard]] AddResult add(std::span<std::optional<Stack>> slots, dwe::assets::ItemHandle item, std::int32_t count,
                                std::int32_t maxStack, bool /*overflowIsLost*/) const override {
        AddResult result;
        for (std::optional<Stack>& slot : slots) {
            if (count == 0) {
                break;
            }
            if (!slot) {
                slot = Stack{.item = item, .count = 0};
            }
            if (slot->item != item) {
                continue;
            }
            const std::int32_t moved = std::min(count, maxStack - slot->count);
            slot->count += moved;
            result.added += moved;
            count -= moved;
        }
        result.refused = count;
        return result;
    }
};

} // namespace

TEST_CASE("the original ruleset's inventory section has the original's numbers") {
    const InventoryRules rules = originalRules();
    CHECK(rules.bag.maxSlots == 30);
    CHECK(rules.bag.slotCounts == std::vector<std::int32_t>{10, 20, 30});
    CHECK(rules.bag.maxStack == 99);
    CHECK(rules.maxMoney == 999999);
    CHECK(rules.sortOrders.size() == 3);
    CHECK(rules.defeatLostPercent == 30);
    CHECK(rules.shop.stockTriggerBase == 0x180);
    CHECK(rules.shop.babyDiscountPercent == 90);
    CHECK(rules.quirks.stackOverflowIsLost);
    CHECK(rules.quirks.babyDiscountLimitedByFullPrice);
    CHECK(rules.quirks.omniDiskIgnoresLimit);
    CHECK(rules.quirks.uselessItemsConsumed);
}

TEST_CASE("a changed ruleset value changes the bag: a stack of 50") {
    dwe::ruleset::Ruleset ruleset = originalRuleset();
    REQUIRE(ruleset.setValue(SectionName, "bag.max_stack", "50").has_value());
    REQUIRE(dwe::ruleset::checkRuleset(ruleset, {{std::string{SectionName}, inventorySectionCheck()}}).has_value());
    auto inventory = Inventory::fromRuleset(ruleset);
    REQUIRE_MESSAGE(inventory.has_value(), errorOf(inventory));
    const AddResult added = inventory->add(handleOf(Tonic), 60);
    CHECK(added.added == 50);
    CHECK(added.lost == 10);
}

TEST_CASE("a bad value is refused at its line") {
    dwe::ruleset::Ruleset ruleset = originalRuleset();
    REQUIRE(ruleset.setValue(SectionName, "bag.new_game_slots", "15").has_value());
    const auto checked = dwe::ruleset::checkRuleset(ruleset, {{std::string{SectionName}, inventorySectionCheck()}});
    REQUIRE_FALSE(checked.has_value());
    CHECK(checked.error().message.find("slot_counts") != std::string::npos);
    dwe::ruleset::Ruleset unknown = originalRuleset();
    REQUIRE(unknown.setValue(SectionName, "bag.stacking", "spill").has_value());
    CHECK_FALSE(dwe::ruleset::checkRuleset(unknown, {{std::string{SectionName}, inventorySectionCheck()}}).has_value());
}

TEST_CASE("the section's stacking names the strategy: switching it to another one changes where items go") {
    dwe::ruleset::Ruleset ruleset = originalRuleset();
    REQUIRE(ruleset.setValue(SectionName, "bag.stacking", "spill").has_value());
    auto registry = stackingRules();
    registry.add("spill", [] { return std::make_unique<SpillStacking>(); });
    const dwe::ruleset::Section& section = *ruleset.section(SectionName);
    auto stacking = registry.create(section, StackingPath);
    REQUIRE_MESSAGE(stacking.has_value(), errorOf(stacking));
    auto rules = parseInventoryRules(section);
    REQUIRE_MESSAGE(rules.has_value(), errorOf(rules));
    Inventory inventory{std::move(*rules), std::move(*stacking)};
    const AddResult added = inventory.add(handleOf(Tonic), 150);
    CHECK(added.added == 150);
    CHECK(added.lost == 0);
    const auto countIn = [&](std::size_t slot) {
        return required(inventory.slots()[slot].transform([](const Stack& stack) { return stack.count; }));
    };
    CHECK(countIn(0) == 99);
    CHECK(countIn(1) == 51);
}

TEST_CASE("without the overflow quirk the rest of a stack is refused, not lost") {
    Inventory inventory = originalInventory();
    InventoryRules rules = inventory.rules();
    rules.quirks.stackOverflowIsLost = false;
    inventory.setRules(rules, stackingRules().create(*originalRuleset().section(SectionName), StackingPath).value());
    inventory.add(handleOf(Tonic), 95);
    const AddResult added = inventory.add(handleOf(Tonic), 10);
    CHECK(added.added == 4);
    CHECK(added.lost == 0);
    CHECK(added.refused == 6);
}

namespace {

// The original's inventory section with bag.new_game_items set to one item.
dwe::ruleset::Section withStartingItem(std::string_view item, std::int32_t count) {
    using dwe::core::YamlNode;
    dwe::ruleset::Section section = *originalRuleset().section(SectionName);
    YamlNode entry = YamlNode::mapping();
    entry.set("item", YamlNode::scalar(std::string{item}));
    entry.set("count", YamlNode::scalar(std::to_string(count)));
    YamlNode items = YamlNode::sequence();
    items.append(entry);
    const YamlNode* found = section.node.find("bag");
    REQUIRE(found != nullptr);
    YamlNode bag = *found;
    bag.set("new_game_items", items);
    section.node.set("bag", bag);
    return section;
}

} // namespace

TEST_CASE("the original starts with an empty bag: script dg 193 takes every item of the boot state away") {
    CHECK(originalRules().bag.newGameItems.empty());
}

TEST_CASE("a ruleset's new-game items are read, each count from 1 to max_stack") {
    const auto rules = parseInventoryRules(withStartingItem(Tonic, 3));
    REQUIRE_MESSAGE(rules.has_value(), errorOf(rules));
    CHECK(rules->bag.newGameItems == std::vector<StartingItem>{{.item = std::string{Tonic}, .count = 3}});
    const auto tooMany = parseInventoryRules(withStartingItem(Tonic, 100));
    REQUIRE_FALSE(tooMany.has_value());
    CHECK(tooMany.error().message.find("max_stack") != std::string::npos);
    CHECK_FALSE(parseInventoryRules(withStartingItem(Tonic, 0)).has_value());
}

TEST_CASE("a ruleset names its items by id: a number, or an id the items table lacks, is refused") {
    CHECK_FALSE(parseInventoryRules(withStartingItem("Not An Id", 1)).has_value());
    // A number left from the old format reads as an id, which no item has.
    const auto number = parseInventoryRules(withStartingItem("13", 1));
    REQUIRE_MESSAGE(number.has_value(), errorOf(number));
    CHECK(errorOf(checkItemIds(*number, fixtureItems())).find("'13'") != std::string::npos);
    const auto unknown = parseInventoryRules(withStartingItem("mymod:lantern", 1));
    REQUIRE_MESSAGE(unknown.has_value(), errorOf(unknown));
    CHECK(errorOf(checkItemIds(*unknown, fixtureItems())).find("mymod:lantern") != std::string::npos);
    const auto known = parseInventoryRules(withStartingItem(Tonic, 1));
    REQUIRE_MESSAGE(known.has_value(), errorOf(known));
    const auto checked = checkItemIds(*known, fixtureItems());
    CHECK_MESSAGE(checked.has_value(), errorOf(checked));
}
