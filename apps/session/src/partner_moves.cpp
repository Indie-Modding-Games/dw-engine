#include <dwe/assets/yaml_reader.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/partner_moves.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <utility>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;
using messages::formatMessage;

constexpr std::string_view NewGameSlotKey = "new_game_slot";
constexpr std::string_view RebornSlotKey = "reborn_slot";
constexpr std::string_view LearnedKey = "learned";
constexpr std::string_view EquippedKey = "equipped";
constexpr std::string_view NullText = "null";

MoveSlot slotOf(YamlReader& reader, const YamlNode& node) {
  const auto slot = reader.integer<MoveSlot>(node);
  if (reader.ok() && (slot < 0 || std::cmp_greater_equal(slot, game::battle::SpeciesMoves))) {
    reader.fail(node, formatMessage(messages::MoveSlotOutside, slot, game::battle::SpeciesMoves));
  }
  return slot;
}

} // namespace

core::Result<PartnerMoveRules> readPartnerMoveRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, messages::PartnerMovesSectionText,
                         {NewGameSlotKey, RebornSlotKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  const PartnerMoveRules rules{.newGameSlot = slotOf(reader, reader.field(root, NewGameSlotKey)),
                               .rebornSlot = slotOf(reader, reader.field(root, RebornSlotKey))};
  return reader.finish(rules, section.file);
}

core::Result<void> checkPartnerMovesSection(const ruleset::Section& section) {
  if (auto read = readPartnerMoveRules(section); !read) {
    return std::unexpected{read.error()};
  }
  return {};
}

YamlNode toYaml(const PartnerMoves& moves) {
  YamlNode learned = YamlNode::sequence();
  for (const std::string& move : moves.learned) {
    learned.append(YamlNode::scalar(move));
  }
  YamlNode equipped = YamlNode::sequence();
  for (const std::optional<MoveSlot>& slot : moves.equipped) {
    equipped.append(YamlNode::scalar(slot ? std::format("{}", *slot) : std::string{NullText}));
  }
  YamlNode node = YamlNode::mapping();
  node.set(std::string{LearnedKey}, std::move(learned));
  node.set(std::string{EquippedKey}, std::move(equipped));
  return node;
}

core::Result<PartnerMoves> partnerMovesFromYaml(const YamlNode& node) {
  YamlReader reader;
  if (!reader.expectKeys(node, messages::PartnerMovesStateText, {LearnedKey, EquippedKey})) {
    return std::unexpected{reader.error(messages::PartnerMovesStateText)};
  }
  PartnerMoves moves;
  for (const YamlNode& move : reader.sequence(reader.field(node, LearnedKey), LearnedKey)) {
    learn(moves, reader.text(move));
  }
  const auto equipped = reader.sequence(reader.field(node, EquippedKey), EquippedKey);
  if (reader.ok() && equipped.size() != moves.equipped.size()) {
    reader.fail(node,
                formatMessage(messages::EquippedCount, equipped.size(), moves.equipped.size()));
  }
  for (std::size_t index = 0; reader.ok() && index < equipped.size(); ++index) {
    if (!YamlReader::isNull(equipped[index])) {
      moves.equipped.at(index) = slotOf(reader, equipped[index]);
    }
  }
  return reader.finish(std::move(moves), messages::PartnerMovesStateText);
}

bool learn(PartnerMoves& moves, std::string_view move) {
  const auto at = std::ranges::lower_bound(moves.learned, move);
  if (at != moves.learned.end() && *at == move) {
    return false;
  }
  moves.learned.insert(at, std::string{move});
  return true;
}

bool knows(const PartnerMoves& moves, std::string_view move) {
  return std::ranges::binary_search(moves.learned, move);
}

namespace {
int techniqueFamily(int index) {
  return index == 48 ? 44 : index == 57 ? 55 : index;
}
}

bool knowsTechnique(const PartnerMoves& moves, const game::battle::MoveCatalog& catalog,
                    std::string_view id) {
  if (knows(moves, id)) return true;
  const auto target = catalog.find(id);
  if (!target) return false;
  const auto family = techniqueFamily(catalog.at(*target).index);
  for (const auto& learned : moves.learned) {
    const auto ref = catalog.find(learned);
    if (ref && techniqueFamily(catalog.at(*ref).index) == family) return true;
  }
  return false;
}

TechniqueToggle toggleTechnique(PartnerMoves& moves, const assets::DigimonRow& species,
                                const game::battle::MoveCatalog& catalog, MoveSlot slot) {
  if (slot < 0 || std::cmp_greater_equal(slot, species.moves.size()) || !species.moves[static_cast<std::size_t>(slot)]) {
    return TechniqueToggle::Unavailable;
  }
  const auto& id = *species.moves[static_cast<std::size_t>(slot)];
  const auto ref = catalog.find(id);
  if (!ref || catalog.at(*ref).finisher || !knowsTechnique(moves, catalog, id)) {
    return TechniqueToggle::Unavailable;
  }
  for (std::size_t place = 0; place < game::battle::FinisherSlot; ++place) {
    const auto equipped = moves.equipped[place];
    const auto current = equipped && *equipped >= 0 && std::cmp_less(*equipped, species.moves.size())
        && species.moves[static_cast<std::size_t>(*equipped)] ? catalog.find(*species.moves[static_cast<std::size_t>(*equipped)]) : std::nullopt;
    if (current && techniqueFamily(catalog.at(*current).index) == techniqueFamily(catalog.at(*ref).index)) {
      moves.equipped[place].reset();
      return TechniqueToggle::Removed;
    }
  }
  for (std::size_t place = 0; place < game::battle::FinisherSlot; ++place) {
    if (!moves.equipped[place]) {
      moves.equipped[place] = slot;
      return TechniqueToggle::Equipped;
    }
  }
  return TechniqueToggle::Full;
}

bool canFinishTechniqueEdit(const PartnerMoves& moves, const assets::DigimonRow& species,
                            const game::battle::MoveCatalog& catalog) {
  for (std::size_t place = 0; place < game::battle::FinisherSlot; ++place) {
    const auto slot = moves.equipped[place];
    if (!slot || *slot < 0 || std::cmp_greater_equal(*slot, species.moves.size()) || !species.moves[static_cast<std::size_t>(*slot)]) continue;
    const auto move = catalog.find(*species.moves[static_cast<std::size_t>(*slot)]);
    if (move && catalog.at(*move).power > 0) return true;
  }
  return false;
}

} // namespace dwe::session
