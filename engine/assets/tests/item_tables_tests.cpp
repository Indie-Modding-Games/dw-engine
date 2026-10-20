#include <dwe/assets/item_effect_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/assets/shop_table.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <string_view>

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// Made-up items in the shape dwx writes.
constexpr std::string_view FixtureItems = R"(items:
- id: "tonic"
  index: 0
  name: "tonic"
  description: "Heals"
  value: 100
  merit_value: 0
  sorting_value: 0
  item_color: 2
  droppable: 1
  unknown_1e: 0
- id: "gate_key"
  index: 7
  name: "gate key"
  description: null
  value: 0
  merit_value: 5
  sorting_value: 5
  item_color: 255
  droppable: 0
  unknown_1e: 0
)";

constexpr std::string_view FixtureEffects = R"(item_effects:
- item: "tonic"
  index: 0
  handler: "hp_healing"
  healing: 500
  evolves_to: null
  values: null
- item: "bun"
  index: 1
  handler: "food"
  healing: null
  evolves_to: null
  values:
    energy: 10
    tiredness_relief: 0
    happiness: 1
    discipline: 0
    weight: 2
    lifetime: 0
    max_hp: 0
    max_mp: 0
    hp: 0
    mp: 0
    offense: 0
    defense: 0
    speed: 0
    brain: 0
    training_boost_flag: 0
    training_boost_value: 0
    training_boost_hours: 0
    sickness_chance: 0
- item: "pin"
  index: 2
  handler: "chip"
  healing: null
  evolves_to: null
  values:
    max_hp: 500
    max_mp: 0
    offense: 0
    defense: 0
    speed: 0
    brain: 0
    lifetime: 0
- item: "crest"
  index: 3
  handler: "evolution"
  healing: null
  evolves_to: "drake"
  values: null
- item: "whistle"
  index: 4
  handler: "chip"
  healing: null
  evolves_to: null
  values: null
)";

} // namespace

TEST_CASE("the item table reads every item and finds them by id and index") {
    const auto table = dwe::assets::parseItemTable(FixtureItems);
    REQUIRE_MESSAGE(table.has_value(), errorOf(table));
    REQUIRE(table->items.size() == 2);
    const dwe::assets::ItemEntry* tonic = table->find("tonic");
    REQUIRE(tonic != nullptr);
    CHECK(tonic->value == 100);
    CHECK(tonic->itemColor == 2);
    CHECK(tonic->droppable == 1);
    const dwe::assets::ItemEntry* key = table->findByIndex(7);
    REQUIRE(key != nullptr);
    CHECK(key->id == "gate_key");
    CHECK(key->meritValue == 5);
    CHECK(key->sortingValue == 5);
    CHECK(table->find("nothing") == nullptr);
}

TEST_CASE("the item table refuses unknown keys and items listed twice") {
    std::string unknown{FixtureItems};
    unknown.replace(unknown.find("unknown_1e: 0"), 13, "colour: 3");
    CHECK_FALSE(dwe::assets::parseItemTable(unknown).has_value());
    std::string twice{FixtureItems};
    twice.replace(twice.find("index: 7"), 8, "index: 0");
    const auto table = dwe::assets::parseItemTable(twice);
    REQUIRE_FALSE(table.has_value());
    CHECK(table.error().message.find("listed twice") != std::string::npos);
}

TEST_CASE("the item effect table reads each handler with its numbers") {
    using dwe::assets::ItemHandler;
    const auto table = dwe::assets::parseItemEffectTable(FixtureEffects);
    REQUIRE_MESSAGE(table.has_value(), errorOf(table));
    REQUIRE(table->effects.size() == 5);
    CHECK(table->findByIndex(0)->handler == ItemHandler::HpHealing);
    CHECK(table->findByIndex(0)->healing == 500);
    const dwe::assets::ItemEffectEntry* bun = table->findByIndex(1);
    const dwe::assets::FoodValues food = bun->food.value_or(dwe::assets::FoodValues{});
    CHECK(bun->food.has_value());
    CHECK(food.energy == 10);
    CHECK(food.weight == 2);
    CHECK_FALSE(bun->chip.has_value());
    CHECK(table->findByIndex(2)->chip.has_value());
    CHECK(table->findByIndex(2)->chip.value_or(dwe::assets::ChipValues{}).maxHp == 500);
    CHECK(table->findByIndex(3)->evolvesTo == "drake");
    // A chip case the extractor could not decode has no values.
    CHECK_FALSE(table->findByIndex(4)->chip.has_value());
    CHECK(table->findByIndex(9) == nullptr);
}

TEST_CASE("the item effect table refuses an unknown handler and values on a handler without them") {
    std::string handler{FixtureEffects};
    handler.replace(handler.find("\"hp_healing\""), 12, "\"magic\"");
    CHECK_FALSE(dwe::assets::parseItemEffectTable(handler).has_value());
    std::string values{FixtureEffects};
    values.replace(values.find("handler: \"chip\""), 15, "handler: \"food\"");
    CHECK_FALSE(dwe::assets::parseItemEffectTable(values).has_value());
}

TEST_CASE("the shop table reads its maps and shopkeepers, named or raw") {
    const auto table = dwe::assets::parseShopTable("shop_maps:\n- \"inn01\"\n- 12\nshopkeeper_digimon:\n- \"mole\"\n");
    REQUIRE_MESSAGE(table.has_value(), errorOf(table));
    CHECK(table->shopMaps == std::vector<std::string>{"inn01", "12"});
    CHECK(table->shopkeepers == std::vector<std::string>{"mole"});
    CHECK_FALSE(dwe::assets::parseShopTable("shop_maps: []\n").has_value());
}
