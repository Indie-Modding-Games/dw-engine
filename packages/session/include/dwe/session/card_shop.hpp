#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/inventory/card_shop.hpp>
#include <dwe/session/cards_adapter.hpp>
#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/map_head_lines.hpp>
#include <dwe/session/session_system.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::session {

class InventoryAdapter;

// The ruleset section of the card shop (rules/<id>/card_shop.yaml).
inline constexpr std::string_view CardShopSection = "card_shop";

// What the keeper says, as line numbers of the shop's MAPHEAD section (shop/script_menu.c:
// openBuyCardMenu, openSellCardMenu): the stock of the day over the buy list, that there is none
// left, and the question over the sell list.
struct CardShopLines {
  std::int32_t today = 0;
  std::int32_t soldOut = 0;
  std::int32_t sellPrompt = 0;
};

struct CardShopRules {
  game::inventory::CardStockRules stock;
  // createItemMenuAmountBox: the shop pays the price divided by this (CARD_PRICES >> 1).
  std::int32_t sellDivisor = 0;
  // showMapHeadTextbox(..., 0x4d3): the section of the keeper's lines and the pack's.
  std::int32_t linesSection = 0;
  CardShopLines lines;
  // createSingleCardShopMenu: the "is it OK?" box, a line of another section.
  std::int32_t confirmSection = 0;
  std::int32_t confirmLine = 0;
  // createSingleCardShopMenu: a slot that cannot be bought fails with this sound.
  SoundCue failSound;
  // playShopSoundOnlyInSavannah (MAIN_D_801345C0): a trade plays this sound, only on this map.
  std::string tradeMap;
  SoundCue tradeSound;
  // The price of a pack the card shop's script charges (script 126: `money >= 500`, then
  // reduce_money(500) before routine 14); 0 (left out) turns the bulk purchase off.
  std::int32_t packPrice = 0;
};

core::Result<CardShopRules> readCardShopRules(const ruleset::Section& section);

// What routine 14 opens (script_interp.c:436): a pack of cards (rollCardPack), the buy menu
// (openBuyCardMenu, trigger 4) or the sell menu (openSellCardMenu, trigger 3).
enum class CardShopKind : std::uint8_t { Pack, Buy, Sell };

struct CardShopDesc {
  // The mounted packs, for script/maphead; they must outlive the shop.
  const assets::VirtualFileSystem* files = nullptr;
  // The player's cards and the bag's money; they must outlive the shop.
  CardsAdapter* cards = nullptr;
  InventoryAdapter* inventory = nullptr;
};

// A card of the bulk purchase's summary: how many copies the packs gave, and whether the player
// held none before them.
struct PackSummaryRow {
  std::int32_t card = 0;
  std::int32_t count = 0;
  bool isNew = false;
};

// The cards of some packs grouped by card, in card order.
[[nodiscard]] std::vector<PackSummaryRow> summarizePacks(const std::vector<PackCard>& drawn);

// The card shop the story opens (CALL_ROUTINE 14): the stock a new day rolls (SCRIPT_STATE
// smth[0..5], saved with the game), the pack's draws, one card bought from the stock and copies
// sold back. The game's screen shows the visit and closes it.
class CardShop final : public SessionSystem {
public:
  explicit CardShop(CardShopDesc desc);
  ~CardShop() override;
  CardShop(const CardShop&) = delete;
  CardShop& operator=(const CardShop&) = delete;
  CardShop(CardShop&&) = delete;
  CardShop& operator=(CardShop&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // A ruleset without the section has no card shop: routine 14 then closes at once.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  // A save without the section gets a new stock once loaded.
  core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
  // script_interp.c: initializeScripts runs a new day, which rolls the first stock.
  core::Result<void> startNewGame(GameSession& session) override;
  void tick(GameSession& session, const ruleset::ClockAdvance& advance) override;
  void afterLoad(GameSession& session) override;

  // dailyPStatTrigger: each new day rolls the stock again (the calendar calls it).
  void startNewDays(std::int32_t days);

  // Opens a visit; false (nothing opens) without the rules or the cards.
  bool open(CardShopKind kind);
  void close();
  [[nodiscard]] const std::optional<CardShopKind>& visit() const { return visit_; }
  [[nodiscard]] bool available() const;
  [[nodiscard]] const CardShopRules& rules() const;
  [[nodiscard]] const game::inventory::CardCollection& cards() const;

  [[nodiscard]] const std::vector<std::int32_t>& stock() const { return stock_; }
  // Replaces the stock, as a save holds it.
  void setStock(std::vector<std::int32_t> stock) { stock_ = std::move(stock); }

  // rollCardPack: how many cards a pack gives, and one of them (the copy goes to the player and
  // variable 249 names its Digimon for the line).
  [[nodiscard]] std::int32_t packDraws() const;
  std::optional<PackCard> drawPackCard();

  // The bulk purchase (an engine improvement, the bulk_trades option): whether routine 14 lets the
  // player open more packs at once.
  void setBulkPacks(bool enabled) { bulkPacks_ = enabled; }
  [[nodiscard]] bool bulkPacks() const { return bulkPacks_ && rules_ && rules_->packPrice > 0; }
  // The most packs one order opens: the one the script has paid and one more per pack price the
  // money still covers.
  [[nodiscard]] std::int32_t packLimit() const;
  // Opens `packs` packs (clamped to 1..packLimit) exactly as that many single purchases would: for
  // each pack after the first (paid by the script) the price, then the pack's draws in order.
  std::vector<PackCard> openPacks(std::int32_t packs);

  // hasAnyCardToBuy: the stock as the buy list shows it.
  [[nodiscard]] std::vector<game::inventory::CardOffer> offers() const;
  // tickSingleCardShop's yes: the card of the slot is bought (one copy, its price paid) and the
  // slot is sold. False, changing nothing, for a slot that cannot be bought.
  bool buy(std::size_t slot);

  // shopFillSellCardList: the sell list.
  [[nodiscard]] std::vector<std::optional<game::inventory::CardSaleRow>> saleRows() const;
  // What the shop pays for one copy of the card.
  [[nodiscard]] std::optional<std::int32_t> sellPrice(std::int32_t card) const;
  // tickItemMenuAmountBox's confirm: `count` copies go and their price is paid, up to the most
  // money. False, changing nothing, for copies the player does not hold.
  bool sell(std::int32_t card, std::int32_t count);

  // The sound of a slot that cannot be bought, and the one a trade plays on the trade sound's map.
  void playFailSound();
  void playTradeSound(std::string_view mapId);

  [[nodiscard]] std::int32_t money() const;
  [[nodiscard]] std::int32_t maxMoney() const;

  // The text key of a line of the shop's section, and of the "is it OK?" box (resolveMapHeadEntry);
  // empty when MAPHEAD's listing does not have it.
  [[nodiscard]] std::string lineKey(std::int32_t line) const;
  [[nodiscard]] std::string confirmKey() const;

private:
  class Serializer;

  void rollStock();
  [[nodiscard]] std::string keyOf(std::int32_t section, std::int32_t line) const;

  CardShopDesc desc_;
  std::optional<CardShopRules> rules_;
  std::optional<CardShopKind> visit_;
  std::unique_ptr<Serializer> serializer_;
  std::vector<std::int32_t> stock_;
  // A save without the stock: it is rolled once the game is loaded.
  bool rollAfterLoad_ = false;
  bool bulkPacks_ = false;
  // The session whose generator the rolls use: the last one that ticked, loaded or started a game.
  GameSession* session_ = nullptr;
  // MAPHEAD's sections as jump tables of text keys, read on the first visit.
  std::optional<MapHeadLineTables> lineTables_;
};

} // namespace dwe::session
