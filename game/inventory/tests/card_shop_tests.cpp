#include <dwe/assets/card_table.hpp>
#include <dwe/game/inventory/card_shop.hpp>
#include <dwe/game/inventory/cards.hpp>
#include <dwe/ruleset/random.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace {

using dwe::assets::CardEntry;
using dwe::assets::CardTable;
using dwe::game::inventory::CardCollection;
using dwe::game::inventory::CardRules;
using dwe::game::inventory::CardStockRules;
using dwe::game::inventory::rollCardStock;
using dwe::game::inventory::SoldCard;
using dwe::ruleset::Random;

// Made-up cards: 1 of rarity 0 (price 500), 2 of rarity 1 (price 50), at most 3 copies.
constexpr std::int32_t MaxCopies = 3;
constexpr std::int32_t RarePrice = 500;
constexpr std::int32_t CommonPrice = 50;

CardCollection collection() {
  CardTable table{
      .prices = {RarePrice, CommonPrice},
      .cards = {
          CardEntry{.index = 1, .digimon = {}, .rarity = 0, .meritValue = 0, .iconPalette = {}},
          CardEntry{.index = 2, .digimon = {}, .rarity = 1, .meritValue = 0, .iconPalette = {}}}};
  return CardCollection{
      CardRules{.maxCopies = MaxCopies, .firstListed = 1, .lastListed = 3, .pack = {}, .lines = {}},
      std::move(table)};
}

} // namespace

TEST_CASE("a new day's card stock rolls each slot in the range and skips the skipped cards") {
  const CardStockRules rules{.size = 6, .first = 1, .last = 64, .skipped = {4}};
  Random random = Random::original(0x1234);
  Random replay = Random::original(0x1234);
  const std::vector<std::int32_t> stock = rollCardStock(rules, random);
  REQUIRE(stock.size() == 6);
  // dailyPStatTrigger: randomLimit(0x40) + 1 per slot, again while it is card 4.
  for (const std::int32_t card : stock) {
    std::int32_t expected = 0;
    do {
      expected = 1 + replay.below(64);
    } while (expected == 4);
    CHECK(card == expected);
  }
  CHECK(random.state() == replay.state());
}

TEST_CASE("a card stock keeps a card twice, as the original's check never retries") {
  // Only card 2 can come out: every slot holds it.
  const CardStockRules rules{.size = 3, .first = 1, .last = 2, .skipped = {1}};
  Random random{7};
  CHECK(rollCardStock(rules, random) == std::vector<std::int32_t>{2, 2, 2});
  // A range with nothing to roll gives no stock instead of rolling forever.
  CHECK(rollCardStock({.size = 3, .first = 1, .last = 1, .skipped = {1}}, random).empty());
}

TEST_CASE("the buy list prices each card and tells what the player can buy") {
  CardCollection cards = collection();
  REQUIRE(cards.giveCopy(2));
  const auto offers = cardOffers({1, SoldCard, 2, 9}, cards, CommonPrice);
  REQUIRE(offers.size() == 4);
  // Card 1 costs more than the money; card 2 is held already; card 9 is not in the table.
  CHECK(offers[0].card == 1);
  CHECK(offers[0].price == RarePrice);
  CHECK_FALSE(offers[0].buyable);
  CHECK(offers[0].unheld);
  CHECK(offers[1].card == SoldCard);
  CHECK(offers[2].buyable);
  CHECK_FALSE(offers[2].unheld);
  CHECK(offers[3].card == SoldCard);
  CHECK(hasCardOnSale(offers));
  CHECK_FALSE(hasCardOnSale(cardOffers({SoldCard, SoldCard}, cards, RarePrice)));
  // A card at the most copies cannot be bought.
  REQUIRE(cards.giveCopy(2));
  REQUIRE(cards.giveCopy(2));
  CHECK_FALSE(cardOffers({2}, cards, RarePrice)[0].buyable);
}

TEST_CASE("the shop buys a card for half its price and the copies sold leave") {
  CardCollection cards = collection();
  CHECK(cardSellPrice(cards, 1, 2) == RarePrice / 2);
  CHECK_FALSE(cardSellPrice(cards, 9, 2).has_value());
  REQUIRE(cards.giveCopy(1));
  REQUIRE(cards.giveCopy(1));
  CHECK_FALSE(cards.sellCopies(1, 3));
  CHECK(cards.sellCopies(1, 2));
  CHECK(cards.copies(1) == 0);
  CHECK_FALSE(cards.sellCopies(9, 1));
}

TEST_CASE("the sell list has one row per listed card, empty for the ones not held") {
  CardCollection cards = collection();
  REQUIRE(cards.giveCopy(2));
  const auto rows = cardSaleRows(cards);
  REQUIRE(rows.size() == 3);
  CHECK_FALSE(rows[0].has_value());
  REQUIRE(rows[1].has_value());
  CHECK(rows[1]->card == 2);
  CHECK(rows[1]->copies == 1);
  CHECK_FALSE(rows[2].has_value());
}

TEST_CASE("a pack draws the cards the original drew from the same generator state") {
  // A table shaped as the original's: 66 cards in number order, grouped by rarity as CARD_DATA
  // (the first and the last of rarity 5, which no roll gives).
  constexpr std::array<std::pair<std::int32_t, std::int32_t>, 7> Groups{
      {{5, 1}, {0, 5}, {1, 20}, {2, 25}, {3, 10}, {4, 4}, {5, 1}}};
  CardTable table{.prices = {}, .cards = {}};
  std::int32_t index = 0;
  for (const auto& [rarity, count] : Groups) {
    for (std::int32_t card = 0; card < count; ++card) {
      table.cards.push_back(CardEntry{
          .index = index++, .digimon = {}, .rarity = rarity, .meritValue = 0, .iconPalette = {}});
    }
  }
  CardCollection cards{
      CardRules{.maxCopies = 9,
                .firstListed = 1,
                .lastListed = 64,
                .pack = {.draws = 3, .rarityRoll = 100, .rarityBelow = {1, 5, 20, 50}},
                .lines = {}},
      std::move(table)};
  // Measured on the original (rollCard's entry, libc rand state at 0x80009010; 2026-10-30): the
  // first draw of a pack came from 0xCF0A481B and gave card 0x3D, the second from 0x0589E791 and
  // gave card 0x40.
  Random first = Random::original(0xCF0A481B);
  const auto drawnFirst = cards.drawPackCard(first);
  REQUIRE(drawnFirst.has_value());
  CHECK(drawnFirst->card == 0x3D);
  Random second = Random::original(0x0589E791);
  const auto drawnSecond = cards.drawPackCard(second);
  REQUIRE(drawnSecond.has_value());
  CHECK(drawnSecond->card == 0x40);
}
