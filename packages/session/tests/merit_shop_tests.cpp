#include <dwe/assets/card_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/session/cards_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/merit_shop.hpp>
#include <dwe/session/story_adapter.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "packages/game/inventory/tests/fixture.hpp"
#include "session_fixture.hpp"

namespace items = dwe::game::inventory::testing;
using dwe::game::evolution::testing::TableFixture;
using dwe::session::MeritShop;
using dwe::session::testing::evolutionAdapter;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

// Made-up cards in the shape dwx extracts: card 1 is worth 100 merit and card 2 30.
constexpr std::string_view CardTable = R"(prices: [5000, 1500]
cards:
- {index: 0, digimon: null, sprite_id: 5, merit_value: 0}
- {index: 1, digimon: "kabu", sprite_id: 0, merit_value: 100}
- {index: 2, digimon: "kabu", sprite_id: 1, merit_value: 30}
)";

// The made-up items, with merit prices on the tonic (20) and the brain chip (800).
dwe::assets::ItemTable meritItems() {
  std::vector<dwe::assets::ItemEntry> entries{items::fixtureItems().entries().begin(),
                                              items::fixtureItems().entries().end()};
  for (dwe::assets::ItemEntry& entry : entries) {
    if (entry.id == items::Tonic) {
      entry.meritValue = 20;
    } else if (entry.id == items::BrainChip) {
      entry.meritValue = 800;
    }
  }
  return dwe::assets::ItemTable{std::move(entries)};
}

// A new game on the game's rules with the bag, the cards and the merit exchange.
struct Exchange {
  TestPacks packs{{"data/cards.yaml", CardTable}};
  dwe::session::StoryAdapter story{dwe::session::StoryAdapterDesc{.files = &packs.files()}};
  dwe::session::InventoryAdapter* inventory = nullptr;
  dwe::session::CardsAdapter* cards = nullptr;
  MeritShop* meritShop = nullptr;
  std::unique_ptr<dwe::session::GameSession> session;

  Exchange() {
    const TableFixture forms;
    auto evolution = evolutionAdapter(packs, forms);
    auto bag = std::make_unique<dwe::session::InventoryAdapter>(
        dwe::session::InventoryAdapterDesc{.files = &packs.files(),
                                           .items = meritItems(),
                                           .effects = items::fixtureEffects(),
                                           .evolution = evolution.get()});
    auto table = dwe::assets::parseCardTable(CardTable);
    REQUIRE(table.has_value());
    auto held = std::make_unique<dwe::session::CardsAdapter>(story, std::move(*table), nullptr);
    auto exchange = std::make_unique<MeritShop>(dwe::session::MeritShopDesc{
        .files = &packs.files(), .cards = held.get(), .inventory = bag.get(), .story = &story});
    inventory = bag.get();
    cards = held.get();
    meritShop = exchange.get();
    std::vector<std::unique_ptr<dwe::session::SessionSystem>> playing;
    playing.push_back(std::move(evolution));
    playing.push_back(std::move(bag));
    playing.push_back(std::move(held));
    playing.push_back(std::move(exchange));
    session = newGameWith(packs, std::move(playing));
  }

  [[nodiscard]] MeritShop& shop() const { return *meritShop; }
  void setMerit(std::int32_t merit) const {
    dwe::session::TamerRecord record = session->tamer().record;
    record.merit = merit;
    session->setTamerRecord(record);
  }
  [[nodiscard]] dwe::assets::ItemHandle item(std::string_view id) const {
    const auto handle = inventory->items().handleOf(id);
    REQUIRE(handle.has_value());
    return *handle;
  }
};

} // namespace

TEST_CASE("the merit exchange takes a card for its merit and keeps the merit at 9999") {
  const Exchange exchange;
  REQUIRE(exchange.shop().open());
  REQUIRE(exchange.cards->giveCopy(1).has_value());
  REQUIRE(exchange.cards->giveCopy(1).has_value());
  REQUIRE(exchange.cards->giveCopy(2).has_value());
  const auto rows = exchange.shop().cardRows();
  REQUIRE(rows.size() >= 2);
  REQUIRE(rows[0].has_value());
  CHECK(rows[0]->copies == 2);
  // createMeritCardTradeDialogue: the worth and the line of the card's rarity (7 + spriteId).
  CHECK(exchange.shop().cardWorth(1) == 100);
  CHECK(exchange.shop().worthLine(1) == 7);
  CHECK(exchange.shop().worthLine(2) == 8);
  // openMeritShop state 8: MERIT += worth, a copy goes.
  CHECK(exchange.shop().giveCard(1));
  CHECK(exchange.shop().merit() == 100);
  CHECK(exchange.cards->copies(1) == 1);
  exchange.setMerit(9980);
  CHECK(exchange.shop().giveCard(2));
  CHECK(exchange.shop().merit() == 9999);
  CHECK(exchange.cards->copies(2) == 0);
  // A card not held is worth nothing and is not taken.
  CHECK_FALSE(exchange.shop().cardWorth(2).has_value());
  CHECK_FALSE(exchange.shop().giveCard(2));
  CHECK(exchange.shop().merit() == 9999);
}

TEST_CASE("the merit exchange gives an item for its merit price only when the merit reaches it") {
  const Exchange exchange;
  REQUIRE(exchange.shop().open());
  exchange.setMerit(500);
  const auto offers = exchange.shop().itemOffers();
  REQUIRE(offers.size() == 2);
  CHECK(offers[0].item == exchange.item(items::Tonic));
  CHECK(offers[0].buyable);
  CHECK(offers[1].item == exchange.item(items::BrainChip));
  CHECK_FALSE(offers[1].buyable);
  CHECK_FALSE(exchange.shop().takeItem(exchange.item(items::BrainChip)));
  CHECK(exchange.shop().merit() == 500);
  // openMeritShop state 0xc: MERIT -= price, giveItem(item, 1).
  CHECK(exchange.shop().takeItem(exchange.item(items::Tonic)));
  CHECK(exchange.shop().merit() == 480);
  CHECK(exchange.inventory->inventory().countOf(exchange.item(items::Tonic)) == 1);
}

TEST_CASE("the merit exchange's rules carry its lines, the lock trigger and the sounds") {
  const Exchange exchange;
  const auto& rules = exchange.shop().rules();
  CHECK(rules.linesSection == 1236);
  CHECK(rules.lines.choices == 1);
  CHECK(rules.lines.cardWorth == 7);
  CHECK(rules.mostMerit == 9999);
  CHECK(rules.failSound.sound == 11);
  CHECK(exchange.shop().choiceCancellable());
  // The stand-in packs have no MAPHEAD listing: the lines have no key, and nothing breaks.
  REQUIRE(exchange.shop().open());
  CHECK(exchange.shop().lineKey(rules.lines.greeting).empty());
  exchange.shop().playFailSound();
  exchange.shop().close();
  CHECK_FALSE(exchange.shop().visiting());
}
