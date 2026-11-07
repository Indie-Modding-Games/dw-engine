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
constexpr std::string_view NewGameSpeciesSlotsKey = "new_game_species_slots";
constexpr std::string_view RebornSlotKey = "reborn_slot";
constexpr std::string_view LearnedKey = "learned";
constexpr std::string_view EquippedKey = "equipped";
constexpr std::string_view NullText = "null";
// partner_moves.quirks: unlearnMove's 16-bit mask.
constexpr std::string_view ForgetMaskQuirk = "forget_mask_16_bits";

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
                         {NewGameSlotKey, RebornSlotKey, ruleset::QuirksKey},
                         {NewGameSpeciesSlotsKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  PartnerMoveRules rules{.newGameSlot = slotOf(reader, reader.field(root, NewGameSlotKey)),
                         .newGameSpeciesSlots = {},
                         .rebornSlot = slotOf(reader, reader.field(root, RebornSlotKey))};
  rules.forgetMask16Bits =
      ruleset::readQuirks(reader, reader.field(root, ruleset::QuirksKey), {ForgetMaskQuirk})
          .on(ForgetMaskQuirk);
  if (const YamlNode* species = root.find(NewGameSpeciesSlotsKey);
      species != nullptr && !YamlReader::isNull(*species)) {
    if (!species->isMapping()) {
      reader.fail(*species, formatMessage(messages::NotSpeciesSlots, NewGameSpeciesSlotsKey));
    } else {
      for (const core::YamlEntry& entry : species->entries()) {
        rules.newGameSpeciesSlots.emplace(entry.key, slotOf(reader, entry.value));
      }
    }
  }
  return reader.finish(std::move(rules), section.file);
}

MoveSlot PartnerMoveRules::newGameSlotOf(std::string_view species) const {
  const auto found = newGameSpeciesSlots.find(species);
  return found != newGameSpeciesSlots.end() ? found->second : newGameSlot;
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
} // namespace

bool knowsTechnique(const PartnerMoves& moves, const game::battle::MoveCatalog& catalog,
                    std::string_view id) {
  if (knows(moves, id))
    return true;
  const auto target = catalog.find(id);
  if (!target)
    return false;
  const auto family = techniqueFamily(catalog.at(*target).index);
  for (const auto& learned : moves.learned) {
    const auto ref = catalog.find(learned);
    if (ref && techniqueFamily(catalog.at(*ref).index) == family)
      return true;
  }
  return false;
}

// game_menu.c: the change view's confirm (tickDigimonMenu): getEquippedSlot finds the slot that
// holds the technique under the cursor (44 and 48, 55 and 57 count as one) and clears it; otherwise
// equipMove puts it in the first free slot, if the partner knows it (hasMove) and its species has
// it, or refuses with playSound(0, 4) when the three are taken. Measured in the original: Spit Fire
// equipped in slot 0, confirm cleared it (EQUIPPED_MOVES 255, base move 255), confirm again put it
// back in slot 0 (base move 46, its species slot plus 0x2E); an unknown technique changed nothing.
TechniqueToggle toggleTechnique(PartnerMoves& moves, const assets::DigimonRow& species,
                                const game::battle::MoveCatalog& catalog, MoveSlot slot) {
  if (slot < 0 || std::cmp_greater_equal(slot, species.moves.size()) ||
      !species.moves[static_cast<std::size_t>(slot)]) {
    return TechniqueToggle::Unavailable;
  }
  const auto& id = *species.moves[static_cast<std::size_t>(slot)];
  const auto ref = catalog.find(id);
  if (!ref || catalog.at(*ref).finisher || !knowsTechnique(moves, catalog, id)) {
    return TechniqueToggle::Unavailable;
  }
  for (std::size_t place = 0; place < game::battle::FinisherSlot; ++place) {
    const auto equipped = moves.equipped[place];
    const auto current = equipped && *equipped >= 0 &&
                                 std::cmp_less(*equipped, species.moves.size()) &&
                                 species.moves[static_cast<std::size_t>(*equipped)]
                             ? catalog.find(*species.moves[static_cast<std::size_t>(*equipped)])
                             : std::nullopt;
    if (current &&
        techniqueFamily(catalog.at(*current).index) == techniqueFamily(catalog.at(*ref).index)) {
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
    if (!slot || *slot < 0 || std::cmp_greater_equal(*slot, species.moves.size()) ||
        !species.moves[static_cast<std::size_t>(*slot)])
      continue;
    const auto move = catalog.find(*species.moves[static_cast<std::size_t>(*slot)]);
    if (move && catalog.at(*move).power > 0)
      return true;
  }
  return false;
}

namespace {

// lostAllLives: the percent of the known moves is (base - trainer level) * 2, rounded at 50.
constexpr std::int32_t ForgetLevelBase = 10;
constexpr std::int32_t ForgetPercentPerLevel = 2;
constexpr std::int32_t Hundred = 100;
constexpr std::int32_t HalfHundred = 50;
// unlearnMove: the learned bits are two words of 32, cleared through a 16-bit mask.
constexpr std::int32_t WordBits = 32;
constexpr std::uint32_t MaskBits = 0xFFFF;
constexpr std::uint32_t SharedBit = 0x1000;

} // namespace

std::int32_t movesToForget(std::int32_t known, std::int32_t trainerLevel) {
  std::int32_t hundredths = (ForgetLevelBase - trainerLevel) * ForgetPercentPerLevel * known;
  if (hundredths <= 0) {
    return 0;
  }
  if (hundredths % Hundred >= HalfHundred) {
    hundredths += Hundred;
  }
  return hundredths / Hundred;
}

std::vector<std::int32_t> movesClearedBy(std::int32_t number, bool mask16Bits) {
  if (!mask16Bits) {
    return {number};
  }
  std::uint32_t mask = 0;
  if (number == 0x2C || number == 0x30) {
    mask = SharedBit;
  } else if (number != 0x37 && number != 0x39) {
    mask = (1U << static_cast<std::uint32_t>(number % WordBits)) & MaskBits;
  }
  // learnedMoves[number / 32] &= (mask ^ 0xFFFF): the bits the kept mask lacks go.
  const std::uint32_t kept = mask ^ MaskBits;
  std::vector<std::int32_t> cleared;
  const std::int32_t word = number / WordBits;
  for (std::int32_t bit = 0; bit < WordBits; ++bit) {
    if ((kept & (1U << static_cast<std::uint32_t>(bit))) == 0) {
      cleared.push_back((word * WordBits) + bit);
    }
  }
  return cleared;
}

bool forget(PartnerMoves& moves, std::string_view move) {
  const auto found = std::ranges::find(moves.learned, move);
  if (found == moves.learned.end()) {
    return false;
  }
  moves.learned.erase(found);
  return true;
}

} // namespace dwe::session
