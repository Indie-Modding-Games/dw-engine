#include <dwe/assets/shop_table.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/partner/partner.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/shop_counter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

#include "map_head_lines.hpp"
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
                          AnythingElseKey, DiscountKey})) {
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
          .discount = nonNegative(reader, node, DiscountKey)};
}

} // namespace

core::Result<ShopCounterRules> readShopCounterRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, messages::ShopSectionText,
                         {KeeperLinesSectionKey, OwnerlessLinesSectionKey, RecycleTriggerKey,
                          AmountStepKey, LinesKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  ShopCounterRules rules{.keeperLinesSection = nonNegative(reader, root, KeeperLinesSectionKey),
                         .ownerlessLinesSection =
                             nonNegative(reader, root, OwnerlessLinesSectionKey),
                         .recycleTrigger = nonNegative(reader, root, RecycleTriggerKey),
                         .amountStep = nonNegative(reader, root, AmountStepKey),
                         .lines = readLines(reader, reader.field(root, LinesKey))};
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

ShopCounter::ShopCounter(ShopCounterDesc desc)
    : desc_(desc), stockPort_(std::make_unique<StockPort>(desc.story)) {}

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

void ShopCounter::open(ShopVisit visit) {
  if (!rules_ || desc_.inventory == nullptr) {
    core::logDebug("[port:shop] openShop (script_common.c:1618): the ruleset has no shop section; "
                   "the visit closes at once");
    return;
  }
  if (recycles()) {
    core::logDebug("[port:shop] openRecycleShop (script_interp.c:420, CALL_ROUTINE 8 with trigger "
                   "{}): not ported yet; the visit closes at once",
                   rules_->recycleTrigger);
    return;
  }
  if (auto loaded = loadLines(); !loaded) {
    core::logWarning("[port:shop] getShopkeeperLine (script_common.c:1174): {}",
                     loaded.error().message);
  }
  visit_ = std::move(visit);
  core::logDebug("[port:shop] openShop (script_common.c:1618): keeper '{}'{}, lines section {}",
                 visit_->keeperSpecies, visit_->ownerless ? " (ownerless)" : "", linesSection());
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
  return game::inventory::shopStock(desc_.inventory->inventory(), items(), *stockPort_);
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
  return game::inventory::maxPurchase(desc_.inventory->inventory(), items(), item, buyer);
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
  auto bought = desc_.inventory->buy(session, item, amount, *stockPort_);
  if (bought) {
    core::logDebug("[port:shop] tickItemMenuAmountBox (script_common.c:1457): bought {} x{} for "
                   "{}, saved {}",
                   items().at(item).id, amount, bought->paid, bought->saved);
  }
  return bought;
}

core::Result<std::int32_t> ShopCounter::sell(GameSession& session, assets::ItemHandle item,
                                             std::int32_t amount) {
  if (!visit_) {
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
  if (!lineTables_ || line < 0) {
    return {};
  }
  const auto table = lineTables_->find(linesSection());
  if (table == lineTables_->end() || static_cast<std::size_t>(line) >= table->second.size()) {
    return {};
  }
  return table->second[static_cast<std::size_t>(line)];
}

std::int32_t ShopCounter::linesSection() const {
  if (!rules_ || !visit_) {
    return 0;
  }
  if (visit_->ownerless) {
    return rules_->ownerlessLinesSection;
  }
  // getShopkeeperLine: a species data/shops does not list talks with the first keeper's lines.
  const auto keeper = std::ranges::find(keepers_, visit_->keeperSpecies);
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
