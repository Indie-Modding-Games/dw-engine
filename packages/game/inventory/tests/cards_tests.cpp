#include <dwe/assets/card_table.hpp>
#include <dwe/game/inventory/cards.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <utility>

namespace {

using dwe::assets::CardEntry;
using dwe::assets::CardTable;
using dwe::game::inventory::CardCollection;
using dwe::game::inventory::CardRules;

// Made-up cards: numbers 1 and 2, and a limit of 3 copies.
constexpr std::int32_t MaxCopies = 3;

CardCollection collection() {
  CardTable table{
      .prices = {},
      .cards = {
          CardEntry{.index = 1, .digimon = {}, .rarity = 0, .meritValue = 0, .iconPalette = {}},
          CardEntry{.index = 2, .digimon = {}, .rarity = 0, .meritValue = 0, .iconPalette = {}}}};
  return CardCollection{
      CardRules{.maxCopies = MaxCopies, .firstListed = 1, .lastListed = 2, .pack = {}, .lines = {}},
      std::move(table)};
}

} // namespace

TEST_CASE("a script gives copies of a card up to the limit") {
  CardCollection cards = collection();
  for (std::int32_t given = 0; given < MaxCopies + 2; ++given) {
    CHECK(cards.giveCopy(2));
  }
  CHECK(cards.copies(2) == MaxCopies);
  CHECK(cards.copies(1) == 0);
}

TEST_CASE("a script takes copies of a card down to none") {
  CardCollection cards = collection();
  CHECK(cards.giveCopy(1));
  CHECK(cards.takeCopy(1));
  CHECK(cards.takeCopy(1));
  CHECK(cards.copies(1) == 0);
}

TEST_CASE("a script cannot give or take a card the table does not have") {
  CardCollection cards = collection();
  CHECK_FALSE(cards.giveCopy(7));
  CHECK_FALSE(cards.takeCopy(-1));
  CHECK(cards.state().copies.empty());
}
