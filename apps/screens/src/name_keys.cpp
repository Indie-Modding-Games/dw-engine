#include <dwe/localization/catalog.hpp>
#include <dwe/screens/name_keys.hpp>

#include <cstddef>
#include <optional>
#include <string>

namespace dwe::screens {
namespace {

std::string keyOfIndex(std::string_view block, std::int32_t index) {
    return index < 0 ? std::string{} : localization::gameTextKey(MainText, block, static_cast<std::size_t>(index));
}

} // namespace

std::string NameKeys::species(std::string_view id) const {
    const assets::DigimonRow* row = species_ == nullptr ? nullptr : species_->find(id);
    return row == nullptr ? std::string{} : keyOfIndex(SpeciesNamesBlock, row->index);
}

std::string NameKeys::item(std::int32_t index) const {
    const assets::ItemEntry* entry = items_ == nullptr ? nullptr : items_->findByIndex(index);
    return entry == nullptr ? std::string{} : keyOfIndex(ItemNamesBlock, entry->index);
}

std::string NameKeys::item(std::string_view id) const {
    const assets::ItemEntry* entry = items_ == nullptr ? nullptr : items_->find(id);
    return entry == nullptr ? std::string{} : keyOfIndex(ItemNamesBlock, entry->index);
}

std::string NameKeys::itemDescription(std::int32_t index) const {
    const assets::ItemEntry* entry = items_ == nullptr ? nullptr : items_->findByIndex(index);
    return entry == nullptr ? std::string{} : keyOfIndex(ItemDescriptionsBlock, entry->index);
}

std::string NameKeys::move(std::string_view id) const {
    const auto found = moves_ == nullptr ? std::nullopt : moves_->find(id);
    return found ? move(*found) : std::string{};
}

std::string NameKeys::move(game::battle::MoveRef move) const {
    if (moves_ == nullptr || move >= moves_->size()) {
        return {};
    }
    return keyOfIndex(MoveNamesBlock, moves_->at(move).index);
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
