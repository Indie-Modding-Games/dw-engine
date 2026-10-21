#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/game/battle/state.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dwe::screens {

// The original's text the screens show, by source and block of its text tables (docs/formats/textos.md): the slot of
// a name is the row's index in its data table.
inline constexpr std::string_view MainText = "main";
inline constexpr std::string_view BattleText = "btl";
inline constexpr std::string_view SpeciesNamesBlock = "digimon_names";
inline constexpr std::string_view ItemNamesBlock = "item_names";
inline constexpr std::string_view ItemDescriptionsBlock = "item_descs";
inline constexpr std::string_view MoveNamesBlock = "move_names";
inline constexpr std::string_view CommandNamesBlock = "command_names";
inline constexpr std::string_view StatLabelsBlock = "stat_labels";
inline constexpr std::string_view ItemMenuLabelsBlock = "item_menu_labels";
inline constexpr std::string_view SortLabelsBlock = "inventory_sort_labels";
inline constexpr std::string_view ConfirmLabelsBlock = "inventory_confirm_labels";
inline constexpr std::string_view BitsLabelBlock = "battle_ui_bits_labels";

// The text keys of the names of species, items and moves, from the tables that give their rows' indices; an entry a
// pack adds has its own key (localization::addedEntryTextKey). A name
// whose row is unknown (or whose table is missing) has an empty key, which the screens leave blank.
class NameKeys {
public:
    NameKeys(const assets::DigimonTable* species, const assets::ItemTable* items,
             const game::battle::MoveCatalog* moves)
        : species_(species), items_(items), moves_(moves) {}

    [[nodiscard]] std::string species(std::string_view id) const;
    [[nodiscard]] std::string item(assets::ItemHandle item) const;
    [[nodiscard]] std::string item(std::string_view id) const;
    [[nodiscard]] std::string itemDescription(assets::ItemHandle item) const;
    [[nodiscard]] std::string move(std::string_view id) const;
    [[nodiscard]] std::string move(game::battle::MoveRef move) const;

private:
    const assets::DigimonTable* species_;
    const assets::ItemTable* items_;
    const game::battle::MoveCatalog* moves_;
};

// The original's label of a command that is not a move: the battle's command names, in the order of the commands'
// numbers from Flee on (btl.command_names, battle_tick.c: handleCommands). Empty for a move's command.
[[nodiscard]] std::string commandLabelKey(game::battle::Command command);

// A label of the original's text tables by its block and slot in main (stats, the item menu, the sort orders...).
[[nodiscard]] std::string mainLabelKey(std::string_view block, std::size_t slot);

} // namespace dwe::screens
