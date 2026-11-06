#include <dwe/assets/shop_table.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/partner/partner.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/map_head_lines.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/shop_counter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <utility>

#include "shop_messages.hpp"

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;
using messages::formatMessage;

constexpr std::string_view KeeperLinesSectionKey = "keeper_lines_section";
constexpr std::string_view OwnerlessLinesSectionKey = "ownerless_lines_section";
constexpr std::string_view RecycleTriggerKey = "recycle_trigger";
constexpr std::string_view AmountStepKey = "amount_step";
constexpr std::string_view LinesKey = "lines";
constexpr std::string_view GreetingKey = "greeting";
constexpr std::string_view NothingToSellKey = "nothing_to_sell";
constexpr std::string_view ChoicesKey = "choices";
constexpr std::string_view FarewellAfterTradeKey = "farewell_after_trade";
constexpr std::string_view FarewellKey = "farewell";
constexpr std::string_view BitsKey = "bits";
constexpr std::string_view BuyPromptKey = "buy_prompt";
constexpr std::string_view SellPromptKey = "sell_prompt";
constexpr std::string_view ThanksKey = "thanks";
constexpr std::string_view AnythingElseKey = "anything_else";
constexpr std::string_view DiscountKey = "discount";
constexpr std::string_view RecycleChoicesKey = "recycle_choices";
constexpr std::string_view SoldOutKey = "sold_out";
constexpr std::string_view RecyclableKey = "recyclable";
constexpr std::string_view RecycleMaxKey = "recycle_max";
// The save section of the recycle shop's stock.
constexpr std::string_view ShopSaveName = "shop";
constexpr std::string_view RecycledKey = "recycled";
constexpr std::string_view CountKey = "count";
constexpr std::string_view ItemKey = "item";

std::int32_t nonNegative(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  const YamlNode& node = reader.field(parent, key);
  const auto value = reader.integer<std::int32_t>(node);
  if (reader.ok() && value < 0) {
    reader.fail(node, formatMessage(messages::NegativeValue, key));
  }
  return value;
}

ShopLines readLines(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, messages::ShopLinesText,
                         {GreetingKey, NothingToSellKey, ChoicesKey, FarewellAfterTradeKey,
                          FarewellKey, BitsKey, BuyPromptKey, SellPromptKey, ThanksKey,
                          AnythingElseKey, DiscountKey, RecycleChoicesKey, SoldOutKey})) {
    return {};
  }
  return {.greeting = nonNegative(reader, node, GreetingKey),
          .nothingToSell = nonNegative(reader, node, NothingToSellKey),
          .choices = nonNegative(reader, node, ChoicesKey),
          .farewellAfterTrade = nonNegative(reader, node, FarewellAfterTradeKey),
          .farewell = nonNegative(reader, node, FarewellKey),
          .bits = nonNegative(reader, node, BitsKey),
          .buyPrompt = nonNegative(reader, node, BuyPromptKey),
          .sellPrompt = nonNegative(reader, node, SellPromptKey),
          .thanks = nonNegative(reader, node, ThanksKey),
          .anythingElse = nonNegative(reader, node, AnythingElseKey),
          .discount = nonNegative(reader, node, DiscountKey),
          .recycleChoices = nonNegative(reader, node, RecycleChoicesKey),
          .soldOut = nonNegative(reader, node, SoldOutKey)};
}

} // namespace

core::Result<ShopCounterRules> readShopCounterRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, messages::ShopSectionText,
                         {KeeperLinesSectionKey, OwnerlessLinesSectionKey, RecycleTriggerKey,
                          RecyclableKey, RecycleMaxKey, AmountStepKey, LinesKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  ShopCounterRules rules{.keeperLinesSection = nonNegative(reader, root, KeeperLinesSectionKey),
                         .ownerlessLinesSection =
                             nonNegative(reader, root, OwnerlessLinesSectionKey),
                         .recycleTrigger = nonNegative(reader, root, RecycleTriggerKey),
                         .recyclable = {},
                         .recycleMax = nonNegative(reader, root, RecycleMaxKey),
                         .amountStep = nonNegative(reader, root, AmountStepKey),
                         .lines = readLines(reader, reader.field(root, LinesKey))};
  assets::readEntryList(reader, reader.field(root, RecyclableKey), RecyclableKey, rules.recyclable);
  return reader.finish(std::move(rules), section.file);
}

core::Result<void> checkShopSection(const ruleset::Section& section) {
  if (auto read = readShopCounterRules(section); !read) {
    return std::unexpected{read.error()};
  }
  return {};
}

// The story's triggers as the stock of the shop (shopFillBuyItemList).
class ShopCounter::StockPort final : public game::inventory::StockTriggers {
public:
  explicit StockPort(const StoryAdapter* story) : story_(story) {}

  [[nodiscard]] bool isSet(std::int32_t trigger) const override {
    if (story_ == nullptr) {
      return false;
    }
    const auto set = story_->state().trigger(trigger);
    return set.value_or(false);
  }

private:
  const StoryAdapter* story_;
};

// The recycle shop's items as stock triggers: an item is in stock while the shop holds some
// (fillRecycleShopItemList).
class ShopCounter::RecyclePort final : public game::inventory::StockTriggers {
public:
  explicit RecyclePort(const ShopCounter& counter) : counter_(&counter) {}

  [[nodiscard]] bool isSet(std::int32_t trigger) const override {
    const auto& items = counter_->items();
    const std::int32_t base = counter_->desc_.inventory->inventory().rules().shop.stockTriggerBase;
    const assets::ItemEntry* item = items.findByOriginalIndex(trigger - base);
    return item != nullptr && counter_->recycleCount(items.handleOf(*item)) > 0;
  }

private:
  const ShopCounter* counter_;
};

class ShopCounter::Serializer final : public game::save::SectionSerializer {
public:
  explicit Serializer(ShopCounter& counter) : counter_(&counter) {}

  [[nodiscard]] core::Result<YamlNode> write() const override {
    YamlNode recycled = YamlNode::sequence();
    for (const auto& [item, count] : counter_->recycled_) {
      YamlNode entry = YamlNode::mapping();
      entry.set(std::string{ItemKey}, YamlNode::scalar(item));
      entry.set(std::string{CountKey}, YamlNode::scalar(std::to_string(count)));
      recycled.append(std::move(entry));
    }
    YamlNode root = YamlNode::mapping();
    root.set(std::string{RecycledKey}, std::move(recycled));
    return root;
  }

  core::Result<void> read(const YamlNode& state) override {
    YamlReader reader;
    std::map<std::string, std::int32_t, std::less<>> recycled;
    if (reader.expectKeys(state, ShopSaveName, {RecycledKey})) {
      for (const YamlNode& entry : reader.sequence(reader.field(state, RecycledKey), RecycledKey)) {
        if (reader.expectKeys(entry, RecycledKey, {ItemKey, CountKey})) {
          const std::string item = reader.text(reader.field(entry, ItemKey));
          recycled[item] = nonNegative(reader, entry, CountKey);
        }
      }
    }
    auto read = reader.finish(std::move(recycled), std::string{ShopSaveName});
    if (!read) {
      return std::unexpected{read.error()};
    }
    counter_->recycled_ = std::move(*read);
    return {};
  }

  core::Result<void> readMissing() override {
    counter_->recycled_.clear();
    return {};
  }

private:
  ShopCounter* counter_;
};

ShopCounter::ShopCounter(ShopCounterDesc desc)
    : desc_(desc), stockPort_(std::make_unique<StockPort>(desc.story)),
      recyclePort_(std::make_unique<RecyclePort>(*this)),
      serializer_(std::make_unique<Serializer>(*this)) {}

ShopCounter::~ShopCounter() = default;

ruleset::SectionChecks ShopCounter::sectionChecks() const {
  return {{std::string{ShopSection}, checkShopSection}};
}

core::Result<void> ShopCounter::readRules(const ruleset::Ruleset& rules) {
  const ruleset::Section* section = rules.section(ShopSection);
  if (section == nullptr) {
    rules_.reset();
    return {};
  }
  auto read = readShopCounterRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = std::move(*read);
  return {};
}

core::Result<void> ShopCounter::addSaveSections(game::save::SaveSections& sections) {
  return sections.add(std::string{ShopSaveName}, *serializer_, game::save::MigrationChain{});
}

core::Result<void> ShopCounter::startNewGame(GameSession& /*session*/) {
  recycled_.clear();
  return {};
}

std::int32_t ShopCounter::recycleCount(assets::ItemHandle item) const {
  const auto found = recycled_.find(items().at(item).id);
  return found == recycled_.end() ? 0 : found->second;
}

void ShopCounter::recycleLost(const std::vector<game::inventory::Stack>& lost) {
  if (!rules_) {
    return;
  }
  for (const game::inventory::Stack& stack : lost) {
    const assets::ItemEntry& item = items().at(stack.item);
    if (!rules_->recyclable.contains(item)) {
      continue;
    }
    std::int32_t& count = recycled_[item.id];
    count = std::min(count + stack.count, rules_->recycleMax);
    core::logDebug("[port:shop] handleItemLoss (script_common.c:1372): {} x{} to the recycle "
                   "shop, now {}",
                   item.id, stack.count, count);
  }
}

void ShopCounter::open(ShopVisit visit) {
  if (!rules_ || desc_.inventory == nullptr) {
    core::logDebug("[port:shop] openShop (script_common.c:1618): the ruleset has no shop section; "
                   "the visit closes at once");
    return;
  }
  visit.recycle = visit.recycle || recycles();
  if (auto loaded = loadLines(); !loaded) {
    core::logWarning("[port:shop] getShopkeeperLine (script_common.c:1174): {}",
                     loaded.error().message);
  }
  visit_ = std::move(visit);
  core::logDebug(
      "[port:shop] {}: keeper '{}'{}, lines section {}",
      visit_->recycle ? "openRecycleShop (script_menu.c:584)" : "openShop (script_common.c:1618)",
      visit_->keeperSpecies, visit_->ownerless ? " (ownerless)" : "", linesSection(*visit_));
}

void ShopCounter::close() {
  if (visit_) {
    core::logDebug("[port:shop] openShop (script_common.c:1618): visit closed");
  }
  visit_.reset();
}

const ShopCounterRules& ShopCounter::rules() const {
  DWE_ASSERT(rules_.has_value());
  return *rules_;
}

bool ShopCounter::recycles() const {
  return rules_ && stockPort_->isSet(rules_->recycleTrigger);
}

std::vector<game::inventory::ShopOffer> ShopCounter::stock() const {
  return game::inventory::shopStock(
      desc_.inventory->inventory(), items(),
      recycling() ? static_cast<const game::inventory::StockTriggers&>(*recyclePort_)
                  : *stockPort_);
}

std::vector<game::inventory::SaleOffer> ShopCounter::saleOffers() const {
  return game::inventory::saleOffers(desc_.inventory->inventory(), items());
}

std::int32_t ShopCounter::shownBuyPrice(assets::ItemHandle item) const {
  return game::inventory::buyPrice(desc_.inventory->inventory(), items().at(item),
                                   game::inventory::Buyer{});
}

std::int32_t ShopCounter::sellPrice(assets::ItemHandle item) const {
  return game::inventory::sellPrice(desc_.inventory->inventory(), items().at(item));
}

std::int32_t ShopCounter::maxPurchase(GameSession& session, assets::ItemHandle item) const {
  const auto level = static_cast<std::int32_t>(session.partner().species().level);
  const game::inventory::Buyer buyer{
      .partnerIsBaby =
          std::ranges::contains(desc_.inventory->itemUseRules().shopDiscountLevels, level)};
  const std::int32_t most =
      game::inventory::maxPurchase(desc_.inventory->inventory(), items(), item, buyer);
  // createItemMenuAmountBox: the recycle shop sells no more than it holds.
  return recycling() ? std::min(most, recycleCount(item)) : most;
}

std::int32_t ShopCounter::money() const {
  return desc_.inventory->inventory().money();
}

std::int32_t ShopCounter::maxMoney() const {
  return desc_.inventory->inventory().rules().maxMoney;
}

const assets::ItemTable& ShopCounter::items() const {
  return desc_.inventory->items();
}

core::Result<game::inventory::Purchase>
ShopCounter::buy(GameSession& session, assets::ItemHandle item, std::int32_t amount) {
  if (!visit_) {
    return core::fail(std::string{messages::ShopClosed});
  }
  if (recycling() && amount > recycleCount(item)) {
    return core::fail(messages::formatMessage(messages::ShopAmountOutside, amount,
                                              items().at(item).id, recycleCount(item)));
  }
  auto bought = desc_.inventory->buy(
      session, item, amount,
      recycling() ? static_cast<const game::inventory::StockTriggers&>(*recyclePort_)
                  : *stockPort_);
  if (bought && recycling()) {
    // tickItemMenuAmountBox: the recycle shop gives up what it sold.
    recycled_[items().at(item).id] -= amount;
  }
  if (bought) {
    core::logDebug("[port:shop] tickItemMenuAmountBox (script_common.c:1457): bought {} x{} for "
                   "{}, saved {}",
                   items().at(item).id, amount, bought->paid, bought->saved);
  }
  return bought;
}

core::Result<std::int32_t> ShopCounter::sell(GameSession& session, assets::ItemHandle item,
                                             std::int32_t amount) {
  if (!visit_ || visit_->recycle) {
    return core::fail(std::string{messages::ShopClosed});
  }
  auto sold = game::inventory::sell(desc_.inventory->inventory(), items(), item, amount);
  if (sold) {
    session.afterSystemCommand();
    core::logDebug("[port:shop] tickItemMenuAmountBox (script_common.c:1457): sold {} x{} for {}",
                   items().at(item).id, amount, *sold);
  }
  return sold;
}

std::string ShopCounter::lineKey(std::int32_t line) const {
  if (!lineTables_ || !visit_ || line < 0) {
    return {};
  }
  const auto table = lineTables_->find(linesSection(*visit_));
  if (table == lineTables_->end() || static_cast<std::size_t>(line) >= table->second.size()) {
    return {};
  }
  return table->second[static_cast<std::size_t>(line)];
}

std::string ShopCounter::keeperLineKey(const ShopVisit& visit, std::int32_t line) {
  if (!rules_ || line < 0) {
    return {};
  }
  if (auto loaded = loadLines(); !loaded) {
    core::logWarning("[port:shop] getShopkeeperLine (script_common.c:1174): {}",
                     loaded.error().message);
    return {};
  }
  if (!lineTables_) {
    return {};
  }
  const auto table = lineTables_->find(linesSection(visit));
  if (table == lineTables_->end() || static_cast<std::size_t>(line) >= table->second.size()) {
    return {};
  }
  return table->second[static_cast<std::size_t>(line)];
}

std::int32_t ShopCounter::linesSection(const ShopVisit& visit) const {
  if (!rules_) {
    return 0;
  }
  if (visit.ownerless) {
    return rules_->ownerlessLinesSection;
  }
  // getShopkeeperLine: a species data/shops does not list talks with the first keeper's lines.
  const auto keeper = std::ranges::find(keepers_, visit.keeperSpecies);
  const auto place = keeper == keepers_.end() ? 0 : std::distance(keepers_.begin(), keeper);
  return rules_->keeperLinesSection + static_cast<std::int32_t>(place);
}

core::Result<void> ShopCounter::loadLines() {
  if (lineTables_ || desc_.files == nullptr) {
    return {};
  }
  auto shops = assets::loadShopTable(*desc_.files);
  if (!shops) {
    return std::unexpected{shops.error()};
  }
  auto tables = readMapHeadLineTables(*desc_.files);
  if (!tables) {
    return std::unexpected{tables.error()};
  }
  keepers_ = std::move(shops->shopkeepers);
  lineTables_ = std::move(*tables);
  return {};
}

} // namespace dwe::session
