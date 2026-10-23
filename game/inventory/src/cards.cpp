#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/inventory/cards.hpp>

#include <algorithm>
#include <utility>

namespace dwe::game::inventory {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view SectionText = "the cards section";
constexpr std::string_view MaxCopiesKey = "max_copies";
constexpr std::string_view ListedKey = "listed";
constexpr std::string_view FirstKey = "first";
constexpr std::string_view LastKey = "last";
constexpr std::string_view PackKey = "pack";
constexpr std::string_view DrawsKey = "draws";
constexpr std::string_view RarityRollKey = "rarity_roll";
constexpr std::string_view RarityBelowKey = "rarity_below";
constexpr std::string_view LinesKey = "lines";
constexpr std::string_view NewByRarityKey = "new_by_rarity";
constexpr std::string_view FewCopiesKey = "few_copies";
constexpr std::string_view AgainKey = "again";
constexpr std::string_view ManyKey = "many";

std::vector<std::int32_t> integers(YamlReader& reader, const YamlNode& parent,
                                   std::string_view key) {
  std::vector<std::int32_t> values;
  for (const YamlNode& value : reader.sequence(reader.field(parent, key), key)) {
    values.push_back(reader.integer<std::int32_t>(value));
  }
  return values;
}

std::int32_t integer(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  return reader.integer<std::int32_t>(reader.field(parent, key));
}

} // namespace

core::Result<CardRules> parseCardRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, SectionText, {MaxCopiesKey, ListedKey, PackKey, LinesKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  const YamlNode& listed = reader.field(root, ListedKey);
  const YamlNode& pack = reader.field(root, PackKey);
  const YamlNode& lines = reader.field(root, LinesKey);
  if (!reader.expectKeys(listed, ListedKey, {FirstKey, LastKey}) ||
      !reader.expectKeys(pack, PackKey, {DrawsKey, RarityRollKey, RarityBelowKey}) ||
      !reader.expectKeys(lines, LinesKey, {NewByRarityKey, FewCopiesKey, AgainKey, ManyKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  CardRules rules{.maxCopies = integer(reader, root, MaxCopiesKey),
                  .firstListed = integer(reader, listed, FirstKey),
                  .lastListed = integer(reader, listed, LastKey),
                  .pack = {.draws = integer(reader, pack, DrawsKey),
                           .rarityRoll = integer(reader, pack, RarityRollKey),
                           .rarityBelow = integers(reader, pack, RarityBelowKey)},
                  .lines = {.newByRarity = integers(reader, lines, NewByRarityKey),
                            .fewCopies = integer(reader, lines, FewCopiesKey),
                            .again = integer(reader, lines, AgainKey),
                            .many = integer(reader, lines, ManyKey)}};
  return reader.finish(std::move(rules), section.file);
}

CardCollection::CardCollection(CardRules rules, assets::CardTable table)
    : rules_{std::move(rules)}, table_{std::move(table)} {}

void CardCollection::restore(CardState state) {
  state_ = std::move(state);
  if (state_.copies.size() > table_.cards.size()) {
    state_.copies.resize(table_.cards.size());
  }
}

std::int32_t CardCollection::copies(std::int32_t card) const {
  if (card < 0 || std::cmp_greater_equal(card, state_.copies.size())) {
    return 0;
  }
  return state_.copies[static_cast<std::size_t>(card)];
}

std::vector<HeldCard> CardCollection::heldCards() const {
  std::vector<HeldCard> held;
  for (std::int32_t card = rules_.firstListed; card <= rules_.lastListed; ++card) {
    if (const std::int32_t count = copies(card); count > 0) {
      held.push_back({.card = card, .copies = count});
    }
  }
  return held;
}

std::int32_t CardCollection::rollRarity(ruleset::Random& random) const {
  const std::int32_t roll = random.below(rules_.pack.rarityRoll);
  const auto bound = std::ranges::find_if(rules_.pack.rarityBelow,
                                          [roll](std::int32_t below) { return roll < below; });
  return static_cast<std::int32_t>(bound - rules_.pack.rarityBelow.begin());
}

std::int32_t CardCollection::lineFor(const assets::CardEntry& card, std::int32_t copies) const {
  const CardLines& lines = rules_.lines;
  if (copies == 0) {
    if (lines.newByRarity.empty()) {
      return 0;
    }
    const auto last = static_cast<std::int32_t>(lines.newByRarity.size()) - 1;
    return lines.newByRarity[static_cast<std::size_t>(std::clamp(card.rarity, 0, last))];
  }
  return copies < lines.fewCopies ? lines.again : lines.many;
}

std::optional<DrawnCard> CardCollection::drawPackCard(ruleset::Random& random) {
  const std::int32_t rarity = rollRarity(random);
  std::vector<const assets::CardEntry*> candidates;
  for (const assets::CardEntry& card : table_.cards) {
    if (card.rarity == rarity) {
      candidates.push_back(&card);
    }
  }
  if (candidates.empty()) {
    core::logDebug("[port:cards] rollCard (script_menu.c:1414): no card of rarity {}", rarity);
    return std::nullopt;
  }
  const assets::CardEntry& card = *candidates[static_cast<std::size_t>(
      random.below(static_cast<std::int32_t>(candidates.size())))];
  const std::int32_t before = copies(card.index);
  if (before < rules_.maxCopies) {
    if (std::cmp_greater_equal(card.index, state_.copies.size())) {
      state_.copies.resize(static_cast<std::size_t>(card.index) + 1, 0);
    }
    state_.copies[static_cast<std::size_t>(card.index)] = before + 1;
  }
  const DrawnCard drawn{.card = card.index, .copiesBefore = before, .line = lineFor(card, before)};
  core::logDebug("[port:cards] rollCard / showCardTextbox (script_menu.c:1414, 1327): rarity {} "
                 "card {} ('{}'), {} copies before, line {}",
                 rarity, card.index, card.digimon, before, drawn.line);
  return drawn;
}

void CardCollection::setCopies(std::int32_t card, std::int32_t copies) {
  if (std::cmp_greater_equal(card, state_.copies.size())) {
    state_.copies.resize(static_cast<std::size_t>(card) + 1, 0);
  }
  state_.copies[static_cast<std::size_t>(card)] = copies;
}

bool CardCollection::giveCopy(std::int32_t card) {
  if (table_.find(card) == nullptr) {
    return false;
  }
  const std::int32_t before = copies(card);
  if (before < rules_.maxCopies) {
    setCopies(card, before + 1);
  }
  core::logDebug("[port:cards] give_card (script_interp.c:1143): card {}, {} -> {} copies", card,
                 before, copies(card));
  return true;
}

bool CardCollection::takeCopy(std::int32_t card) {
  if (table_.find(card) == nullptr) {
    return false;
  }
  const std::int32_t before = copies(card);
  if (before > 0) {
    setCopies(card, before - 1);
  }
  core::logDebug("[port:cards] take_card (script_interp.c:1151): card {}, {} -> {} copies", card,
                 before, copies(card));
  return true;
}

} // namespace dwe::game::inventory
