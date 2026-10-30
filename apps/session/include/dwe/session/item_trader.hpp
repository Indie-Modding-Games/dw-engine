#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/session/map_head_lines.hpp>
#include <dwe/session/session_system.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::game::inventory {
class Inventory;
} // namespace dwe::game::inventory

namespace dwe::game::story {
class StoryState;
} // namespace dwe::game::story

namespace dwe::session {

class InventoryAdapter;
class StoryAdapter;

// The ruleset section of the item trader (rules/<id>/item_trader.yaml).
inline constexpr std::string_view ItemTraderSection = "item_trader";

// What the trader says, as line numbers of its MAPHEAD section (shop/script_menu.c:
// openMojyamonShop, tickMojyaTradeMenu).
struct ItemTraderLines {
  std::int32_t greeting = 0;
  std::int32_t ask = 0;
  std::int32_t farewell = 0;
  std::int32_t confirm = 0;
  std::int32_t choices = 0;
  std::int32_t thanks = 0;
  std::int32_t bagFull = 0;
};

// One trade of a table: the item the trader takes and the one it gives (MOJYAMON_ITEMS_GIVE and
// MOJYAMON_ITEMS_GET), by item id.
struct TradeOffer {
  std::string give;
  std::string get;
};

// A sound of the original's: sound `sound` of slot `slot` (sound.c: playSound).
struct TraderSound {
  std::int64_t slot = 0;
  std::int64_t sound = 0;
};

struct ItemTraderRules {
  // showMapHeadTextbox(..., 0x4d6): the MAPHEAD section of the trader's lines.
  std::int32_t linesSection = 0;
  ItemTraderLines lines;
  // pstat 249: the script names the trade table in it; the trader writes the item the player
  // offers there, and `noItem` while none is offered.
  std::int64_t tableVariable = 0;
  std::int64_t noItem = 0;
  // setMojyaItemTradedTrigger: trade i of the tables, counted across them, sets trigger
  // firstTraded + i; once all of them are set, allTraded too.
  std::int64_t firstTradedTrigger = 0;
  std::int64_t allTradedTrigger = 0;
  // tickSelectionDialogue: while this trigger is set, cancel does not answer the yes-or-no.
  std::int64_t choiceLockTrigger = 0;
  // tickMojyaTradeMenu: an item the player does not have.
  TraderSound refuseSound;
  std::vector<std::vector<TradeOffer>> tables;
};

core::Result<ItemTraderRules> readItemTraderRules(const ruleset::Section& section);

// A row of the trade menu (mojyaTradeFillItemList, calculateItemListStrings): what the trader
// takes and gives, whether the bag holds the first (the 0x80 flag; red otherwise) and whether this
// trade was made before (the mark before the row).
struct TradeRow {
  assets::ItemHandle give;
  assets::ItemHandle get;
  bool owned = false;
  bool traded = false;
};

enum class TradeOutcome : std::uint8_t {
  // giveItem took the new item: the offered one left the bag and the trade's trigger is set.
  Traded,
  // giveItem refused it (no free slot, or its stack is full): nothing changed.
  BagFull,
};

// The rows of trade table `table`; nothing for a table the rules do not have, and a trade whose
// items the item table does not know is left out.
[[nodiscard]] std::vector<TradeRow> tradeRows(const ItemTraderRules& rules, std::size_t table,
                                              const game::inventory::Inventory& bag,
                                              const assets::ItemTable& items,
                                              const game::story::StoryState& story);

// openMojyamonShop state 5 for row `row` of table `table`: the new item first; if the bag takes it,
// the offered one goes and setMojyaItemTradedTrigger marks the trade.
TradeOutcome makeTrade(const ItemTraderRules& rules, std::size_t table, std::size_t row,
                       game::inventory::Inventory& bag, const assets::ItemTable& items,
                       game::story::StoryState& story);

// Who trades: the NPC the routine's variable 254 names, by species.
struct ItemTraderVisit {
  std::string traderSpecies;
};

struct ItemTraderDesc {
  // The mounted packs, for script/maphead; they must outlive the trader.
  const assets::VirtualFileSystem* files = nullptr;
  // The bag and the story's triggers and variables; they must outlive the trader.
  InventoryAdapter* inventory = nullptr;
  StoryAdapter* story = nullptr;
};

// The item trader the story opens (script_interp.c: CALL_ROUTINE 2 -> shop/script_menu.c:
// openMojyamonShop): the script names one of the trade tables in variable 249; the player offers
// an item of the bag for the trader's. It keeps the visit open until the game's screen closes it;
// the story waits meanwhile.
class ItemTrader final : public SessionSystem {
public:
  explicit ItemTrader(ItemTraderDesc desc);
  ~ItemTrader() override;
  ItemTrader(const ItemTrader&) = delete;
  ItemTrader& operator=(const ItemTrader&) = delete;
  ItemTrader(ItemTrader&&) = delete;
  ItemTrader& operator=(ItemTrader&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // A ruleset without the section has no trader: the routine then closes at once.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> startNewGame(GameSession& session) override;

  // openMojyamonShop state 0: the table variable 249 names, then 249 says no item. False (closed)
  // without rules, bag or story, or for a table the rules do not have.
  bool open(ItemTraderVisit visit);
  void close();
  [[nodiscard]] const std::optional<ItemTraderVisit>& visit() const { return visit_; }
  [[nodiscard]] bool available() const { return rules_.has_value(); }
  [[nodiscard]] const ItemTraderRules& rules() const;
  [[nodiscard]] const assets::ItemTable& items() const;

  [[nodiscard]] std::vector<TradeRow> rows() const;
  // tickMojyaTradeMenu's confirm: an item the bag holds goes to variable 249 and the trader asks;
  // false for one it does not hold (the refusal sound).
  bool offer(std::size_t row);
  // The item offered, until the visit closes.
  [[nodiscard]] std::optional<assets::ItemHandle> offered() const;
  // The yes: makeTrade for the offered row.
  TradeOutcome trade();
  // Whether cancel answers no to the yes-or-no (the lock trigger is clear).
  [[nodiscard]] bool choiceCancellable() const;

  // The text key of a line of the trader (resolveMapHeadEntry); empty when MAPHEAD's listing does
  // not have it.
  [[nodiscard]] std::string lineKey(std::int32_t line) const;

private:
  ItemTraderDesc desc_;
  std::optional<ItemTraderRules> rules_;
  std::optional<ItemTraderVisit> visit_;
  std::size_t table_ = 0;
  std::optional<std::size_t> offeredRow_;
  std::optional<MapHeadLineTables> lineTables_;
};

} // namespace dwe::session
