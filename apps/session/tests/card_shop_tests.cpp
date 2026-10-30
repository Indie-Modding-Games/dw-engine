#include <dwe/assets/card_table.hpp>
#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/inventory/card_shop.hpp>
#include <dwe/session/card_shop.hpp>
#include <dwe/session/cards_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "game/inventory/tests/fixture.hpp"
#include "session_fixture.hpp"

namespace items = dwe::game::inventory::testing;
using dwe::game::evolution::testing::TableFixture;
using dwe::game::inventory::SoldCard;
using dwe::session::CardShop;
using dwe::session::CardShopKind;
using dwe::session::testing::evolutionAdapter;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

// Made-up cards in the shape dwx extracts: card 1 costs 500 and card 2 costs 50.
constexpr std::string_view CardTable = R"(prices: [500, 50]
cards:
- {index: 0, digimon: null, sprite_id: 5, merit_value: 0}
- {index: 1, digimon: "kabu", sprite_id: 0, merit_value: 100}
- {index: 2, digimon: "kabu", sprite_id: 1, merit_value: 50}
)";

// A new game on the game's rules with the bag (made-up items), the cards and the card shop.
struct Shop {
  TestPacks packs{{"data/cards.yaml", CardTable}};
  dwe::session::StoryAdapter story{dwe::session::StoryAdapterDesc{.files = &packs.files()}};
  dwe::session::InventoryAdapter* inventory = nullptr;
  dwe::session::CardsAdapter* cards = nullptr;
  CardShop* cardShop = nullptr;
  std::unique_ptr<dwe::session::GameSession> session;

  Shop() {
    const TableFixture forms;
    auto evolution = evolutionAdapter(packs, forms);
    auto bag = std::make_unique<dwe::session::InventoryAdapter>(
        dwe::session::InventoryAdapterDesc{.files = &packs.files(),
                                           .items = items::fixtureItems(),
                                           .effects = items::fixtureEffects(),
                                           .evolution = evolution.get()});
    auto table = dwe::assets::parseCardTable(CardTable);
    REQUIRE(table.has_value());
    auto held = std::make_unique<dwe::session::CardsAdapter>(story, std::move(*table), nullptr);
    auto counter = std::make_unique<CardShop>(dwe::session::CardShopDesc{
        .files = &packs.files(), .cards = held.get(), .inventory = bag.get()});
    inventory = bag.get();
    cards = held.get();
    cardShop = counter.get();
    std::vector<std::unique_ptr<dwe::session::SessionSystem>> playing;
    playing.push_back(std::move(evolution));
    playing.push_back(std::move(bag));
    playing.push_back(std::move(held));
    playing.push_back(std::move(counter));
    session = newGameWith(packs, std::move(playing));
  }

  [[nodiscard]] CardShop& shop() const { return *cardShop; }
  [[nodiscard]] std::int32_t money() const { return shop().money(); }
  void setMoney(std::int32_t money) const {
    auto& bag = inventory->inventory();
    bag.spendMoney(bag.money());
    bag.addMoney(money);
  }
};

} // namespace

TEST_CASE("a new game rolls the card shop's stock, and so does each new day") {
  const Shop shop;
  const std::vector<std::int32_t> first = shop.shop().stock();
  REQUIRE(first.size() == 6);
  // dailyPStatTrigger: cards 1 to 64, never card 4.
  for (const std::int32_t card : first) {
    CHECK(card >= 1);
    CHECK(card <= 64);
    CHECK(card != 4);
  }
  const std::uint64_t before = shop.session->random().state();
  shop.shop().startNewDays(2);
  CHECK(shop.session->random().state() != before);
  CHECK(shop.shop().stock().size() == 6);
}

TEST_CASE("the card shop sells one card of the stock for its price") {
  const Shop shop;
  REQUIRE(shop.shop().open(CardShopKind::Buy));
  shop.shop().setStock({1, 2, SoldCard});
  shop.setMoney(520);
  const auto offers = shop.shop().offers();
  REQUIRE(offers.size() == 3);
  CHECK(offers[0].buyable);
  CHECK(offers[0].price == 500);
  CHECK(offers[0].unheld);
  CHECK(offers[2].card == SoldCard);
  CHECK_FALSE(shop.shop().buy(2));
  CHECK(shop.shop().buy(0));
  // tickSingleCardShop: the price is paid, a copy is added and the slot is sold.
  CHECK(shop.money() == 20);
  CHECK(shop.cards->copies(1) == 1);
  CHECK(shop.shop().stock()[0] == SoldCard);
  CHECK_FALSE(shop.shop().buy(0));
  // Card 2 costs 50: not with 20 bits.
  CHECK_FALSE(shop.shop().offers()[1].buyable);
  CHECK_FALSE(shop.shop().buy(1));
  CHECK(shop.money() == 20);
}

TEST_CASE("the card shop buys copies back for half their price") {
  const Shop shop;
  REQUIRE(shop.shop().open(CardShopKind::Sell));
  shop.setMoney(0);
  REQUIRE(shop.cards->giveCopy(1).has_value());
  REQUIRE(shop.cards->giveCopy(1).has_value());
  CHECK(shop.shop().sellPrice(1) == 250);
  const auto rows = shop.shop().saleRows();
  REQUIRE_FALSE(rows.empty());
  REQUIRE(rows[0].has_value());
  CHECK(rows[0]->copies == 2);
  CHECK_FALSE(shop.shop().sell(1, 3));
  CHECK(shop.shop().sell(1, 2));
  CHECK(shop.money() == 500);
  CHECK(shop.cards->copies(1) == 0);
  // The money stops at the most.
  REQUIRE(shop.cards->giveCopy(1).has_value());
  shop.setMoney(shop.shop().maxMoney() - 10);
  CHECK(shop.shop().sell(1, 1));
  CHECK(shop.money() == shop.shop().maxMoney());
}

TEST_CASE("the card shop names its lines from MAPHEAD's section") {
  const Shop shop;
  REQUIRE(shop.shop().open(CardShopKind::Pack));
  CHECK(shop.shop().packDraws() == 3);
  // The stand-in packs have no MAPHEAD listing: the lines have no key, and nothing breaks.
  CHECK(shop.shop().lineKey(shop.shop().rules().lines.today).empty());
  shop.shop().close();
  CHECK_FALSE(shop.shop().visit().has_value());
}

TEST_CASE("the card shop's rules carry the fail sound and the trade sound of the shop's map") {
  const Shop shop;
  const auto& rules = shop.shop().rules();
  // createSingleCardShopMenu: playSound(0, 0xB); playShopSoundOnlyInSavannah: playSound(0, 0x17)
  // only on screen 131, the card shop's map.
  CHECK(rules.failSound.sound == 11);
  CHECK(rules.tradeMap == "gias06b");
  CHECK(rules.tradeSound.sound == 23);
  // Without a sound port nothing plays, and nothing fails.
  shop.shop().playFailSound();
  shop.shop().playTradeSound("gias06b");
}

TEST_CASE("a pack's card puts its species in variable 249 once the cards have the species table") {
  const Shop shop;
  // showCardTextbox: writePStat(PSTAT_249, CARD_DATA[card].digimonId) before the line that names
  // it.
  dwe::assets::DigimonTable species;
  dwe::assets::DigimonRow kabuterimon;
  kabuterimon.id = "kabu";
  kabuterimon.index = 7;
  species.digimon.push_back(kabuterimon);
  shop.cards->setDigimon(&species);
  bool drawn = false;
  for (int tries = 0; tries < 1000 && !drawn; ++tries) {
    drawn = shop.shop().drawPackCard().has_value();
  }
  REQUIRE(drawn);
  const auto digimon = shop.story.state().variable(249);
  REQUIRE(digimon.has_value());
  CHECK(*digimon == 7);
}
