#include <dwe/assets/item_table.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/session/discard_menu.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "game/inventory/tests/fixture.hpp"

namespace items = dwe::game::inventory::testing;
using dwe::game::evolution::testing::TableFixture;
using dwe::session::DiscardMenu;
using dwe::session::DiscardMenuDesc;
using dwe::session::DiscardRow;
using dwe::session::GameSession;
using dwe::session::InventoryAdapter;
using dwe::session::InventoryAdapterDesc;
using dwe::session::SessionSystem;
using dwe::session::StoryAdapter;
using dwe::session::StoryAdapterDesc;
using dwe::session::testing::evolutionAdapter;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

constexpr std::int32_t CancelTrigger = 3;
constexpr std::int32_t ChosenVariable = 254;
constexpr std::uint8_t NoItem = 255;

struct Picking {
  std::unique_ptr<GameSession> session;
  InventoryAdapter* inventory = nullptr;
  StoryAdapter* story = nullptr;
  DiscardMenu* menu = nullptr;

  [[nodiscard]] dwe::game::inventory::Inventory& bag() const { return inventory->inventory(); }
  [[nodiscard]] bool cancelSet() const {
    return story->state().trigger(CancelTrigger).value_or(false);
  }
  [[nodiscard]] std::uint8_t chosen() const {
    return story->state().variable(ChosenVariable).value_or(0);
  }
  void setCancel(bool set) const { REQUIRE(story->state().setTrigger(CancelTrigger, set)); }
  [[nodiscard]] DiscardRow row(std::string_view id) const {
    const auto rows = menu->rows();
    const auto found = std::ranges::find_if(
        rows, [&](const auto& shown) { return shown && shown->item == items::handleOf(id); });
    REQUIRE_MESSAGE(found != rows.end(), "the bag has no " << id);
    return **found;
  }
};

// A started session on the game's pack with the bag, the story and the menu on the made-up items.
Picking sessionWithMenu(const TestPacks& packs) {
  const TableFixture forms;
  auto evolution = evolutionAdapter(packs, forms);
  auto inventory =
      std::make_unique<InventoryAdapter>(InventoryAdapterDesc{.files = &packs.files(),
                                                              .items = items::fixtureItems(),
                                                              .effects = items::fixtureEffects(),
                                                              .evolution = evolution.get()});
  auto story = std::make_unique<StoryAdapter>(StoryAdapterDesc{.files = &packs.files()});
  auto menu = std::make_unique<DiscardMenu>(
      DiscardMenuDesc{.files = &packs.files(), .inventory = inventory.get(), .story = story.get()});
  Picking picking{
      .session = nullptr, .inventory = inventory.get(), .story = story.get(), .menu = menu.get()};
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(evolution));
  systems.push_back(std::move(inventory));
  systems.push_back(std::move(story));
  systems.push_back(std::move(menu));
  picking.session = newGameWith(packs, std::move(systems));
  // An empty bag, whatever the new game gave.
  std::vector<dwe::assets::ItemHandle> given;
  for (const auto& slot : picking.bag().slots()) {
    if (slot) {
      given.push_back(slot->item);
    }
  }
  for (const dwe::assets::ItemHandle item : given) {
    picking.bag().remove(item, picking.bag().countOf(item));
  }
  return picking;
}

} // namespace

TEST_CASE("the discard menu reads the original ruleset: MAPHEAD 0x4d8, trigger 3, variable 254") {
  const TestPacks packs;
  const Picking picking = sessionWithMenu(packs);
  REQUIRE(picking.menu->available());
  CHECK(picking.menu->rules().confirmSection == 0x4d8);
  CHECK(picking.menu->rules().confirmLine == 0);
  CHECK(picking.menu->rules().cancelTrigger == CancelTrigger);
  CHECK(picking.menu->rules().chosenVariable == ChosenVariable);
  CHECK(picking.menu->rules().noItem == NoItem);
}

TEST_CASE("openDiscardItem without a droppable item sets trigger 3 and 255 and stays closed") {
  const TestPacks packs;
  const Picking picking = sessionWithMenu(packs);
  picking.bag().add(items::handleOf(items::GateKey), 1);
  picking.setCancel(false);
  CHECK_FALSE(picking.menu->open());
  CHECK_FALSE(picking.menu->isOpen());
  CHECK(picking.cancelSet());
  CHECK(picking.chosen() == NoItem);
}

TEST_CASE("shopFillSellItemList lists every slot; only droppable items can be picked") {
  const TestPacks packs;
  const Picking picking = sessionWithMenu(packs);
  picking.bag().add(items::handleOf(items::Steak), 4);
  picking.bag().add(items::handleOf(items::GateKey), 1);
  REQUIRE(picking.menu->open());
  const auto rows = picking.menu->rows();
  CHECK(rows.size() == picking.bag().slots().size());
  CHECK(std::ranges::count_if(rows, [](const auto& shown) { return !shown; }) ==
        static_cast<std::ptrdiff_t>(picking.bag().slots().size()) - 2);
  CHECK(picking.row(items::Steak).choosable);
  CHECK(picking.row(items::Steak).count == 4);
  CHECK_FALSE(picking.row(items::GateKey).choosable);
  // A row that cannot be picked changes nothing (createSingleCardShopMenu's fail sound).
  CHECK_FALSE(picking.menu->choose(picking.row(items::GateKey)));
  CHECK(picking.menu->isOpen());
}

TEST_CASE("the yes of the confirm box writes the item's number to 254 and clears trigger 3") {
  const TestPacks packs;
  const Picking picking = sessionWithMenu(packs);
  picking.bag().add(items::handleOf(items::Steak), 4);
  picking.setCancel(true);
  REQUIRE(picking.menu->open());
  REQUIRE(picking.menu->choose(picking.row(items::Steak)));
  CHECK_FALSE(picking.menu->isOpen());
  CHECK(picking.chosen() == 39);
  CHECK_FALSE(picking.cancelSet());
  // The menu only names the item: the story's routine 13 takes it from the bag.
  CHECK(picking.bag().countOf(items::handleOf(items::Steak)) == 4);
}

TEST_CASE("cancel closes the menu only while trigger 3 is set, with 255 and the trigger cleared") {
  const TestPacks packs;
  const Picking picking = sessionWithMenu(packs);
  picking.bag().add(items::handleOf(items::Bun), 1);

  // A full bag on a gift: the story cleared trigger 3, so the player has to pick one.
  picking.setCancel(false);
  REQUIRE(picking.menu->open());
  CHECK_FALSE(picking.menu->cancellable());
  CHECK_FALSE(picking.menu->cancel());
  CHECK(picking.menu->isOpen());
  REQUIRE(picking.menu->choose(picking.row(items::Bun)));

  // Handing an item over: the story set trigger 3, so the player may keep everything.
  REQUIRE(picking.story->state().setVariable(ChosenVariable, 0));
  picking.setCancel(true);
  REQUIRE(picking.menu->open());
  CHECK(picking.menu->cancellable());
  REQUIRE(picking.menu->cancel());
  CHECK_FALSE(picking.menu->isOpen());
  CHECK(picking.chosen() == NoItem);
  CHECK_FALSE(picking.cancelSet());
}
