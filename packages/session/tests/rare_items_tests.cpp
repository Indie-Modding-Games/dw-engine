#include <dwe/assets/file_system.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/json_schema.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "packages/game/inventory/tests/fixture.hpp"

namespace items = dwe::game::inventory::testing;
using dwe::assets::CollisionCell;
using dwe::assets::ItemHandle;
using dwe::ruleset::Random;
using dwe::session::FieldItemsAdapter;
using dwe::session::GameSession;
using dwe::session::InventoryAdapter;
using dwe::session::InventoryAdapterDesc;
using dwe::session::RareItemDrop;
using dwe::session::rollRareItems;
using dwe::session::SessionSystem;
using dwe::session::StoryAdapter;
using dwe::session::StoryAdapterDesc;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

// The forest's spots in the hidden content's rare_items sections (script 18, section 0x00fe).
constexpr std::array<CollisionCell, 7> ForestSpots{{{.column = 33, .row = 49},
                                                    {.column = 49, .row = 53},
                                                    {.column = 67, .row = 42},
                                                    {.column = 34, .row = 29},
                                                    {.column = 50, .row = 34},
                                                    {.column = 49, .row = 18},
                                                    {.column = 65, .row = 22}}};
constexpr std::int32_t ForestHeadTrigger = 236;
constexpr std::string_view Forest = "trop06";

std::string_view textOf(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

dwe::core::YamlNode gamePackFile(std::string_view path) {
  const dwe::assets::DiskFileSystem disk;
  const auto bytes = disk.readFile(std::filesystem::path{DWE_GAME_PACK} / path);
  REQUIRE_MESSAGE(bytes.has_value(), errorOf(bytes));
  auto document = dwe::core::parseYaml(textOf(*bytes));
  REQUIRE_MESSAGE(document.has_value(), errorOf(document));
  return std::move(*document);
}

dwe::session::RareItemRules rareRulesOf(std::string_view ruleset) {
  const dwe::ruleset::Section section{
      .name = "rare_items",
      .owner = std::string{ruleset},
      .file = "rare_items.yaml",
      .node = gamePackFile(std::format("rules/{}/rare_items.yaml", ruleset))};
  auto rules = dwe::session::readRareItemRules(section);
  REQUIRE_MESSAGE(rules.has_value(), errorOf(rules));
  return std::move(*rules);
}

std::vector<ItemHandle> twoItems() {
  return {items::handleOf(items::Crest), items::handleOf(items::BigCrest)};
}

// The made-up items with the game pack's two rare items after them, numbered by the engine.
dwe::assets::ItemTable withRareItems() {
  const auto fixture = items::fixtureItems().entries();
  std::vector<dwe::assets::ItemEntry> entries{fixture.begin(), fixture.end()};
  for (const std::string_view id : {"game:blood_moon", "game:steel_heart"}) {
    entries.push_back({.id = std::string{id},
                       .index = static_cast<std::int32_t>(entries.size()) + 1'000,
                       .originalIndex = false,
                       .value = 9999,
                       .meritValue = 0,
                       .sortingValue = 4,
                       .itemColor = 0,
                       .droppable = 1});
  }
  return dwe::assets::ItemTable{std::move(entries)};
}

// A session on the hidden content with the bag (the packs' items: the game pack's rare ones), the
// story and the field items; a spot's chance is 100 so every load lays.
struct Field {
  std::unique_ptr<GameSession> session;
  InventoryAdapter* inventory = nullptr;
  StoryAdapter* story = nullptr;
  FieldItemsAdapter* fieldItems = nullptr;
};

Field fieldOnHiddenContent(const TestPacks& packs) {
  auto inventory =
      std::make_unique<InventoryAdapter>(InventoryAdapterDesc{.files = &packs.files(),
                                                              .items = withRareItems(),
                                                              .effects = items::fixtureEffects(),
                                                              .evolution = nullptr});
  auto story = std::make_unique<StoryAdapter>(StoryAdapterDesc{.files = &packs.files()});
  auto fieldItems = std::make_unique<FieldItemsAdapter>(*inventory, *story);
  Field field{.session = nullptr,
              .inventory = inventory.get(),
              .story = story.get(),
              .fieldItems = fieldItems.get()};
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(inventory));
  systems.push_back(std::move(story));
  systems.push_back(std::move(fieldItems));
  field.session = newGameWith(packs, std::move(systems), nullptr, 7, "oculto");
  const auto certain = field.session->rules().setValue("rare_items", "chance_percent", "100");
  REQUIRE_MESSAGE(certain.has_value(), errorOf(certain));
  field.session->tick(dwe::session::testing::Tick);
  REQUIRE(field.fieldItems->rareItemRules());
  REQUIRE(field.fieldItems->rareItemRules()->chancePercent == 100);
  return field;
}

std::size_t lyingCount(const FieldItemsAdapter& adapter) {
  return static_cast<std::size_t>(std::ranges::count_if(
      adapter.items().ground(), [](const auto& item) { return item.has_value(); }));
}

} // namespace

TEST_CASE("the hidden content's rare items: 1 in 100 on the forest's seven spots, both items with "
          "the Mega step") {
  const dwe::assets::DiskFileSystem disk;
  const auto schemaBytes =
      disk.readFile(std::filesystem::path{DWE_SCHEMA_DIR} / "ruleset_rare_items.schema.json");
  REQUIRE_MESSAGE(schemaBytes.has_value(), errorOf(schemaBytes));
  const auto schemaText = dwe::core::parseYaml(textOf(*schemaBytes));
  REQUIRE_MESSAGE(schemaText.has_value(), errorOf(schemaText));
  const auto schema = dwe::core::JsonSchema::compile(*schemaText);
  REQUIRE_MESSAGE(schema.has_value(), errorOf(schema));
  for (const std::string_view ruleset : {"oculto", "oculto_mega"}) {
    CAPTURE(ruleset);
    const auto document = gamePackFile(std::format("rules/{}/rare_items.yaml", ruleset));
    for (const dwe::core::SchemaProblem& problem : schema->check(document).problems) {
      FAIL_CHECK(std::format("{}:{}: {}: {}", problem.line, problem.column, problem.path,
                             problem.message));
    }
    const auto rules = rareRulesOf(ruleset);
    CHECK(rules.chancePercent == 1);
    REQUIRE(rules.maps.contains(Forest));
    const auto& forest = rules.maps.find(Forest)->second;
    CHECK(std::ranges::equal(forest.spots, ForestSpots));
    CHECK(forest.unlessTrigger == std::optional<std::int32_t>{ForestHeadTrigger});
  }
  CHECK(rareRulesOf("oculto").pool == std::vector<std::string>{"game:blood_moon"});
  CHECK(rareRulesOf("oculto_mega").pool ==
        std::vector<std::string>{"game:blood_moon", "game:steel_heart"});
}

TEST_CASE("a rare item roll: about 1 spot in 100 lays one, once a load, and the same seed lays "
          "the same") {
  constexpr int Loads = 20'000;
  Random random{1234};
  Random again{1234};
  std::size_t hits = 0;
  for (int load = 0; load < Loads; ++load) {
    const std::vector<RareItemDrop> drops = rollRareItems(ForestSpots, 1, twoItems(), random);
    hits += drops.size();
    if (drops.size() == 2) {
      CHECK(drops[0].item != drops[1].item);
    }
    const std::vector<RareItemDrop> replayed = rollRareItems(ForestSpots, 1, twoItems(), again);
    REQUIRE(replayed.size() == drops.size());
    for (std::size_t i = 0; i < drops.size(); ++i) {
      CHECK(replayed[i].item == drops[i].item);
      CHECK(replayed[i].cell == drops[i].cell);
    }
  }
  // 7 spots × 20 000 loads at 1 %: 1 400 expected; a few standard deviations (about 37) around it.
  CHECK(hits > 1'250);
  CHECK(hits < 1'550);
}

TEST_CASE("a rare item roll at 100 lays each item on the first spots; with none eligible it draws "
          "nothing") {
  Random random{99};
  const std::vector<RareItemDrop> drops = rollRareItems(ForestSpots, 100, twoItems(), random);
  REQUIRE(drops.size() == 2);
  CHECK(drops[0].cell == ForestSpots[0]);
  CHECK(drops[1].cell == ForestSpots[1]);
  CHECK(drops[0].item != drops[1].item);

  const std::uint64_t before = random.state();
  CHECK(rollRareItems(ForestSpots, 100, {}, random).empty());
  CHECK(random.state() == before);
}

TEST_CASE("the field lays a rare item as the forest loads, only with the option on, never over "
          "the head's spot, nor once the head lays nothing or the bag has it") {
  const TestPacks packs;
  Field field = fieldOnHiddenContent(packs);
  FieldItemsAdapter& adapter = *field.fieldItems;
  Random& random = field.session->random();
  const std::optional<ItemHandle> moon = field.inventory->items().handleOf("game:blood_moon");
  REQUIRE(moon);
  std::uint32_t load = 0;

  // Off (the default): nothing lies and nothing is drawn.
  CHECK_FALSE(adapter.rareItems());
  const std::uint64_t before = random.state();
  adapter.loadMap(Forest, ++load, 0.0F, random);
  CHECK(lyingCount(adapter) == 0);
  CHECK(random.state() == before);

  // On: the first spot lays the pool's item; the head's own item for that spot does not come.
  adapter.setRareItems(true);
  adapter.loadMap(Forest, ++load, 0.0F, random);
  REQUIRE(lyingCount(adapter) == 1);
  CHECK(adapter.items().ground()[0]->item == *moon);
  CHECK(adapter.items().ground()[0]->cell == ForestSpots[0]);
  CHECK(adapter.spawnItem(Forest, 0, ForestSpots[0], 0.0F));
  CHECK(lyingCount(adapter) == 1);
  // The same load again changes nothing; another map has no spots.
  adapter.loadMap(Forest, load, 0.0F, random);
  CHECK(lyingCount(adapter) == 1);
  adapter.loadMap("trop05", ++load, 0.0F, random);
  CHECK(lyingCount(adapter) == 0);

  // In the bag: it does not come again until used.
  field.inventory->inventory().add(*moon, 1);
  adapter.loadMap(Forest, ++load, 0.0F, random);
  CHECK(lyingCount(adapter) == 0);
  field.inventory->inventory().remove(*moon, 1);
  adapter.loadMap(Forest, ++load, 0.0F, random);
  CHECK(lyingCount(adapter) == 1);

  // The head lays nothing once its trigger is set, nor does the rare roll.
  REQUIRE(field.story->state().setTrigger(ForestHeadTrigger, true).has_value());
  adapter.loadMap(Forest, ++load, 0.0F, random);
  CHECK(lyingCount(adapter) == 0);
}

TEST_CASE("a bag with the rare items goes in the save by their ids and comes back") {
  const TestPacks packs;
  Field saving = fieldOnHiddenContent(packs);
  for (const std::string_view id : {"game:blood_moon", "game:steel_heart"}) {
    const std::optional<ItemHandle> item = saving.inventory->items().handleOf(id);
    REQUIRE(item);
    saving.inventory->inventory().add(*item, 1);
  }
  const auto file = saving.session->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  const auto text = dwe::core::emitYaml(file->sections.at("inventory").state);
  REQUIRE_MESSAGE(text.has_value(), errorOf(text));
  CHECK(text->contains("item: \"game:blood_moon\""));
  CHECK(text->contains("item: \"game:steel_heart\""));

  Field loading = fieldOnHiddenContent(packs);
  const auto read = loading.session->loadFile(*file);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  for (const std::string_view id : {"game:blood_moon", "game:steel_heart"}) {
    CAPTURE(id);
    const std::optional<ItemHandle> item = loading.inventory->items().handleOf(id);
    REQUIRE(item);
    CHECK(loading.inventory->inventory().countOf(*item) == 1);
  }
}
