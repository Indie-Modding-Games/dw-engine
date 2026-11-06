#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/session/card_shop.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>

#include <algorithm>
#include <format>
#include <iterator>
#include <string>
#include <utility>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;
using game::inventory::SoldCard;

constexpr std::string_view StockKey = "stock";
constexpr std::string_view SizeKey = "size";
constexpr std::string_view FirstKey = "first";
constexpr std::string_view LastKey = "last";
constexpr std::string_view SkippedKey = "skipped";
constexpr std::string_view SellDivisorKey = "sell_divisor";
constexpr std::string_view LinesSectionKey = "lines_section";
constexpr std::string_view LinesKey = "lines";
constexpr std::string_view TodayKey = "today";
constexpr std::string_view SoldOutKey = "sold_out";
constexpr std::string_view SellPromptKey = "sell_prompt";
constexpr std::string_view ConfirmSectionKey = "confirm_section";
constexpr std::string_view ConfirmLineKey = "confirm_line";
constexpr std::string_view FailSoundKey = "fail_sound";
constexpr std::string_view TradeSoundKey = "trade_sound";
constexpr std::string_view MapKey = "map";
constexpr std::string_view SlotKey = "slot";
constexpr std::string_view SoundKey = "sound";
constexpr std::string_view PackPriceKey = "pack_price";
// The save section of the stock; a sold slot is saved as SoldCard.
constexpr std::string_view ShopSaveName = "card_shop";

std::int32_t nonNegative(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  const YamlNode& node = reader.field(parent, key);
  const auto value = reader.integer<std::int32_t>(node);
  if (reader.ok() && value < 0) {
    reader.fail(node, std::format("{} cannot be negative", key));
  }
  return value;
}

SoundCue soundCue(YamlReader& reader, const YamlNode& node) {
  return {.slot = nonNegative(reader, node, SlotKey), .sound = nonNegative(reader, node, SoundKey)};
}

core::Result<void> checkCardShopSection(const ruleset::Section& section) {
  if (auto read = readCardShopRules(section); !read) {
    return std::unexpected{read.error()};
  }
  return {};
}

} // namespace

core::Result<CardShopRules> readCardShopRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, CardShopSection,
                         {StockKey, SellDivisorKey, LinesSectionKey, LinesKey, ConfirmSectionKey,
                          ConfirmLineKey, FailSoundKey, TradeSoundKey},
                         {PackPriceKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  CardShopRules rules{.stock = {},
                      .sellDivisor = nonNegative(reader, root, SellDivisorKey),
                      .linesSection = nonNegative(reader, root, LinesSectionKey),
                      .lines = {},
                      .confirmSection = nonNegative(reader, root, ConfirmSectionKey),
                      .confirmLine = nonNegative(reader, root, ConfirmLineKey),
                      .failSound = {},
                      .tradeMap = {},
                      .tradeSound = {},
                      .packPrice = 0};
  const YamlNode& fail = reader.field(root, FailSoundKey);
  if (reader.expectKeys(fail, FailSoundKey, {SlotKey, SoundKey})) {
    rules.failSound = soundCue(reader, fail);
  }
  const YamlNode& trade = reader.field(root, TradeSoundKey);
  if (reader.expectKeys(trade, TradeSoundKey, {MapKey, SlotKey, SoundKey})) {
    rules.tradeMap = reader.text(reader.field(trade, MapKey));
    rules.tradeSound = soundCue(reader, trade);
  }
  const YamlNode& stock = reader.field(root, StockKey);
  if (reader.expectKeys(stock, StockKey, {SizeKey, FirstKey, LastKey, SkippedKey})) {
    rules.stock = {.size = nonNegative(reader, stock, SizeKey),
                   .first = nonNegative(reader, stock, FirstKey),
                   .last = nonNegative(reader, stock, LastKey),
                   .skipped = {}};
    for (const YamlNode& card : reader.sequence(reader.field(stock, SkippedKey), SkippedKey)) {
      rules.stock.skipped.push_back(reader.integer<std::int32_t>(card));
    }
  }
  const YamlNode& lines = reader.field(root, LinesKey);
  if (reader.expectKeys(lines, LinesKey, {TodayKey, SoldOutKey, SellPromptKey})) {
    rules.lines = {.today = nonNegative(reader, lines, TodayKey),
                   .soldOut = nonNegative(reader, lines, SoldOutKey),
                   .sellPrompt = nonNegative(reader, lines, SellPromptKey)};
  }
  if (const auto price = reader.optionalInteger<std::int32_t>(root, PackPriceKey)) {
    rules.packPrice = *price;
    if (reader.ok() && *price < 0) {
      reader.fail(reader.field(root, PackPriceKey), "pack_price cannot be negative");
    }
  }
  if (reader.ok() && rules.sellDivisor == 0) {
    reader.fail(reader.field(root, SellDivisorKey), "sell_divisor cannot be 0");
  }
  return reader.finish(std::move(rules), section.file);
}

class CardShop::Serializer final : public game::save::SectionSerializer {
public:
  explicit Serializer(CardShop& shop) : shop_(&shop) {}

  [[nodiscard]] core::Result<YamlNode> write() const override {
    YamlNode stock = YamlNode::sequence();
    for (const std::int32_t card : shop_->stock_) {
      stock.append(YamlNode::scalar(std::to_string(card)));
    }
    YamlNode root = YamlNode::mapping();
    root.set(std::string{StockKey}, std::move(stock));
    return root;
  }

  core::Result<void> read(const YamlNode& state) override {
    YamlReader reader;
    std::vector<std::int32_t> stock;
    if (reader.expectKeys(state, ShopSaveName, {StockKey})) {
      for (const YamlNode& card : reader.sequence(reader.field(state, StockKey), StockKey)) {
        stock.push_back(reader.integer<std::int32_t>(card));
      }
    }
    auto read = reader.finish(std::move(stock), std::string{ShopSaveName});
    if (!read) {
      return std::unexpected{read.error()};
    }
    shop_->stock_ = std::move(*read);
    shop_->rollAfterLoad_ = false;
    return {};
  }

  core::Result<void> readMissing() override {
    shop_->stock_.clear();
    shop_->rollAfterLoad_ = true;
    return {};
  }

private:
  CardShop* shop_;
};

CardShop::CardShop(CardShopDesc desc)
    : desc_(desc), serializer_(std::make_unique<Serializer>(*this)) {}

CardShop::~CardShop() = default;

ruleset::SectionChecks CardShop::sectionChecks() const {
  return {{std::string{CardShopSection}, checkCardShopSection}};
}

core::Result<void> CardShop::readRules(const ruleset::Ruleset& rules) {
  const ruleset::Section* section = rules.section(CardShopSection);
  if (section == nullptr) {
    rules_.reset();
    return {};
  }
  auto read = readCardShopRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = std::move(*read);
  return {};
}

core::Result<void> CardShop::addSaveSections(game::save::SaveSections& sections) {
  return sections.add(std::string{ShopSaveName}, *serializer_, game::save::MigrationChain{});
}

core::Result<void> CardShop::startNewGame(GameSession& session) {
  session_ = &session;
  visit_.reset();
  stock_.clear();
  rollAfterLoad_ = false;
  rollStock();
  return {};
}

void CardShop::tick(GameSession& session, const ruleset::ClockAdvance& /*advance*/) {
  session_ = &session;
}

void CardShop::afterLoad(GameSession& session) {
  session_ = &session;
  if (std::exchange(rollAfterLoad_, false)) {
    rollStock();
  }
}

void CardShop::startNewDays(std::int32_t days) {
  for (std::int32_t day = 0; day < days; ++day) {
    rollStock();
  }
}

void CardShop::rollStock() {
  if (!rules_ || session_ == nullptr) {
    return;
  }
  stock_ = game::inventory::rollCardStock(rules_->stock, session_->random());
}

bool CardShop::available() const {
  return rules_.has_value() && desc_.cards != nullptr && desc_.cards->collection() != nullptr &&
         desc_.inventory != nullptr;
}

bool CardShop::open(CardShopKind kind) {
  if (!available()) {
    core::logDebug("[port:cards] call_routine 14 (script_interp.c:436): no card_shop section or "
                   "no cards; the visit closes at once");
    return false;
  }
  if (!lineTables_ && desc_.files != nullptr) {
    if (auto tables = readMapHeadLineTables(*desc_.files); tables) {
      lineTables_ = std::move(*tables);
    } else {
      core::logWarning("[port:cards] resolveMapHeadEntry: {}", tables.error().message);
    }
  }
  visit_ = kind;
  return true;
}

void CardShop::close() {
  visit_.reset();
}

const CardShopRules& CardShop::rules() const {
  DWE_ASSERT(rules_.has_value());
  return *rules_;
}

const game::inventory::CardCollection& CardShop::cards() const {
  DWE_ASSERT(available());
  return *desc_.cards->collection();
}

std::int32_t CardShop::packDraws() const {
  return desc_.cards != nullptr ? desc_.cards->packDraws() : 0;
}

std::optional<PackCard> CardShop::drawPackCard() {
  if (desc_.cards == nullptr || session_ == nullptr) {
    return std::nullopt;
  }
  return desc_.cards->drawPackCard(session_->random());
}

std::int32_t CardShop::packLimit() const {
  if (!rules_ || rules_->packPrice <= 0) {
    return 1;
  }
  return 1 + (money() / rules_->packPrice);
}

std::vector<PackCard> CardShop::openPacks(std::int32_t packs) {
  std::vector<PackCard> drawn;
  const std::int32_t count = std::clamp(packs, 1, packLimit());
  for (std::int32_t pack = 0; pack < count; ++pack) {
    // Script 126: reduce_money(500) for every pack but the first, which it paid before routine 14.
    if (pack > 0) {
      desc_.inventory->inventory().spendMoney(rules_->packPrice);
    }
    // rollCardPack: the pack's draws, one after the other (a draw with no card gives none).
    for (std::int32_t draw = 0; draw < packDraws(); ++draw) {
      if (std::optional<PackCard> card = drawPackCard()) {
        drawn.push_back(*card);
      }
    }
  }
  core::logDebug("[port:cards] bulk purchase: {} packs, {} cards, {} bits left", count,
                 drawn.size(), money());
  return drawn;
}

std::vector<PackSummaryRow> summarizePacks(const std::vector<PackCard>& drawn) {
  std::vector<PackSummaryRow> rows;
  for (const PackCard& card : drawn) {
    auto row = std::ranges::find(rows, card.card, &PackSummaryRow::card);
    if (row == rows.end()) {
      rows.push_back({.card = card.card, .count = 0, .isNew = card.copiesBefore == 0});
      row = std::prev(rows.end());
    }
    ++row->count;
  }
  std::ranges::sort(rows, {}, &PackSummaryRow::card);
  return rows;
}

std::vector<game::inventory::CardOffer> CardShop::offers() const {
  if (!available()) {
    return {};
  }
  return game::inventory::cardOffers(stock_, cards(), money());
}

bool CardShop::buy(std::size_t slot) {
  const std::vector<game::inventory::CardOffer> shown = offers();
  if (slot >= shown.size() || !shown[slot].buyable) {
    return false;
  }
  const game::inventory::CardOffer& offer = shown[slot];
  if (!desc_.cards->collection()->giveCopy(offer.card)) {
    return false;
  }
  desc_.inventory->inventory().spendMoney(offer.price);
  stock_[slot] = SoldCard;
  core::logDebug("[port:cards] tickSingleCardShop (script_common.c:1101): slot {} card {} bought "
                 "for {}, {} left",
                 slot, offer.card, offer.price, money());
  return true;
}

std::vector<std::optional<game::inventory::CardSaleRow>> CardShop::saleRows() const {
  if (!available()) {
    return {};
  }
  return game::inventory::cardSaleRows(cards());
}

std::optional<std::int32_t> CardShop::sellPrice(std::int32_t card) const {
  if (!available()) {
    return std::nullopt;
  }
  return game::inventory::cardSellPrice(cards(), card, rules_->sellDivisor);
}

bool CardShop::sell(std::int32_t card, std::int32_t count) {
  const std::optional<std::int32_t> price = sellPrice(card);
  if (!price || !desc_.cards->collection()->sellCopies(card, count)) {
    return false;
  }
  // tickItemMenuAmountBox: the money stops at the most (0xF423F).
  desc_.inventory->inventory().addMoney(*price * count);
  return true;
}

void CardShop::playFailSound() {
  if (rules_ && session_ != nullptr && session_->sound() != nullptr) {
    session_->sound()->playSound(rules_->failSound.slot, rules_->failSound.sound);
  }
}

void CardShop::playTradeSound(std::string_view mapId) {
  if (!rules_ || session_ == nullptr || session_->sound() == nullptr || mapId != rules_->tradeMap) {
    return;
  }
  session_->sound()->playSound(rules_->tradeSound.slot, rules_->tradeSound.sound);
}

std::int32_t CardShop::money() const {
  return desc_.inventory != nullptr ? desc_.inventory->inventory().money() : 0;
}

std::int32_t CardShop::maxMoney() const {
  return desc_.inventory != nullptr ? desc_.inventory->inventory().rules().maxMoney : 0;
}

std::string CardShop::keyOf(std::int32_t section, std::int32_t line) const {
  if (!lineTables_ || line < 0) {
    return {};
  }
  const auto table = lineTables_->find(section);
  if (table == lineTables_->end() || static_cast<std::size_t>(line) >= table->second.size()) {
    return {};
  }
  return table->second[static_cast<std::size_t>(line)];
}

std::string CardShop::lineKey(std::int32_t line) const {
  return rules_ ? keyOf(rules_->linesSection, line) : std::string{};
}

std::string CardShop::confirmKey() const {
  return rules_ ? keyOf(rules_->confirmSection, rules_->confirmLine) : std::string{};
}

} // namespace dwe::session
