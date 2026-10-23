#pragma once

#include <dwe/assets/card_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// The player's card collection (script_interp.c: getCardAmount, setCardAmount) and the card pack
// (shop/script_menu.c: rollCardPack, rollCard, showCardTextbox). The cards are data/cards; the
// pack's lines are the story's (MAPHEAD section 0x4d3).
namespace dwe::game::inventory {

// The ruleset section of the cards.
inline constexpr std::string_view CardsSectionName = "cards";

// A pack: `draws` cards; each rolls a rarity, then a card of that rarity, every one alike. The
// rarity roll is 0 to rarityRoll - 1; a roll below rarityBelow[r] (and not below an earlier bound)
// is rarity r, and a roll past the last bound is the next rarity.
struct CardPackRules {
  std::int32_t draws = 0;
  std::int32_t rarityRoll = 0;
  std::vector<std::int32_t> rarityBelow;
};

// The lines of the pack's text for a card drawn (showCardTextbox): a new card's by its rarity
// (past the list, the last), one held fewer than `fewCopies` times, and one held more often.
struct CardLines {
  std::vector<std::int32_t> newByRarity;
  std::int32_t fewCopies = 0;
  std::int32_t again = 0;
  std::int32_t many = 0;
};

struct CardRules {
  // The most copies of one card the player holds (showCardTextbox).
  std::int32_t maxCopies = 0;
  // The cards a list of the held ones shows (shopFillSellCardList: 1 to 0x40).
  std::int32_t firstListed = 0;
  std::int32_t lastListed = 0;
  CardPackRules pack;
  CardLines lines;
};

// Reads the `cards` section; errors name the line.
core::Result<CardRules> parseCardRules(const ruleset::Section& section);

// What a save keeps of the cards: the copies of each card by number.
struct CardState {
  std::vector<std::int32_t> copies;

  friend bool operator==(const CardState&, const CardState&) = default;
};

// A card the player holds, as a list of the held cards shows it.
struct HeldCard {
  std::int32_t card = 0;
  std::int32_t copies = 0;
};

// A card drawn from a pack, and the line of the pack's text that names it.
struct DrawnCard {
  std::int32_t card = 0;
  std::int32_t copiesBefore = 0;
  std::int32_t line = 0;
};

// The player's cards, on one card table.
class CardCollection {
public:
  CardCollection(CardRules rules, assets::CardTable table);

  [[nodiscard]] const CardRules& rules() const { return rules_; }
  [[nodiscard]] const assets::CardTable& table() const { return table_; }
  [[nodiscard]] const CardState& state() const { return state_; }

  // Takes the rules of a reloaded or tuned ruleset; the copies stay.
  void setRules(CardRules rules) { rules_ = std::move(rules); }

  // Replaces the state, as a save holds it; a card past the table is dropped.
  void restore(CardState state);

  [[nodiscard]] std::int32_t copies(std::int32_t card) const;

  // The cards held, in number order, from firstListed to lastListed (shopFillSellCardList).
  [[nodiscard]] std::vector<HeldCard> heldCards() const;

  // A pack's card (rollCard, showCardTextbox): its copy is added below maxCopies. Nothing when the
  // table has no card of the rolled rarity.
  std::optional<DrawnCard> drawPackCard(ruleset::Random& random);

  // A copy a script gives (script_interp.c:1143: give_card): one more, up to maxCopies. False for a
  // card the table does not have.
  bool giveCopy(std::int32_t card);
  // A copy a script takes (script_interp.c:1151: take_card): one fewer, down to none. False for a
  // card the table does not have.
  bool takeCopy(std::int32_t card);

private:
  [[nodiscard]] std::int32_t rollRarity(ruleset::Random& random) const;
  [[nodiscard]] std::int32_t lineFor(const assets::CardEntry& card, std::int32_t copies) const;
  void setCopies(std::int32_t card, std::int32_t copies);

  CardRules rules_;
  assets::CardTable table_;
  CardState state_;
};

} // namespace dwe::game::inventory
