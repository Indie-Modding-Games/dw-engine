#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/story/story_state.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/item_trader.hpp>
#include <dwe/session/story_adapter.hpp>

#include <format>
#include <limits>
#include <utility>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view LinesSectionKey = "lines_section";
constexpr std::string_view LinesKey = "lines";
constexpr std::string_view GreetingKey = "greeting";
constexpr std::string_view AskKey = "ask";
constexpr std::string_view FarewellKey = "farewell";
constexpr std::string_view ConfirmKey = "confirm";
constexpr std::string_view ChoicesKey = "choices";
constexpr std::string_view ThanksKey = "thanks";
constexpr std::string_view BagFullKey = "bag_full";
constexpr std::string_view TableVariableKey = "table_variable";
constexpr std::string_view NoItemKey = "no_item";
constexpr std::string_view FirstTradedKey = "first_traded_trigger";
constexpr std::string_view AllTradedKey = "all_traded_trigger";
constexpr std::string_view ChoiceLockKey = "choice_lock_trigger";
constexpr std::string_view RefuseSoundKey = "refuse_sound";
constexpr std::string_view SlotKey = "slot";
constexpr std::string_view SoundKey = "sound";
constexpr std::string_view TablesKey = "tables";
constexpr std::string_view GiveKey = "give";
constexpr std::string_view GetKey = "get";

constexpr std::int32_t ByteMost = 0xFF;
constexpr std::int32_t TriggerMost = 799;

std::int32_t inRange(YamlReader& reader, const YamlNode& parent, std::string_view key,
                     std::int32_t most = std::numeric_limits<std::int32_t>::max()) {
  const YamlNode& node = reader.field(parent, key);
  const auto value = reader.integer<std::int32_t>(node);
  if (reader.ok() && (value < 0 || value > most)) {
    reader.fail(node, std::format("{} must be from 0 to {}", key, most));
  }
  return value;
}

core::Result<void> checkItemTraderSection(const ruleset::Section& section) {
  if (auto read = readItemTraderRules(section); !read) {
    return std::unexpected{read.error()};
  }
  return {};
}

// The trade's place across the tables: ITEM_MENU_TYPE (the table times its length) plus the row.
std::size_t tradeNumber(const ItemTraderRules& rules, std::size_t table, std::size_t row) {
  std::size_t number = row;
  for (std::size_t i = 0; i < table && i < rules.tables.size(); ++i) {
    number += rules.tables[i].size();
  }
  return number;
}

bool triggerSet(const game::story::StoryState& story, std::int64_t trigger) {
  return story.trigger(static_cast<std::int32_t>(trigger)).value_or(false);
}

} // namespace

core::Result<ItemTraderRules> readItemTraderRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, ItemTraderSection,
                         {LinesSectionKey, LinesKey, TableVariableKey, NoItemKey, FirstTradedKey,
                          AllTradedKey, ChoiceLockKey, RefuseSoundKey, TablesKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  ItemTraderRules rules{.linesSection = inRange(reader, root, LinesSectionKey),
                        .lines = {},
                        .tableVariable = inRange(reader, root, TableVariableKey, ByteMost),
                        .noItem = inRange(reader, root, NoItemKey, ByteMost),
                        .firstTradedTrigger = inRange(reader, root, FirstTradedKey, TriggerMost),
                        .allTradedTrigger = inRange(reader, root, AllTradedKey, TriggerMost),
                        .choiceLockTrigger = inRange(reader, root, ChoiceLockKey, TriggerMost),
                        .refuseSound = {},
                        .tables = {}};
  const YamlNode& lines = reader.field(root, LinesKey);
  if (reader.expectKeys(
          lines, LinesKey,
          {GreetingKey, AskKey, FarewellKey, ConfirmKey, ChoicesKey, ThanksKey, BagFullKey})) {
    rules.lines = {.greeting = inRange(reader, lines, GreetingKey),
                   .ask = inRange(reader, lines, AskKey),
                   .farewell = inRange(reader, lines, FarewellKey),
                   .confirm = inRange(reader, lines, ConfirmKey),
                   .choices = inRange(reader, lines, ChoicesKey),
                   .thanks = inRange(reader, lines, ThanksKey),
                   .bagFull = inRange(reader, lines, BagFullKey)};
  }
  const YamlNode& sound = reader.field(root, RefuseSoundKey);
  if (reader.expectKeys(sound, RefuseSoundKey, {SlotKey, SoundKey})) {
    rules.refuseSound = {.slot = inRange(reader, sound, SlotKey),
                         .sound = inRange(reader, sound, SoundKey)};
  }
  std::size_t trades = 0;
  for (const YamlNode& table : reader.sequence(reader.field(root, TablesKey), TablesKey)) {
    std::vector<TradeOffer>& offers = rules.tables.emplace_back();
    for (const YamlNode& offer : reader.sequence(table, TablesKey)) {
      if (reader.expectKeys(offer, TablesKey, {GiveKey, GetKey})) {
        offers.push_back({.give = reader.text(reader.field(offer, GiveKey)),
                          .get = reader.text(reader.field(offer, GetKey))});
      }
    }
    trades += offers.size();
  }
  // Every trade has a trigger before the one that says all are made.
  if (reader.ok() &&
      rules.firstTradedTrigger + static_cast<std::int64_t>(trades) > rules.allTradedTrigger) {
    reader.fail(root, std::format("{} plus the {} trades must not pass {}", FirstTradedKey, trades,
                                  AllTradedKey));
  }
  return reader.finish(std::move(rules), section.file);
}

std::vector<TradeRow> tradeRows(const ItemTraderRules& rules, std::size_t table,
                                const game::inventory::Inventory& bag,
                                const assets::ItemTable& items,
                                const game::story::StoryState& story) {
  std::vector<TradeRow> rows;
  if (table >= rules.tables.size()) {
    return rows;
  }
  const std::vector<TradeOffer>& offers = rules.tables[table];
  for (std::size_t row = 0; row < offers.size(); ++row) {
    const auto give = items.handleOf(offers[row].give);
    const auto get = items.handleOf(offers[row].get);
    if (!give || !get) {
      core::logWarning("[port:trader] mojyaTradeFillItemList (shop/script_menu.c:1758): trade "
                       "'{}' for '{}' names an item the item table does not have",
                       offers[row].give, offers[row].get);
      continue;
    }
    // getItemCount: the bag's first stack of the item; calculateItemListStrings: the trade's
    // trigger draws the mark.
    const auto trigger =
        rules.firstTradedTrigger + static_cast<std::int64_t>(tradeNumber(rules, table, row));
    rows.push_back({.give = *give,
                    .get = *get,
                    .owned = bag.countOf(*give) != 0,
                    .traded = triggerSet(story, trigger)});
  }
  return rows;
}

TradeOutcome makeTrade(const ItemTraderRules& rules, std::size_t table, std::size_t row,
                       game::inventory::Inventory& bag, const assets::ItemTable& items,
                       game::story::StoryState& story) {
  const std::vector<TradeRow> rows = tradeRows(rules, table, bag, items, story);
  DWE_ASSERT(row < rows.size());
  const TradeRow& trade = rows[row];
  if (!bag.add(trade.get, 1).taken()) {
    return TradeOutcome::BagFull;
  }
  bag.remove(trade.give, 1);
  // setMojyaItemTradedTrigger: this trade's trigger, then the last one once every trade's is set.
  const auto first = rules.firstTradedTrigger;
  const auto mark = [&](std::int64_t trigger) {
    if (auto set = story.setTrigger(static_cast<std::int32_t>(trigger), true); !set) {
      core::logWarning("[port:trader] setMojyaItemTradedTrigger: {}", set.error().message);
    }
  };
  mark(first + static_cast<std::int64_t>(tradeNumber(rules, table, row)));
  for (std::int64_t trigger = first; trigger < rules.allTradedTrigger; ++trigger) {
    if (!triggerSet(story, trigger)) {
      return TradeOutcome::Traded;
    }
  }
  mark(rules.allTradedTrigger);
  return TradeOutcome::Traded;
}

ItemTrader::ItemTrader(ItemTraderDesc desc) : desc_(desc) {}

ItemTrader::~ItemTrader() = default;

ruleset::SectionChecks ItemTrader::sectionChecks() const {
  return {{std::string{ItemTraderSection}, checkItemTraderSection}};
}

core::Result<void> ItemTrader::readRules(const ruleset::Ruleset& rules) {
  const ruleset::Section* section = rules.section(ItemTraderSection);
  if (section == nullptr) {
    rules_.reset();
    return {};
  }
  auto read = readItemTraderRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = std::move(*read);
  return {};
}

core::Result<void> ItemTrader::startNewGame(GameSession& /*session*/) {
  visit_.reset();
  offeredRow_.reset();
  return {};
}

const ItemTraderRules& ItemTrader::rules() const {
  DWE_ASSERT(rules_.has_value());
  return *rules_;
}

const assets::ItemTable& ItemTrader::items() const {
  DWE_ASSERT(desc_.inventory != nullptr);
  return desc_.inventory->items();
}

bool ItemTrader::open(ItemTraderVisit visit) {
  if (!rules_ || desc_.inventory == nullptr || desc_.story == nullptr) {
    core::logDebug("[port:trader] openMojyamonShop (shop/script_menu.c:1051): no item_trader "
                   "section, bag or story; the routine closes at once");
    return false;
  }
  auto& state = desc_.story->state();
  const auto table = state.variable(static_cast<std::int32_t>(rules_->tableVariable));
  if (!table || *table >= rules_->tables.size()) {
    core::logWarning("[port:trader] openMojyamonShop (shop/script_menu.c:1051): variable {} names "
                     "no trade table; the routine closes at once",
                     rules_->tableVariable);
    return false;
  }
  // State 0: the table, then writePStat(249, 0xff).
  table_ = *table;
  if (auto set = state.setVariable(static_cast<std::int32_t>(rules_->tableVariable),
                                   static_cast<std::uint8_t>(rules_->noItem));
      !set) {
    core::logWarning("[port:trader] openMojyamonShop: {}", set.error().message);
  }
  if (!lineTables_ && desc_.files != nullptr) {
    if (auto tables = readMapHeadLineTables(*desc_.files); tables) {
      lineTables_ = std::move(*tables);
    } else {
      core::logWarning("[port:trader] resolveMapHeadEntry: {}", tables.error().message);
    }
  }
  offeredRow_.reset();
  visit_ = std::move(visit);
  core::logDebug("[port:trader] openMojyamonShop (shop/script_menu.c:1051): state 0, table {}, "
                 "trader '{}'",
                 table_, visit_->traderSpecies);
  return true;
}

void ItemTrader::close() {
  visit_.reset();
  offeredRow_.reset();
}

std::vector<TradeRow> ItemTrader::rows() const {
  if (!rules_ || !visit_) {
    return {};
  }
  return tradeRows(*rules_, table_, desc_.inventory->inventory(), items(), desc_.story->state());
}

bool ItemTrader::offer(std::size_t row) {
  const std::vector<TradeRow> shown = rows();
  if (row >= shown.size() || !shown[row].owned) {
    return false;
  }
  // tickMojyaTradeMenu: writePStat(249, item & 0x7f), for the "you'll really give me" line.
  const std::int32_t index = items().at(shown[row].give).index;
  if (index >= 0 && index < rules_->noItem) {
    if (auto set = desc_.story->state().setVariable(
            static_cast<std::int32_t>(rules_->tableVariable), static_cast<std::uint8_t>(index));
        !set) {
      core::logWarning("[port:trader] tickMojyaTradeMenu: {}", set.error().message);
    }
  }
  offeredRow_ = row;
  return true;
}

std::optional<assets::ItemHandle> ItemTrader::offered() const {
  const std::vector<TradeRow> shown = rows();
  if (!offeredRow_ || *offeredRow_ >= shown.size()) {
    return std::nullopt;
  }
  return shown[*offeredRow_].give;
}

TradeOutcome ItemTrader::trade() {
  DWE_ASSERT(offeredRow_.has_value());
  const TradeOutcome outcome = makeTrade(
      *rules_, table_, *offeredRow_, desc_.inventory->inventory(), items(), desc_.story->state());
  core::logDebug("[port:trader] openMojyamonShop (shop/script_menu.c:1088): state 5, row {}, {}",
                 *offeredRow_, outcome == TradeOutcome::Traded ? "traded" : "bag full");
  return outcome;
}

bool ItemTrader::choiceCancellable() const {
  return rules_ && desc_.story != nullptr &&
         !triggerSet(desc_.story->state(), rules_->choiceLockTrigger);
}

std::string ItemTrader::lineKey(std::int32_t line) const {
  if (!lineTables_ || !rules_) {
    return {};
  }
  const auto table = lineTables_->find(rules_->linesSection);
  const auto at = static_cast<std::size_t>(line);
  if (table == lineTables_->end() || line < 0 || at >= table->second.size()) {
    return {};
  }
  return table->second[at];
}

} // namespace dwe::session
