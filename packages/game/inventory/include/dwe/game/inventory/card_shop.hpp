#pragma once

#include <dwe/game/inventory/cards.hpp>
#include <dwe/ruleset/random.hpp>

#include <cstdint>
#include <optional>
#include <vector>

// The card shop's stock and trades (script_common.c: dailyPStatTrigger, tickSingleCardShop,
// tickItemMenuAmountBox; shop/script_menu.c: hasAnyCardToBuy, shopFillSellCardList). The prices are
// data/cards' (CARD_PRICES by rarity).
namespace dwe::game::inventory {

// A stock slot whose card was bought (the original's 0xFF).
inline constexpr std::int32_t SoldCard = -1;

// How a new day fills the stock (dailyPStatTrigger): `size` slots, each a card rolled from `first`
// to `last` that is not one of `skipped`.
struct CardStockRules {
  std::int32_t size = 0;
  std::int32_t first = 0;
  std::int32_t last = 0;
  std::vector<std::int32_t> skipped;
};

// A new day's stock. Each slot rolls first + randomLimit(last - first + 1) again while it lands on
// a skipped card. The original's check for a card already in the stock compares the slot with
// itself, so it never retries: a stock can hold one card twice, as here.
[[nodiscard]] std::vector<std::int32_t> rollCardStock(const CardStockRules& rules,
                                                      ruleset::Random& random);

// A slot of the buy list (hasAnyCardToBuy): its card (SoldCard for none) and price, whether the
// player can buy it (the money reaches the price and the card is below the most copies), and
// whether the player holds none yet (calculateCardMenuStrings: such a card shows in colour 7).
struct CardOffer {
  std::int32_t card = SoldCard;
  std::int32_t price = 0;
  bool buyable = false;
  bool unheld = false;
};

// The buy list of a stock; a card the table does not have shows as sold.
[[nodiscard]] std::vector<CardOffer> cardOffers(const std::vector<std::int32_t>& stock,
                                                const CardCollection& cards, std::int32_t money);

// Whether a buy list has a card at all (hasAnyCardToBuy's result): without one the keeper says the
// stock is gone and the menu does not open.
[[nodiscard]] bool hasCardOnSale(const std::vector<CardOffer>& offers);

// The shop's price of a card (CARD_PRICES[rarity]); nothing for a card or rarity without one.
[[nodiscard]] std::optional<std::int32_t> cardPrice(const CardCollection& cards, std::int32_t card);

// What the shop pays for one copy (createItemMenuAmountBox: CARD_PRICES[rarity] >> 1).
[[nodiscard]] std::optional<std::int32_t> cardSellPrice(const CardCollection& cards,
                                                        std::int32_t card, std::int32_t divisor);

// A row of the sell list (shopFillSellCardList): one per card from firstListed to lastListed, the
// card when the player holds it and nothing when not.
struct CardSaleRow {
  std::int32_t card = 0;
  std::int32_t copies = 0;
};
[[nodiscard]] std::vector<std::optional<CardSaleRow>> cardSaleRows(const CardCollection& cards);

} // namespace dwe::game::inventory
