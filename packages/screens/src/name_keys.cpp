#include <dwe/assets/content_id.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/screens/name_keys.hpp>

#include <cstddef>
#include <optional>
#include <string>

namespace dwe::screens {
namespace {

std::string keyOfIndex(std::string_view block, std::int32_t index) {
  return index < 0 ? std::string{}
                   : localization::gameTextKey(MainText, block, static_cast<std::size_t>(index));
}

// The key of an entry's text: an entry a pack adds has its own (localization::addedEntryTextKey),
// an original one the slot of its number in the original's table.
std::string keyOfEntry(std::string_view block, std::string_view id, std::int32_t index) {
  if (auto added = assets::ContentId::parse(id); added && added->isNamespaced()) {
    return localization::addedEntryTextKey(*added, block);
  }
  return keyOfIndex(block, index);
}

} // namespace

std::string NameKeys::species(std::string_view id) const {
  const assets::DigimonRow* row = species_ == nullptr ? nullptr : species_->find(id);
  if (row == nullptr) {
    return {};
  }
  return keyOfEntry(SpeciesNamesBlock, row->id, row->index);
}

std::string NameKeys::item(assets::ItemHandle item) const {
  if (items_ == nullptr) {
    return {};
  }
  const assets::ItemEntry& entry = items_->at(item);
  return keyOfEntry(ItemNamesBlock, entry.id, entry.index);
}

std::string NameKeys::item(std::string_view id) const {
  const assets::ItemEntry* entry = items_ == nullptr ? nullptr : items_->find(id);
  return entry == nullptr ? std::string{} : keyOfEntry(ItemNamesBlock, entry->id, entry->index);
}

std::string NameKeys::itemDescription(assets::ItemHandle item) const {
  if (items_ == nullptr) {
    return {};
  }
  const assets::ItemEntry& entry = items_->at(item);
  return keyOfEntry(ItemDescriptionsBlock, entry.id, entry.index);
}

std::string NameKeys::itemDescriptionSpecies(assets::ItemHandle item) const {
  if (items_ == nullptr) {
    return {};
  }
  const std::optional<std::string>& named = items_->at(item).descriptionSpecies;
  return named ? species(*named) : std::string{};
}

std::string NameKeys::move(std::string_view id) const {
  const auto found = moves_ == nullptr ? std::nullopt : moves_->find(id);
  return found ? move(*found) : std::string{};
}

std::string NameKeys::move(game::battle::MoveRef move) const {
  if (moves_ == nullptr || move >= moves_->size()) {
    return {};
  }
  const game::battle::BattleMove& entry = moves_->at(move);
  return keyOfEntry(MoveNamesBlock, entry.id, entry.index);
}

std::string commandLabelKey(game::battle::Command command) {
  const auto number = static_cast<std::size_t>(command);
  constexpr auto First = static_cast<std::size_t>(game::battle::Command::Flee);
  constexpr auto Last = static_cast<std::size_t>(game::battle::Command::Change);
  if (number < First || number > Last) {
    return {};
  }
  return localization::gameTextKey(BattleText, CommandNamesBlock, number - First);
}

std::string mainLabelKey(std::string_view block, std::size_t slot) {
  return localization::gameTextKey(MainText, block, slot);
}

} // namespace dwe::screens
