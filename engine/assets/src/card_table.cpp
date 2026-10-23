#include <dwe/assets/card_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <format>
#include <utility>

#include "document_text.hpp"

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view CardsId = "data/cards";
constexpr std::string_view Context = "card table";
constexpr std::string_view PricesKey = "prices";
constexpr std::string_view CardsKey = "cards";
constexpr std::string_view IndexKey = "index";
constexpr std::string_view DigimonKey = "digimon";
constexpr std::string_view RarityKey = "sprite_id";
constexpr std::string_view MeritKey = "merit_value";

CardEntry readCard(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a card", {IndexKey, DigimonKey, RarityKey, MeritKey})) {
    return {};
  }
  const YamlNode& digimon = reader.field(node, DigimonKey);
  return {.index = reader.integer<std::int32_t>(reader.field(node, IndexKey)),
          .digimon = YamlReader::isNull(digimon) ? std::string{} : reader.text(digimon),
          .rarity = reader.integer<std::int32_t>(reader.field(node, RarityKey)),
          .meritValue = reader.integer<std::int32_t>(reader.field(node, MeritKey))};
}

} // namespace

const CardEntry* CardTable::find(std::int32_t index) const {
  const auto found = std::ranges::find(cards, index, &CardEntry::index);
  return found == cards.end() ? nullptr : &*found;
}

std::optional<std::int32_t> CardTable::priceOf(std::int32_t rarity) const {
  if (rarity < 0 || std::cmp_greater_equal(rarity, prices.size())) {
    return std::nullopt;
  }
  return prices[static_cast<std::size_t>(rarity)];
}

core::Result<CardTable> parseCardTable(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
  }
  YamlReader reader;
  const YamlNode& root = *tree;
  if (!reader.expectKeys(root, "the card table", {PricesKey, CardsKey})) {
    return std::unexpected{reader.error(Context)};
  }
  CardTable table;
  for (const YamlNode& price : reader.sequence(reader.field(root, PricesKey), PricesKey)) {
    table.prices.push_back(reader.integer<std::int32_t>(price));
  }
  const std::span<const YamlNode> cards = reader.sequence(reader.field(root, CardsKey), CardsKey);
  for (const YamlNode& card : cards) {
    table.cards.push_back(readCard(reader, card));
  }
  for (std::size_t i = 0; reader.ok() && i < table.cards.size(); ++i) {
    for (std::size_t j = 0; j < i; ++j) {
      if (table.cards[i].index == table.cards[j].index) {
        reader.fail(cards[i], std::format("card {} is listed twice", table.cards[i].index));
        break;
      }
    }
  }
  return reader.finish(std::move(table), Context);
}

core::Result<std::optional<CardTable>> loadCardTable(const VirtualFileSystem& vfs) {
  auto id = AssetId::parse(CardsId);
  if (!id) {
    return std::unexpected{id.error()};
  }
  if (vfs.resolve(*id) == nullptr) {
    return std::optional<CardTable>{};
  }
  auto text = readDocumentText(vfs, CardsId);
  if (!text) {
    return std::unexpected{text.error()};
  }
  auto table = parseCardTable(*text);
  if (!table) {
    return std::unexpected{table.error()};
  }
  return std::optional<CardTable>{std::move(*table)};
}

} // namespace dwe::assets
