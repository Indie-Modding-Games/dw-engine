#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/story/story_state.hpp>
#include <dwe/session/cards_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/merit_shop.hpp>
#include <dwe/session/story_adapter.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view LinesSectionKey = "lines_section";
constexpr std::string_view LinesKey = "lines";
constexpr std::string_view GreetingKey = "greeting";
constexpr std::string_view ChoicesKey = "choices";
constexpr std::string_view GiveCardKey = "give_card";
constexpr std::string_view TakeItemKey = "take_item";
constexpr std::string_view CardPromptKey = "card_prompt";
constexpr std::string_view ItemPromptKey = "item_prompt";
constexpr std::string_view CardWorthKey = "card_worth";
constexpr std::string_view PointsSectionKey = "points_section";
constexpr std::string_view PointsLineKey = "points_line";
constexpr std::string_view MostMeritKey = "most_merit";
constexpr std::string_view ChoiceLockTriggerKey = "choice_lock_trigger";
constexpr std::string_view FailSoundKey = "fail_sound";
constexpr std::string_view TradeSoundKey = "trade_sound";
constexpr std::string_view MapKey = "map";
constexpr std::string_view SlotKey = "slot";
constexpr std::string_view SoundKey = "sound";

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

core::Result<void> checkMeritShopSection(const ruleset::Section& section) {
  if (auto read = readMeritShopRules(section); !read) {
    return std::unexpected{read.error()};
  }
  return {};
}

} // namespace

core::Result<MeritShopRules> readMeritShopRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, MeritShopSection,
                         {LinesSectionKey, LinesKey, PointsSectionKey, PointsLineKey, MostMeritKey,
                          ChoiceLockTriggerKey, FailSoundKey, TradeSoundKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  MeritShopRules rules{.linesSection = nonNegative(reader, root, LinesSectionKey),
                       .lines = {},
                       .pointsSection = nonNegative(reader, root, PointsSectionKey),
                       .pointsLine = nonNegative(reader, root, PointsLineKey),
                       .mostMerit = nonNegative(reader, root, MostMeritKey),
                       .choiceLockTrigger = nonNegative(reader, root, ChoiceLockTriggerKey),
                       .failSound = {},
                       .tradeMap = {},
                       .tradeSound = {}};
  const YamlNode& lines = reader.field(root, LinesKey);
  if (reader.expectKeys(lines, LinesKey,
                        {GreetingKey, ChoicesKey, GiveCardKey, TakeItemKey, CardPromptKey,
                         ItemPromptKey, CardWorthKey})) {
    rules.lines = {.greeting = nonNegative(reader, lines, GreetingKey),
                   .choices = nonNegative(reader, lines, ChoicesKey),
                   .giveCard = nonNegative(reader, lines, GiveCardKey),
                   .takeItem = nonNegative(reader, lines, TakeItemKey),
                   .cardPrompt = nonNegative(reader, lines, CardPromptKey),
                   .itemPrompt = nonNegative(reader, lines, ItemPromptKey),
                   .cardWorth = nonNegative(reader, lines, CardWorthKey)};
  }
  const YamlNode& fail = reader.field(root, FailSoundKey);
  if (reader.expectKeys(fail, FailSoundKey, {SlotKey, SoundKey})) {
    rules.failSound = soundCue(reader, fail);
  }
  const YamlNode& trade = reader.field(root, TradeSoundKey);
  if (reader.expectKeys(trade, TradeSoundKey, {MapKey, SlotKey, SoundKey})) {
    rules.tradeMap = reader.text(reader.field(trade, MapKey));
    rules.tradeSound = soundCue(reader, trade);
  }
  return reader.finish(std::move(rules), section.file);
}

MeritShop::MeritShop(MeritShopDesc desc) : desc_(desc) {}

ruleset::SectionChecks MeritShop::sectionChecks() const {
  return {{std::string{MeritShopSection}, checkMeritShopSection}};
}

core::Result<void> MeritShop::readRules(const ruleset::Ruleset& rules) {
  const ruleset::Section* section = rules.section(MeritShopSection);
  if (section == nullptr) {
    rules_.reset();
    return {};
  }
  auto read = readMeritShopRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = std::move(*read);
  return {};
}

core::Result<void> MeritShop::startNewGame(GameSession& session) {
  session_ = &session;
  visiting_ = false;
  return {};
}

void MeritShop::tick(GameSession& session, const ruleset::ClockAdvance& /*advance*/) {
  session_ = &session;
}

void MeritShop::afterLoad(GameSession& session) {
  session_ = &session;
  visiting_ = false;
}

bool MeritShop::available() const {
  return rules_.has_value() && session_ != nullptr && desc_.cards != nullptr &&
         desc_.cards->collection() != nullptr && desc_.inventory != nullptr;
}

bool MeritShop::open() {
  if (!available()) {
    core::logDebug("[port:merit] call_routine 1 (openMeritShop, shop/script_menu.c:788): no "
                   "merit_shop section, cards, bag or game; the visit closes at once");
    return false;
  }
  if (!lineTables_ && desc_.files != nullptr) {
    if (auto tables = readMapHeadLineTables(*desc_.files); tables) {
      lineTables_ = std::move(*tables);
    } else {
      core::logWarning("[port:merit] resolveMapHeadEntry: {}", tables.error().message);
    }
  }
  visiting_ = true;
  core::logDebug("[port:merit] openMeritShop (shop/script_menu.c:788): {} merit", merit());
  return true;
}

void MeritShop::close() {
  visiting_ = false;
}

const MeritShopRules& MeritShop::rules() const {
  DWE_ASSERT(rules_.has_value());
  return *rules_;
}

std::int32_t MeritShop::merit() const {
  return session_ != nullptr ? session_->tamer().record.merit : 0;
}

const game::inventory::CardCollection& MeritShop::cards() const {
  DWE_ASSERT(available());
  return *desc_.cards->collection();
}

const assets::ItemTable& MeritShop::items() const {
  DWE_ASSERT(available());
  return desc_.inventory->items();
}

void MeritShop::setMerit(std::int32_t merit) {
  TamerRecord record = session_->tamer().record;
  record.merit = merit;
  session_->setTamerRecord(record);
}

std::vector<std::optional<game::inventory::CardSaleRow>> MeritShop::cardRows() const {
  if (!available()) {
    return {};
  }
  return game::inventory::cardSaleRows(*desc_.cards->collection());
}

std::optional<std::int32_t> MeritShop::cardWorth(std::int32_t card) const {
  if (!available() || desc_.cards->collection()->copies(card) <= 0) {
    return std::nullopt;
  }
  return game::inventory::cardMerit(*desc_.cards->collection(), card);
}

std::int32_t MeritShop::worthLine(std::int32_t card) const {
  if (!available()) {
    return 0;
  }
  const assets::CardEntry* entry = desc_.cards->collection()->table().find(card);
  // createMeritCardTradeDialogue: line CARD_DATA[card].spriteId + 7.
  return rules_->lines.cardWorth + (entry != nullptr ? entry->rarity : 0);
}

bool MeritShop::giveCard(std::int32_t card) {
  const std::optional<std::int32_t> worth = cardWorth(card);
  if (!worth || !desc_.cards->collection()->takeCopy(card)) {
    return false;
  }
  const std::int32_t before = merit();
  setMerit(game::inventory::addMerit(before, *worth, rules_->mostMerit));
  core::logDebug("[port:merit] openMeritShop state 8 (shop/script_menu.c:850): card {} given, "
                 "merit {} -> {}, {} copies left",
                 card, before, merit(), desc_.cards->collection()->copies(card));
  return true;
}

std::vector<game::inventory::MeritOffer> MeritShop::itemOffers() const {
  if (!available()) {
    return {};
  }
  return game::inventory::meritOffers(desc_.inventory->inventory(), desc_.inventory->items(),
                                      merit());
}

bool MeritShop::takeItem(assets::ItemHandle item) {
  const std::vector<game::inventory::MeritOffer> offers = itemOffers();
  const auto offer = std::ranges::find(offers, item, &game::inventory::MeritOffer::item);
  if (offer == offers.end() || !offer->buyable) {
    return false;
  }
  const std::int32_t before = merit();
  setMerit(before - offer->price);
  desc_.inventory->inventory().add(item, 1);
  core::logDebug("[port:merit] openMeritShop state 0xc (shop/script_menu.c:880): item {} taken "
                 "for {}, merit {} -> {}",
                 desc_.inventory->items().at(item).id, offer->price, before, merit());
  return true;
}

bool MeritShop::choiceCancellable() const {
  return rules_ && desc_.story != nullptr &&
         !desc_.story->state().trigger(rules_->choiceLockTrigger).value_or(false);
}

void MeritShop::playSound(const SoundCue& cue) {
  if (session_ != nullptr && session_->sound() != nullptr) {
    session_->sound()->playSound(cue.slot, cue.sound);
  }
}

void MeritShop::playFailSound() {
  if (rules_) {
    playSound(rules_->failSound);
  }
}

void MeritShop::playTradeSound(std::string_view mapId) {
  if (rules_ && mapId == rules_->tradeMap) {
    playSound(rules_->tradeSound);
  }
}

std::string MeritShop::keyOf(std::int32_t section, std::int32_t line) const {
  if (!lineTables_ || line < 0) {
    return {};
  }
  const auto table = lineTables_->find(section);
  const auto at = static_cast<std::size_t>(line);
  if (table == lineTables_->end() || at >= table->second.size()) {
    return {};
  }
  return table->second[at];
}

std::string MeritShop::lineKey(std::int32_t line) const {
  return rules_ ? keyOf(rules_->linesSection, line) : std::string{};
}

std::string MeritShop::pointsKey() const {
  return rules_ ? keyOf(rules_->pointsSection, rules_->pointsLine) : std::string{};
}

} // namespace dwe::session
