#include <dwe/assets/entry_class.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/item_effect_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/assets/new_game_stats.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/shop_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

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

TEST_CASE("the item table reads every item and finds them by id and original number") {
  const auto table = dwe::assets::parseItemTable(FixtureItems);
  REQUIRE_MESSAGE(table.has_value(), errorOf(table));
  REQUIRE(table->entries().size() == 2);
  const dwe::assets::ItemEntry* tonic = table->find("tonic");
  REQUIRE(tonic != nullptr);
  CHECK(tonic->value == 100);
  CHECK(tonic->itemColor == 2);
  CHECK(tonic->droppable == 1);
  const dwe::assets::ItemEntry* key = table->findByOriginalIndex(7);
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
  CHECK(table->find("tonic")->handler == ItemHandler::HpHealing);
  CHECK(table->find("tonic")->healing == 500);
  const dwe::assets::ItemEffectEntry* bun = table->find("bun");
  const dwe::assets::FoodValues food = bun->food.value_or(dwe::assets::FoodValues{});
  CHECK(bun->food.has_value());
  CHECK(food.energy == 10);
  CHECK(food.weight == 2);
  CHECK_FALSE(bun->chip.has_value());
  CHECK(table->find("pin")->chip.has_value());
  CHECK(table->find("pin")->chip.value_or(dwe::assets::ChipValues{}).maxHp == 500);
  CHECK(table->find("crest")->evolvesTo == "drake");
  // A chip case the extractor could not decode has no values.
  CHECK_FALSE(table->find("whistle")->chip.has_value());
  CHECK(table->find("nothing") == nullptr);
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
  const auto table = dwe::assets::parseShopTable(
      "shop_maps:\n- \"inn01\"\n- 12\nshopkeeper_digimon:\n- \"mole\"\n");
  REQUIRE_MESSAGE(table.has_value(), errorOf(table));
  CHECK(table->shopMaps == std::vector<std::string>{"inn01", "12"});
  CHECK(table->shopkeepers == std::vector<std::string>{"mole"});
  CHECK_FALSE(dwe::assets::parseShopTable("shop_maps: []\n").has_value());
}

TEST_CASE("the new game stats give a listed partner its own stats and any other the rest") {
  constexpr std::string_view Text = R"(script: 7
at: 32
partners:
- digimon: "hero"
  stats: {offense: 1, defense: 2, speed: 3, brains: 4, max_hp: 50, max_mp: 60, hp: 50, mp: 60}
others: {offense: 5, defense: 6, speed: 7, brains: 8, max_hp: 70, max_mp: 80, hp: 70, mp: 80}
)";
  const auto table = dwe::assets::parseNewGameStats(Text);
  REQUIRE_MESSAGE(table.has_value(), errorOf(table));
  CHECK(table->script == 7);
  CHECK(table->at == 32);
  CHECK(table->statsOf("hero").brains == 4);
  CHECK(table->statsOf("hero").maxMp == 60);
  CHECK(table->statsOf("other").offense == 5);
  CHECK(table->statsOf("other").hp == 70);
  CHECK_FALSE(dwe::assets::parseNewGameStats("script: 7\nat: 32\npartners: []\n").has_value());
}

namespace {

// The made-up items in a base pack and a mod that replaces the gate key and adds a lantern: the
// base numbers 0 and 7, so the engine gives the lantern 8.
dwe::assets::VirtualFileSystem itemsWithMod() {
  struct Packs {
    dwe::assets::MemoryFileSystem memory;
    Packs() {
      memory.addFile("packs/base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
      memory.addFile("packs/base/data/items.yaml", std::string{FixtureItems});
      memory.addFile("packs/my_mod/pack.yaml", "id: my_mod\nversion: 1.0.0\nlayer: mods\n");
      memory.addFile("packs/my_mod/data/items.yaml", R"(items:
- id: "my_mod:lantern"
  name: "lantern"
  description: "Lights the way"
  value: 50
  merit_value: 0
  sorting_value: 5
  item_color: 0
  droppable: 1
  unknown_1e: 0
- id: "gate_key"
  name: "rusty key"
  description: null
  value: 40
  merit_value: 5
  sorting_value: 5
  item_color: 255
  droppable: 0
  unknown_1e: 0
)");
    }
  };
  static const Packs packs;
  // The base pack is the engine's own; the mod is found where mods are.
  const std::array<dwe::assets::PackLocation, 2> locations{{
      {.fileSystem = &packs.memory,
       .root = "packs/base",
       .origin = dwe::assets::PackOrigin::Engine},
      {.fileSystem = &packs.memory, .root = "packs/my_mod", .origin = dwe::assets::PackOrigin::Mod},
  }};
  auto vfs = dwe::assets::VirtualFileSystem::mount(locations);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  return std::move(*vfs);
}

dwe::assets::ItemTable loadedItems() {
  auto items = dwe::assets::loadItemTable(itemsWithMod());
  REQUIRE_MESSAGE(items.has_value(), errorOf(items));
  return std::move(*items);
}

} // namespace

TEST_CASE(
    "a handle names an entry of one composition: a copy of the table takes it, another composition refuses it") {
  static_assert(!std::is_constructible_v<dwe::assets::ItemHandle, int>);
  static_assert(!std::is_constructible_v<dwe::assets::ItemHandle, dwe::assets::MoveHandle>);
  static_assert(!std::is_default_constructible_v<dwe::assets::ItemHandle>);
  const dwe::assets::ItemTable items = loadedItems();
  const dwe::assets::ItemEntry* entry = items.find("my_mod:lantern");
  REQUIRE(entry != nullptr);
  const dwe::assets::ItemHandle lantern = items.handleOf(*entry);
  CHECK(items.handleOf("my_mod:lantern") == lantern);
  CHECK(items.owns(lantern));
  CHECK(items.at(lantern).id == "my_mod:lantern");
  CHECK_FALSE(items.handleOf("nothing").has_value());

  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization): the test is that a copy takes the handle.
  const dwe::assets::ItemTable copy = items;
  CHECK(copy.owns(lantern));
  // Mounting the packs again composes the tables again: the ids stay, the old handles do not.
  const dwe::assets::ItemTable recomposed = loadedItems();
  CHECK_FALSE(recomposed.owns(lantern));
  const dwe::assets::ItemEntry* again = recomposed.find("my_mod:lantern");
  REQUIRE(again != nullptr);
  CHECK(recomposed.handleOf(*again) != lantern);
  CHECK(again->id == items.at(lantern).id);
}

TEST_CASE(
    "a range of the original covers only the original's items: a pack's item, numbered after a gap, joins by id") {
  const dwe::assets::ItemTable items = loadedItems();
  const dwe::assets::ItemEntry* lantern = items.find("my_mod:lantern");
  REQUIRE(lantern != nullptr);
  CHECK(lantern->index == 8);
  CHECK_FALSE(lantern->originalIndex);
  // The mod's gate key replaces the base's: it keeps the original's number, so the ranges still
  // cover it.
  CHECK(items.find("gate_key")->value == 40);
  CHECK(items.find("gate_key")->originalIndex);
  CHECK(items.findByOriginalIndex(8) == nullptr);
  CHECK(items.findByOriginalIndex(7) == items.find("gate_key"));

  const dwe::assets::EntryClass wide{.ranges = {{.from = 0, .to = 20}}, .below = 100};
  CHECK(wide.contains(*items.find("gate_key")));
  CHECK_FALSE(wide.contains(*lantern));
  const dwe::assets::EntryClass named{
      .ranges = {}, .below = std::nullopt, .ids = {"my_mod:lantern"}};
  CHECK(named.contains(*lantern));

  // At load every number of a range is an item of the original, and every id an item.
  using dwe::assets::checkEntryClass;
  CHECK(checkEntryClass(dwe::assets::EntryClass{.ranges = {{.from = 7, .to = 7}}}, items, "meals",
                        "item")
            .has_value());
  const auto past = checkEntryClass(dwe::assets::EntryClass{.ranges = {{.from = 7, .to = 8}}},
                                    items, "meals", "item");
  REQUIRE_FALSE(past.has_value());
  CHECK(past.error().message.contains("number 8"));
  CHECK_FALSE(checkEntryClass(dwe::assets::EntryClass{.ranges = {{.from = 0, .to = 7}}}, items,
                              "meals", "item")
                  .has_value());
  CHECK(checkEntryClass(named, items, "meals", "item").has_value());
  const auto unknown = checkEntryClass(
      dwe::assets::EntryClass{.ranges = {}, .below = std::nullopt, .ids = {"my_mod:candle"}}, items,
      "meals", "item");
  REQUIRE_FALSE(unknown.has_value());
  CHECK(unknown.error().message.contains("my_mod:candle"));
}

TEST_CASE("a ruleset's list of entries mixes ids and ranges of the original's numbers") {
  const auto node = dwe::core::parseYaml(R"([{from: 0, to: 3}, "my_mod:lantern", tonic])");
  REQUIRE_MESSAGE(node.has_value(), errorOf(node));
  dwe::assets::YamlReader reader;
  dwe::assets::EntryClass entries;
  dwe::assets::readEntryList(reader, *node, "meals", entries);
  REQUIRE(reader.ok());
  CHECK(entries.ranges == std::vector<dwe::assets::IndexRange>{{.from = 0, .to = 3}});
  CHECK(entries.ids == std::vector<std::string>{"my_mod:lantern", "tonic"});

  for (const std::string_view bad :
       {"[{from: 3, to: 0}]", "[{from: -1, to: 2}]", "[{from: 1}]", "[Not An Id]"}) {
    CAPTURE(bad);
    const auto badNode = dwe::core::parseYaml(bad);
    REQUIRE(badNode.has_value());
    dwe::assets::YamlReader badReader;
    dwe::assets::EntryClass ignored;
    dwe::assets::readEntryList(badReader, *badNode, "meals", ignored);
    CHECK_FALSE(badReader.ok());
  }
}

TEST_CASE("the reader takes unsigned numbers up to their own limit") {
  const auto twelve = dwe::core::parseYaml("12");
  REQUIRE_MESSAGE(twelve.has_value(), errorOf(twelve));
  dwe::assets::YamlReader reader;
  CHECK(reader.integer<std::size_t>(*twelve) == 12);
  CHECK(reader.integer<std::uint64_t>(*twelve) == 12);
  CHECK(reader.ok());

  const auto negative = dwe::core::parseYaml("-1");
  REQUIRE(negative.has_value());
  dwe::assets::YamlReader negativeReader;
  CHECK(negativeReader.integer<std::size_t>(*negative) == 0);
  CHECK_FALSE(negativeReader.ok());

  const auto tooBig = dwe::core::parseYaml("300");
  REQUIRE(tooBig.has_value());
  dwe::assets::YamlReader tooBigReader;
  CHECK(tooBigReader.integer<std::uint8_t>(*tooBig) == 0);
  CHECK_FALSE(tooBigReader.ok());
}
