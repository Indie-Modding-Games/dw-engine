#pragma once

#include <dwe/assets/card_table.hpp>
#include <dwe/assets/digimon_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/inventory/cards.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/session/session_system.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace dwe::session {

class StoryAdapter;

// A card of a pack, as the story names it: the line of the pack's text (MAPHEAD section 0x4d3)
// that announces it.
struct PackCard {
  std::int32_t card = 0;
  std::int32_t line = 0;
};

// The player's cards: the `cards` rules, data/cards and the `cards` save section. Without the
// table or the section the player has no cards and a pack gives none.
class CardsAdapter final : public SessionSystem {
public:
  // The story must outlive the adapter; `digimon` gives the number of each card's Digimon.
  CardsAdapter(StoryAdapter& story, std::optional<assets::CardTable> table,
               const assets::DigimonTable* digimon);
  ~CardsAdapter() override;
  CardsAdapter(const CardsAdapter&) = delete;
  CardsAdapter& operator=(const CardsAdapter&) = delete;
  CardsAdapter(CardsAdapter&&) = delete;
  CardsAdapter& operator=(CardsAdapter&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
  core::Result<void> startNewGame(GameSession& session) override;

  // The table that gives the number of each card's Digimon (the game loads it after the systems);
  // without it a pack's line names no Digimon.
  void setDigimon(const assets::DigimonTable* digimon);

  // How many cards a pack gives (rollCardPack); 0 without cards.
  [[nodiscard]] std::int32_t packDraws() const;

  // Draws one card of a pack (rollCard, showCardTextbox): the copy goes to the collection and the
  // story's variable 249 holds the card's Digimon, which the pack's line names.
  std::optional<PackCard> drawPackCard(ruleset::Random& random);

  // A copy of a card a script gives or takes (script_interp.c:1143, 1151: give_card, take_card).
  // Without cards nothing changes; a card the table does not have is an error.
  core::Result<void> giveCopy(std::int32_t card);
  core::Result<void> takeCopy(std::int32_t card);

  [[nodiscard]] std::int32_t copies(std::int32_t card) const;
  [[nodiscard]] std::vector<game::inventory::HeldCard> heldCards() const;
  // The collection, or nullptr without cards (the card shop trades on it).
  [[nodiscard]] game::inventory::CardCollection* collection() {
    return collection_ ? &*collection_ : nullptr;
  }
  [[nodiscard]] const game::inventory::CardCollection* collection() const {
    return collection_ ? &*collection_ : nullptr;
  }

private:
  class Serializer;

  StoryAdapter* story_;
  std::optional<assets::CardTable> table_;
  // The Digimon number of each card, by card number.
  std::vector<std::optional<std::int32_t>> digimonNumbers_;
  std::optional<game::inventory::CardCollection> collection_;
  std::unique_ptr<Serializer> serializer_;
};

} // namespace dwe::session
