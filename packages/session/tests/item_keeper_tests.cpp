#include <dwe/assets/item_table.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/item_keeper.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "packages/game/inventory/tests/fixture.hpp"

namespace items = dwe::game::inventory::testing;
using dwe::game::evolution::testing::TableFixture;
using dwe::session::GameSession;
using dwe::session::InventoryAdapter;
using dwe::session::InventoryAdapterDesc;
using dwe::session::ItemKeeper;
using dwe::session::ItemKeeperDesc;
using dwe::session::KeeperAmount;
using dwe::session::KeeperRow;
using dwe::session::KeeperSide;
using dwe::session::SessionSystem;
using dwe::session::testing::errorOf;
using dwe::session::testing::evolutionAdapter;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

struct Keeping {
  InventoryAdapter* inventory = nullptr;
  ItemKeeper* keeper = nullptr;

  [[nodiscard]] dwe::game::inventory::Inventory& bag() const { return inventory->inventory(); }
  [[nodiscard]] std::int32_t inBag(std::string_view id) const {
    return bag().countOf(items::handleOf(id));
  }
  [[nodiscard]] std::int32_t kept(std::string_view id) const {
    return keeper->keptCount(items::handleOf(id));
  }
  // The bag's row of the item (its first slot).
  [[nodiscard]] KeeperRow bagRow(std::string_view id) const {
    const std::vector<std::optional<KeeperRow>> rows = keeper->bagRows();
    const auto found = std::ranges::find_if(
        rows, [&](const auto& row) { return row && row->item == items::handleOf(id); });
    REQUIRE_MESSAGE(found != rows.end(), "the bag has no " << id);
    return **found;
  }
  [[nodiscard]] KeeperRow keptRow(std::string_view id) const {
    const std::vector<KeeperRow> rows = keeper->keptRows();
    const auto found = std::ranges::find_if(
        rows, [&](const KeeperRow& row) { return row.item == items::handleOf(id); });
    REQUIRE_MESSAGE(found != rows.end(), "the keeper has no " << id);
    return *found;
  }
  // Leaves `count` of the item with the keeper, one at a time.
  void leave(std::string_view id, std::int32_t count) const {
    bag().add(items::handleOf(id), count);
    for (std::int32_t i = 0; i < count; ++i) {
      REQUIRE(keeper->move(KeeperSide::Bag, bagRow(id), KeeperAmount::One));
    }
  }
};

// A started session on the game's pack with the bag and the item keeper on the made-up items.
std::unique_ptr<GameSession> sessionWithKeeper(const TestPacks& packs, Keeping& keeping) {
  const TableFixture forms;
  auto evolution = evolutionAdapter(packs, forms);
  auto inventory =
      std::make_unique<InventoryAdapter>(InventoryAdapterDesc{.files = &packs.files(),
                                                              .items = items::fixtureItems(),
                                                              .effects = items::fixtureEffects(),
                                                              .evolution = evolution.get()});
  auto keeper = std::make_unique<ItemKeeper>(
      ItemKeeperDesc{.files = &packs.files(), .inventory = inventory.get()});
  keeping = {.inventory = inventory.get(), .keeper = keeper.get()};
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(evolution));
  systems.push_back(std::move(inventory));
  systems.push_back(std::move(keeper));
  return newGameWith(packs, std::move(systems));
}

} // namespace

TEST_CASE("the item keeper reads the original ruleset: 99 of an item, moves of ten, MAPHEAD 1237") {
  const TestPacks packs;
  Keeping keeping;
  auto session = sessionWithKeeper(packs, keeping);
  REQUIRE(keeping.keeper->available());
  CHECK(keeping.keeper->rules().maxCount == 99);
  CHECK(keeping.keeper->rules().step == 10);
  CHECK(keeping.keeper->rules().linesSection == 1237);
  CHECK(keeping.keeper->rules().lines.farewell == 2);
}

TEST_CASE("keeperMoveOne moves one item each way; the keeper's list follows the items' order") {
  const TestPacks packs;
  Keeping keeping;
  auto session = sessionWithKeeper(packs, keeping);
  keeping.bag().add(items::handleOf(items::Steak), 2);
  keeping.bag().add(items::handleOf(items::Tonic), 3);
  REQUIRE(keeping.keeper->move(KeeperSide::Bag, keeping.bagRow(items::Steak), KeeperAmount::One));
  REQUIRE(keeping.keeper->move(KeeperSide::Bag, keeping.bagRow(items::Tonic), KeeperAmount::One));
  CHECK(keeping.inBag(items::Tonic) == 2);
  CHECK(keeping.kept(items::Tonic) == 1);
  const std::vector<KeeperRow> rows = keeping.keeper->keptRows();
  REQUIRE(rows.size() == 2);
  const auto& table = keeping.keeper->items();
  CHECK(table.at(rows[0].item).index < table.at(rows[1].item).index);

  REQUIRE(keeping.keeper->move(KeeperSide::Kept, keeping.keptRow(items::Tonic), KeeperAmount::One));
  CHECK(keeping.inBag(items::Tonic) == 3);
  CHECK(keeping.kept(items::Tonic) == 0);
  CHECK(keeping.keeper->keptRows().size() == 1);
}

TEST_CASE("keeperMoveTen needs ten to move and room for ten on the other side (under 90)") {
  const TestPacks packs;
  Keeping keeping;
  auto session = sessionWithKeeper(packs, keeping);
  keeping.bag().add(items::handleOf(items::Bun), 9);
  CHECK_FALSE(keeping.keeper->move(KeeperSide::Bag, keeping.bagRow(items::Bun), KeeperAmount::Ten));
  keeping.bag().add(items::handleOf(items::Bun), 1);
  REQUIRE(keeping.keeper->move(KeeperSide::Bag, keeping.bagRow(items::Bun), KeeperAmount::Ten));
  CHECK(keeping.kept(items::Bun) == 10);
  CHECK(keeping.inBag(items::Bun) == 0);

  // The keeper holds 89 or less to take ten more: 80 + 10 fits, 90 + 10 does not.
  keeping.leave(items::Bun, 80);
  CHECK(keeping.kept(items::Bun) == 90);
  keeping.bag().add(items::handleOf(items::Bun), 10);
  CHECK_FALSE(keeping.keeper->move(KeeperSide::Bag, keeping.bagRow(items::Bun), KeeperAmount::Ten));

  // From the keeper, the bag's stack must be 89 or less.
  keeping.bag().add(items::handleOf(items::Bun), 80);
  CHECK(keeping.inBag(items::Bun) == 90);
  CHECK_FALSE(
      keeping.keeper->move(KeeperSide::Kept, keeping.keptRow(items::Bun), KeeperAmount::Ten));
  keeping.bag().remove(items::handleOf(items::Bun), 1);
  REQUIRE(keeping.keeper->move(KeeperSide::Kept, keeping.keptRow(items::Bun), KeeperAmount::Ten));
  CHECK(keeping.inBag(items::Bun) == 99);
  CHECK(keeping.kept(items::Bun) == 80);
}

TEST_CASE("keeperMoveStack moves the whole stack, up to what the other side still holds") {
  const TestPacks packs;
  Keeping keeping;
  auto session = sessionWithKeeper(packs, keeping);
  keeping.leave(items::Tonic, 95);
  keeping.bag().add(items::handleOf(items::Tonic), 30);
  REQUIRE(keeping.keeper->move(KeeperSide::Bag, keeping.bagRow(items::Tonic), KeeperAmount::Stack));
  CHECK(keeping.kept(items::Tonic) == 99);
  CHECK(keeping.inBag(items::Tonic) == 26);

  REQUIRE(
      keeping.keeper->move(KeeperSide::Kept, keeping.keptRow(items::Tonic), KeeperAmount::Stack));
  CHECK(keeping.inBag(items::Tonic) == 99);
  CHECK(keeping.kept(items::Tonic) == 26);
}

TEST_CASE("itemKeeperFillItemList: a row that has nowhere to go cannot move") {
  const TestPacks packs;
  Keeping keeping;
  auto session = sessionWithKeeper(packs, keeping);
  keeping.leave(items::Bun, 99);
  keeping.bag().add(items::handleOf(items::Bun), 1);
  CHECK_FALSE(keeping.bagRow(items::Bun).movable);
  CHECK(keeping.keptRow(items::Bun).movable);
  CHECK_FALSE(keeping.keeper->move(KeeperSide::Bag, keeping.bagRow(items::Bun), KeeperAmount::One));

  // A bag with every slot taken by other items and no bun stack: the keeper's bun stays.
  keeping.bag().remove(items::handleOf(items::Bun), 1);
  for (const std::string_view id :
       {items::Tonic, items::Ether, items::Floppy, items::Bandage, items::Revival, items::PowerDisk,
        items::OmniDisk, items::ScriptChip, items::BrainChip, items::Steak}) {
    keeping.bag().add(items::handleOf(id), 1);
  }
  CHECK_FALSE(keeping.keptRow(items::Bun).movable);
  CHECK_FALSE(
      keeping.keeper->move(KeeperSide::Kept, keeping.keptRow(items::Bun), KeeperAmount::One));
}

TEST_CASE(
    "what the keeper holds goes in the save and comes back; a save without it holds nothing") {
  const TestPacks packs;
  Keeping saving;
  auto saved = sessionWithKeeper(packs, saving);
  saving.leave(items::Steak, 12);
  saving.leave(items::Tonic, 3);
  auto file = saved->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  REQUIRE(file->sections.contains("item_keeper"));

  Keeping loading;
  auto loaded = sessionWithKeeper(packs, loading);
  REQUIRE(loaded->loadFile(*file).has_value());
  CHECK(loading.kept(items::Steak) == 12);
  CHECK(loading.kept(items::Tonic) == 3);

  file->sections.erase("item_keeper");
  REQUIRE(loaded->loadFile(*file).has_value());
  CHECK(loading.keeper->keptRows().empty());
}
