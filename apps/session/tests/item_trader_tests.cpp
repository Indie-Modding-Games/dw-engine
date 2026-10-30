#include <dwe/assets/item_table.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/story/story_state.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/item_trader.hpp>
#include <dwe/session/story_adapter.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "game/inventory/tests/fixture.hpp"

namespace items = dwe::game::inventory::testing;
using dwe::game::evolution::testing::TableFixture;
using dwe::game::inventory::Inventory;
using dwe::game::story::StoryState;
using dwe::session::GameSession;
using dwe::session::InventoryAdapter;
using dwe::session::InventoryAdapterDesc;
using dwe::session::ItemTrader;
using dwe::session::ItemTraderDesc;
using dwe::session::ItemTraderRules;
using dwe::session::makeTrade;
using dwe::session::SessionSystem;
using dwe::session::StoryAdapter;
using dwe::session::StoryAdapterDesc;
using dwe::session::TradeOutcome;
using dwe::session::tradeRows;
using dwe::session::testing::evolutionAdapter;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

constexpr std::int32_t TableVariable = 249;
constexpr std::uint8_t NoItem = 255;
constexpr std::int32_t FirstTraded = 5;
constexpr std::int32_t AllTraded = 9;

// Two tables of two trades on the made-up items: triggers 5 and 6, then 7 and 8; 9 once all are.
ItemTraderRules madeUpRules() {
  return ItemTraderRules{
      .linesSection = 0,
      .lines = {},
      .tableVariable = TableVariable,
      .noItem = NoItem,
      .firstTradedTrigger = FirstTraded,
      .allTradedTrigger = AllTraded,
      .choiceLockTrigger = 49,
      .refuseSound = {.slot = 0, .sound = 11},
      .tables = {{{.give = std::string{items::Steak}, .get = std::string{items::Tonic}},
                  {.give = std::string{items::Bun}, .get = std::string{items::Ether}}},
                 {{.give = std::string{items::Bandage}, .get = std::string{items::Floppy}},
                  {.give = std::string{items::Rod}, .get = std::string{items::PowerDisk}}}}};
}

Inventory emptyBag() {
  Inventory bag = items::originalInventory();
  std::vector<dwe::assets::ItemHandle> given;
  for (const auto& slot : bag.slots()) {
    if (slot) {
      given.push_back(slot->item);
    }
  }
  for (const dwe::assets::ItemHandle item : given) {
    bag.remove(item, bag.countOf(item));
  }
  return bag;
}

bool triggerSet(const StoryState& story, std::int32_t trigger) {
  return story.trigger(trigger).value_or(false);
}

} // namespace

TEST_CASE(
    "the item trader reads the original ruleset: MAPHEAD 0x4d6, variable 249, triggers 5-14") {
  const TestPacks packs;
  auto trader = std::make_unique<ItemTrader>(ItemTraderDesc{.files = &packs.files()});
  ItemTrader& reader = *trader;
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(trader));
  const auto session = newGameWith(packs, std::move(systems));
  REQUIRE(reader.available());
  const ItemTraderRules& rules = reader.rules();
  CHECK(rules.linesSection == 0x4d6);
  CHECK(rules.lines.greeting == 8);
  CHECK(rules.lines.ask == 9);
  CHECK(rules.lines.farewell == 10);
  CHECK(rules.lines.confirm == 11);
  CHECK(rules.lines.choices == 12);
  CHECK(rules.lines.thanks == 13);
  CHECK(rules.lines.bagFull == 14);
  CHECK(rules.tableVariable == TableVariable);
  CHECK(rules.noItem == NoItem);
  CHECK(rules.firstTradedTrigger == 5);
  CHECK(rules.allTradedTrigger == 14);
  CHECK(rules.choiceLockTrigger == 0x31);
  CHECK(rules.refuseSound.slot == 0);
  CHECK(rules.refuseSound.sound == 0xb);
  // MOJYAMON_ITEMS_GIVE and MOJYAMON_ITEMS_GET: three tables of three.
  REQUIRE(rules.tables.size() == 3);
  for (const auto& table : rules.tables) {
    CHECK(table.size() == 3);
  }
  CHECK(rules.tables[1][0].give == "giant_meat");
  CHECK(rules.tables[1][0].get == "auto_pilot");
  CHECK(rules.tables[1][2].give == "hispeed_dsk");
  CHECK(rules.tables[1][2].get == "medicine");
}

TEST_CASE("mojyaTradeFillItemList marks the items the bag holds and the trades already made") {
  const ItemTraderRules rules = madeUpRules();
  Inventory bag = emptyBag();
  StoryState story;
  bag.add(items::handleOf(items::Bandage), 2);
  REQUIRE(story.setTrigger(FirstTraded + 3, true));

  const auto rows = tradeRows(rules, 1, bag, items::fixtureItems(), story);
  REQUIRE(rows.size() == 2);
  CHECK(rows[0].give == items::handleOf(items::Bandage));
  CHECK(rows[0].get == items::handleOf(items::Floppy));
  CHECK(rows[0].owned);
  CHECK_FALSE(rows[0].traded);
  CHECK_FALSE(rows[1].owned);
  CHECK(rows[1].traded);
  // A table the rules do not have lists nothing.
  CHECK(tradeRows(rules, 2, bag, items::fixtureItems(), story).empty());
}

TEST_CASE("a trade gives the new item, takes one offered and sets the trade's trigger") {
  const ItemTraderRules rules = madeUpRules();
  Inventory bag = emptyBag();
  StoryState story;
  bag.add(items::handleOf(items::Bun), 3);

  CHECK(makeTrade(rules, 0, 1, bag, items::fixtureItems(), story) == TradeOutcome::Traded);
  CHECK(bag.countOf(items::handleOf(items::Bun)) == 2);
  CHECK(bag.countOf(items::handleOf(items::Ether)) == 1);
  CHECK(triggerSet(story, FirstTraded + 1));
  CHECK_FALSE(triggerSet(story, FirstTraded));
  CHECK_FALSE(triggerSet(story, AllTraded));
}

TEST_CASE("setMojyaItemTradedTrigger sets the last trigger once every trade is made") {
  const ItemTraderRules rules = madeUpRules();
  Inventory bag = emptyBag();
  StoryState story;
  for (const std::int32_t trigger : {FirstTraded, FirstTraded + 1, FirstTraded + 2}) {
    REQUIRE(story.setTrigger(trigger, true));
  }
  bag.add(items::handleOf(items::Rod), 1);
  CHECK(makeTrade(rules, 1, 1, bag, items::fixtureItems(), story) == TradeOutcome::Traded);
  CHECK(triggerSet(story, FirstTraded + 3));
  CHECK(triggerSet(story, AllTraded));
  // The last of a stack leaves its slot.
  CHECK(bag.countOf(items::handleOf(items::Rod)) == 0);
}

TEST_CASE("giveItem first: a full bag keeps the offered item and sets nothing") {
  const ItemTraderRules rules = madeUpRules();
  Inventory bag = emptyBag();
  StoryState story;
  // The offered item's single stack would free a slot, but the original gives before it takes.
  bag.add(items::handleOf(items::Steak), 1);
  for (std::int32_t filler = 0; bag.hasRoomFor(items::handleOf(items::Tonic)); ++filler) {
    REQUIRE(filler < items::FillerCount);
    bag.add(items::filler(filler), 1);
  }
  CHECK(makeTrade(rules, 0, 0, bag, items::fixtureItems(), story) == TradeOutcome::BagFull);
  CHECK(bag.countOf(items::handleOf(items::Steak)) == 1);
  CHECK(bag.countOf(items::handleOf(items::Tonic)) == 0);
  CHECK_FALSE(triggerSet(story, FirstTraded));
}

TEST_CASE("openMojyamonShop takes the table from variable 249 and writes 255 there") {
  const TestPacks packs;
  const TableFixture forms;
  auto evolution = evolutionAdapter(packs, forms);
  auto inventory =
      std::make_unique<InventoryAdapter>(InventoryAdapterDesc{.files = &packs.files(),
                                                              .items = items::fixtureItems(),
                                                              .effects = items::fixtureEffects(),
                                                              .evolution = evolution.get()});
  auto story = std::make_unique<StoryAdapter>(StoryAdapterDesc{.files = &packs.files()});
  auto trader = std::make_unique<ItemTrader>(
      ItemTraderDesc{.files = &packs.files(), .inventory = inventory.get(), .story = story.get()});
  StoryAdapter& state = *story;
  ItemTrader& visit = *trader;
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(evolution));
  systems.push_back(std::move(inventory));
  systems.push_back(std::move(story));
  systems.push_back(std::move(trader));
  const auto session = newGameWith(packs, std::move(systems));

  // A table the rules do not have: the routine closes at once.
  REQUIRE(state.state().setVariable(TableVariable, 3));
  CHECK_FALSE(visit.open({.traderSpecies = "trader"}));
  CHECK_FALSE(visit.visit());

  REQUIRE(state.state().setVariable(TableVariable, 1));
  REQUIRE(visit.open({.traderSpecies = "trader"}));
  CHECK(visit.visit());
  CHECK(state.state().variable(TableVariable).value_or(0) == NoItem);
  // The made-up item table has none of the original's trade items: no row can be offered.
  CHECK_FALSE(visit.offer(0));
  CHECK_FALSE(visit.offered());
  CHECK(visit.choiceCancellable());
  REQUIRE(state.state().setTrigger(0x31, true));
  CHECK_FALSE(visit.choiceCancellable());
  visit.close();
  CHECK_FALSE(visit.visit());
}
