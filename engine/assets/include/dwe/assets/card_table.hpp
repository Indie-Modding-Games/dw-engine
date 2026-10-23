#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// A card of `data/cards` (content/schemas/cards.schema.json): the executable's CARD_DATA (script.h:
// CardData). The card's number is its index.
struct CardEntry {
  std::int32_t index = 0;
  // The Digimon on the card (digimon.yaml id, or a raw number as text); empty for none.
  std::string digimon;
  // The card's rarity, 0 the rarest: the price class of `prices` and the pack roll's class
  // (shop/script_menu.c: rollCard). The extractor calls it sprite_id.
  std::int32_t rarity = 0;
  // The merit points the exchange gives for it (shop/script_menu.c:
  // createMeritCardTradeDialogue).
  std::int32_t meritValue = 0;
};

// The cards and their prices by rarity (CARD_PRICES).
struct CardTable {
  std::vector<std::int32_t> prices;
  std::vector<CardEntry> cards;

  // The card with this number, or nullptr.
  [[nodiscard]] const CardEntry* find(std::int32_t index) const;
  // The shop's price of a card of this rarity; nothing for a rarity with no price.
  [[nodiscard]] std::optional<std::int32_t> priceOf(std::int32_t rarity) const;
};

// Parses the text of `data/cards`. Unknown and missing keys are errors.
core::Result<CardTable> parseCardTable(std::string_view yaml);

// Reads and parses `data/cards`; nothing when no pack has it (the tests' stand-in base pack).
core::Result<std::optional<CardTable>> loadCardTable(const VirtualFileSystem& vfs);

} // namespace dwe::assets
