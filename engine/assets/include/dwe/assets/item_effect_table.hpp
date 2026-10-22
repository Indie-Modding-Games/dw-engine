#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// What using each item does as far as the executable says it, `data/item_effects`
// (content/schemas/item_effects.schema.json, formats/exe/item_effects.hpp): the function
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

// Parses the text of `data/item_effects`. Unknown and missing keys are errors, and values must fit
// the handler.
core::Result<ItemEffectTable> parseItemEffectTable(std::string_view yaml);

// Reads and parses `data/item_effects` from the virtual file system.
core::Result<ItemEffectTable> loadItemEffectTable(const VirtualFileSystem& vfs);

} // namespace dwe::assets
