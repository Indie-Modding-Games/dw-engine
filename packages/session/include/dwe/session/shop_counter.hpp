#pragma once

#include <dwe/assets/entry_class.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/inventory/shop.hpp>
#include <dwe/session/session_system.hpp>

#include <cstdint>
#include <functional>
#include <map>
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
class StoryAdapter;

// The ruleset section of the shop counter (rules/<id>/shop.yaml).
inline constexpr std::string_view ShopSection = "shop";

// What the shopkeeper says at each step, as the line numbers of its MAPHEAD lines
// (script_common.c: openShop, showShopkeeperTextbox).
struct ShopLines {
  std::int32_t greeting = 0;
  std::int32_t nothingToSell = 0;
  std::int32_t choices = 0;
  std::int32_t farewellAfterTrade = 0;
  std::int32_t farewell = 0;
  std::int32_t bits = 0;
  std::int32_t buyPrompt = 0;
  std::int32_t sellPrompt = 0;
  std::int32_t thanks = 0;
  std::int32_t anythingElse = 0;
  std::int32_t discount = 0;
  // The recycle shop's choice (buy or leave) and what it says once it has nothing left
  // (script_menu.c: openRecycleShop).
  std::int32_t recycleChoices = 0;
  std::int32_t soldOut = 0;
};

struct ShopCounterRules {
  // getShopkeeperLine: a shopkeeper's lines are this MAPHEAD section plus its place in
  // data/shops' shopkeepers; a shop without an owner talks with the ownerless section.
  std::int32_t keeperLinesSection = 0;
  std::int32_t ownerlessLinesSection = 0;
  // script_interp.c: CALL_ROUTINE 8 opens the recycle shop instead while this trigger is set.
  std::int32_t recycleTrigger = 0;
  // RECYCLABLE_ITEMS: the items a lost battle hands to the recycle shop (handleItemLoss), which
  // keeps up to recycleMax of each.
  assets::EntryClass recyclable;
  std::int32_t recycleMax = 0;
  // tickItemMenuAmountBox: the alternative button moves the amount by this much.
  std::int32_t amountStep = 0;
  ShopLines lines;
};

core::Result<ShopCounterRules> readShopCounterRules(const ruleset::Section& section);
core::Result<void> checkShopSection(const ruleset::Section& section);

// Who keeps the shop the story opens: the NPC whose script variable 254 names it, by species.
struct ShopVisit {
  // No owner (variable 254 is 0xFF): the ownerless lines.
  bool ownerless = false;
  // The keeper's species id; one data/shops does not list talks with the first keeper's lines.
  std::string keeperSpecies;
  // The recycle shop (openRecycleShop): it sells back what lost battles took, and buys nothing.
  bool recycle = false;
};

struct ShopCounterDesc {
  // The mounted packs, for data/shops and script/maphead; they must outlive the counter.
  const assets::VirtualFileSystem* files = nullptr;
  // The bag and the story's stock triggers; they must outlive the counter.
  InventoryAdapter* inventory = nullptr;
  StoryAdapter* story = nullptr;
};

// The shop the story opens (script_interp.c: CALL_ROUTINE 8 -> script_common.c: openShop): it
// holds the open visit until the game's shop screen closes it, and trades through the bag with the
// story's stock triggers. The story waits while a visit is open.
class ShopCounter final : public SessionSystem {
public:
  explicit ShopCounter(ShopCounterDesc desc);
  ~ShopCounter() override;
  ShopCounter(const ShopCounter&) = delete;
  ShopCounter& operator=(const ShopCounter&) = delete;
  ShopCounter(ShopCounter&&) = delete;
  ShopCounter& operator=(ShopCounter&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // Reads the shop section; a ruleset without one has no shops, and a visit then closes at once.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  // The recycle shop's stock is saved; a save without it starts empty.
  core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
  core::Result<void> startNewGame(GameSession& session) override;

  void open(ShopVisit visit);
  void close();
  [[nodiscard]] const std::optional<ShopVisit>& visit() const { return visit_; }
  [[nodiscard]] bool available() const { return rules_.has_value(); }
  [[nodiscard]] const ShopCounterRules& rules() const;
  // Whether the recycle shop opens instead (its trigger is set).
  [[nodiscard]] bool recycles() const;
  // Whether the open visit is the recycle shop's.
  [[nodiscard]] bool recycling() const { return visit_ && visit_->recycle; }
  // How many of an item the recycle shop holds.
  [[nodiscard]] std::int32_t recycleCount(assets::ItemHandle item) const;
  // handleItemLoss (script_common.c:1357): the recyclable items a lost battle took go to the
  // recycle shop.
  void recycleLost(const std::vector<game::inventory::Stack>& lost);

  // The open visit's stock: the story's stock triggers, or the recycle shop's items.
  [[nodiscard]] std::vector<game::inventory::ShopOffer> stock() const;
  [[nodiscard]] std::vector<game::inventory::SaleOffer> saleOffers() const;
  // The price the shop shows: the full price, even for a baby partner (createItemMenuAmountBox).
  [[nodiscard]] std::int32_t shownBuyPrice(assets::ItemHandle item) const;
  [[nodiscard]] std::int32_t sellPrice(assets::ItemHandle item) const;
  [[nodiscard]] std::int32_t maxPurchase(GameSession& session, assets::ItemHandle item) const;
  [[nodiscard]] std::int32_t money() const;
  [[nodiscard]] std::int32_t maxMoney() const;
  // How many of an item the bag holds, all its stacks.
  [[nodiscard]] std::int32_t owned(assets::ItemHandle item) const;
  [[nodiscard]] const assets::ItemTable& items() const;

  core::Result<game::inventory::Purchase> buy(GameSession& session, assets::ItemHandle item,
                                              std::int32_t amount);
  core::Result<std::int32_t> sell(GameSession& session, assets::ItemHandle item,
                                  std::int32_t amount);

  // The text key of a line of the open visit's keeper (getShopkeeperLine); empty when MAPHEAD's
  // listing does not have it.
  [[nodiscard]] std::string lineKey(std::int32_t line) const;
  // The text key of a line of a keeper without opening a visit (getShopkeeperLine for the money
  // box, script_common.c: createShopBitsBox); empty without a shop section or MAPHEAD's line.
  [[nodiscard]] std::string keeperLineKey(const ShopVisit& visit, std::int32_t line);

private:
  class StockPort;
  class RecyclePort;
  class Serializer;

  [[nodiscard]] std::int32_t linesSection(const ShopVisit& visit) const;
  core::Result<void> loadLines();

  ShopCounterDesc desc_;
  std::optional<ShopCounterRules> rules_;
  std::optional<ShopVisit> visit_;
  std::unique_ptr<StockPort> stockPort_;
  std::unique_ptr<RecyclePort> recyclePort_;
  std::unique_ptr<Serializer> serializer_;
  // The recycle shop's stock, by item id (SCRIPT_STATE smth[6 + recycle id]).
  std::map<std::string, std::int32_t, std::less<>> recycled_;
  std::vector<std::string> keepers_;
  // MAPHEAD's sections as jump tables of text keys, read on the first visit.
  std::optional<std::map<std::int32_t, std::vector<std::string>>> lineTables_;
};

} // namespace dwe::session
