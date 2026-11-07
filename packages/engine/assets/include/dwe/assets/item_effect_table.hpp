#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/table_composition.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// What using each item does as far as the executable says it, `data/item_effects`
// (content/schemas/item_effects.schema.json, packages/formats/exe/item_effects.hpp): the function
// ITEM_FUNCTIONS names for it and the numbers of the data tables and of the food and chip cases.
// The logic of the other handlers is the ruleset's.

// The function an item runs (item.c: ITEM_FUNCTIONS; main.c: handleBuffDisks).
enum class ItemHandler : std::uint8_t {
  None,
  HpHealing,
  MpHealing,
  DoubleFloppy,
  Status,
  Revive,
  BattleDisk,
  Chip,
  Food,
  Evolution,
};

// The constants a food case sets (item.c: handleFood).
struct FoodValues {
  std::int32_t energy = 0;
  std::int32_t tirednessRelief = 0;
  std::int32_t happiness = 0;
  std::int32_t discipline = 0;
  std::int32_t weight = 0;
  // In hours.
  std::int32_t lifetime = 0;
  std::int32_t maxHp = 0;
  std::int32_t maxMp = 0;
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
  std::int32_t brain = 0;
  // setTrainingBoost(flag, value, hours); flag 0 gives none.
  std::int32_t trainingBoostFlag = 0;
  std::int32_t trainingBoostValue = 0;
  std::int32_t trainingBoostHours = 0;
  // The chance in 100 of getting sick.
  std::int32_t sicknessChance = 0;
};

// The constants a chip case sets (item.c: handleChips).
struct ChipValues {
  std::int32_t maxHp = 0;
  std::int32_t maxMp = 0;
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
  std::int32_t brain = 0;
  // In hours.
  std::int32_t lifetime = 0;
};

struct ItemEffectEntry {
  // The item's id in data/items.
  std::string item;
  // The item's number in the original; a row a pack adds for a new item has none (-1).
  std::int32_t index = 0;
  ItemHandler handler = ItemHandler::None;
  // HP or MP healed (HEALING_VALUES), for the healing handlers.
  std::optional<std::int32_t> healing = std::nullopt;
  // The Digimon an evolution item leads to (EVOLUTION_ITEM_TARGET): its id, or the raw number as
  // text when the extractor could not name it.
  std::optional<std::string> evolvesTo = std::nullopt;
  // The values of a food or chip case the extractor decoded; neither when the case does more than
  // set them.
  std::optional<FoodValues> food = std::nullopt;
  std::optional<ChipValues> chip = std::nullopt;
};

struct ItemEffectTable {
  std::vector<ItemEffectEntry> effects;

  // The effect of the item with this id, or nullptr.
  [[nodiscard]] const ItemEffectEntry* find(std::string_view item) const;
};

// How the packs' files of the table compose: rows by the item's id, so a pack gives an effect to an
// item it adds without copying the table (table_composition.hpp). A row is keyed by another table's
// id: it takes no number of its own.
inline constexpr std::array<EntrySection, 1> ItemEffectEntrySections{
    {{.section = "item_effects", .entryKey = "item", .numbered = false, .ownsIds = false}}};
inline constexpr TableComposition ItemEffectComposition{.sections = ItemEffectEntrySections};

// Parses the text of `data/item_effects`. Unknown and missing keys are errors, and values must fit
// the handler.
core::Result<ItemEffectTable> parseItemEffectTable(std::string_view yaml);

// Reads and parses `data/item_effects` from the virtual file system, composed from every pack that
// has it.
core::Result<ItemEffectTable> loadItemEffectTable(const VirtualFileSystem& vfs);
// The same for the table a ruleset names by its item_effects role.
core::Result<ItemEffectTable> loadItemEffectTable(const VirtualFileSystem& vfs, const AssetId& id);

} // namespace dwe::assets
