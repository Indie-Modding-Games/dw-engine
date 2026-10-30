#include <dwe/core/log.hpp>
#include <dwe/game/inventory/card_shop.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace dwe::game::inventory {

std::vector<std::int32_t> rollCardStock(const CardStockRules& rules, ruleset::Random& random) {
  std::vector<std::int32_t> stock;
  const std::int32_t span = rules.last - rules.first + 1;
  if (rules.size <= 0 || span <= 0) {
    return stock;
  }
  // A span made only of skipped cards would roll forever; the original's has 63 good ones.
  bool anyGood = false;
  for (std::int32_t card = rules.first; card <= rules.last && !anyGood; ++card) {
    anyGood = !std::ranges::contains(rules.skipped, card);
  }
  if (!anyGood) {
    return stock;
  }
  for (std::int32_t slot = 0; slot < rules.size; ++slot) {
    std::int32_t card = 0;
    do {
      card = rules.first + random.below(span);
    } while (std::ranges::contains(rules.skipped, card));
    stock.push_back(card);
  }
  std::string shown;
  for (const std::int32_t card : stock) {
    shown += (shown.empty() ? "" : " ") + std::to_string(card);
  }
  core::logDebug("[port:cards] dailyPStatTrigger (script_common.c:1398): card stock {}", shown);
  return stock;
}

std::optional<std::int32_t> cardPrice(const CardCollection& cards, std::int32_t card) {
  const assets::CardEntry* entry = cards.table().find(card);
  if (entry == nullptr) {
    return std::nullopt;
  }
  return cards.table().priceOf(entry->rarity);
}

std::optional<std::int32_t> cardSellPrice(const CardCollection& cards, std::int32_t card,
                                          std::int32_t divisor) {
  const std::optional<std::int32_t> price = cardPrice(cards, card);
  if (!price || divisor <= 0) {
    return std::nullopt;
  }
  return *price / divisor;
}

std::vector<CardOffer> cardOffers(const std::vector<std::int32_t>& stock,
                                  const CardCollection& cards, std::int32_t money) {
  std::vector<CardOffer> offers;
  offers.reserve(stock.size());
  for (const std::int32_t card : stock) {
    const std::optional<std::int32_t> price =
        card == SoldCard ? std::nullopt : cardPrice(cards, card);
    if (!price) {
      offers.push_back({});
      continue;
    }
    const std::int32_t held = cards.copies(card);
    offers.push_back({.card = card,
                      .price = *price,
                      .buyable = *price <= money && held < cards.rules().maxCopies,
                      .unheld = held == 0});
  }
  return offers;
}

bool hasCardOnSale(const std::vector<CardOffer>& offers) {
  return std::ranges::any_of(offers, [](const CardOffer& offer) { return offer.card != SoldCard; });
}

std::vector<std::optional<CardSaleRow>> cardSaleRows(const CardCollection& cards) {
  std::vector<std::optional<CardSaleRow>> rows;
  for (std::int32_t card = cards.rules().firstListed; card <= cards.rules().lastListed; ++card) {
    const std::int32_t copies = cards.copies(card);
    if (copies > 0) {
      rows.emplace_back(CardSaleRow{.card = card, .copies = copies});
    } else {
      rows.emplace_back(std::nullopt);
    }
  }
  return rows;
}

} // namespace dwe::game::inventory
