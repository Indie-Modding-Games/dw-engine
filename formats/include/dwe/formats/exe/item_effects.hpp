#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/exe/executable_layout.hpp>
#include <dwe/formats/exe/memory_image.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What using each item does, as far as the executable says it in data or in constant switches.
// ITEM_FUNCTIONS gives the function each item runs; HEALING_VALUES and EVOLUTION_ITEM_TARGET give
// the numbers of two of them; the values of food and chips are constants in handleFood and
// handleChips, read with evaluateConstantSwitch. The remaining handlers (status items, revives,
// battle disks, special cases) are logic: the ruleset describes them.
namespace dwe::formats::exe {

// The function ITEM_FUNCTIONS names for an item (item.c, main.c: handleBuffDisks).
enum class ItemHandler : std::uint8_t {
  None,         // NULL: using the item does nothing
  HpHealing,    // handleHPHealingItem
  MpHealing,    // handleMPHealingItem
  DoubleFloppy, // handleDoubleFloppy
  Status,       // handleStatusItems
  Revive,       // handleRestore
  BattleDisk,   // handleBuffDisks
  Chip,         // handleChips
  Food,         // handleFood
  Evolution,    // handleEvoItems
};

// The locals of handleFood, in the order the layout gives their locations.
enum class FoodValue : std::uint8_t {
  Energy,
  TirednessRelief,
  Happiness,
  Discipline,
  Weight,
  Lifetime,
  MaxHp,
  MaxMp,
  Hp,
  Mp,
  Offense,
  Defense,
  Speed,
  Brain,
  TrainingBoostFlag,
  TrainingBoostValue,
  TrainingBoostHours,
  SicknessChance,
};
inline constexpr std::size_t FoodValueCount = 18;

// The locals of handleChips, in the order the layout gives their locations.
enum class ChipValue : std::uint8_t { MaxHp, MaxMp, Offense, Defense, Speed, Brain, Lifetime };
inline constexpr std::size_t ChipValueCount = 7;

inline constexpr std::size_t ItemHandlerCount = 10;

// The names of the handlers and values in data/item_effects.yaml
// (content/schemas/item_effects.schema.json), in enum order.
inline constexpr std::array<std::string_view, ItemHandlerCount> ItemHandlerNames{
    "none",   "hp_healing",  "mp_healing", "double_floppy", "status",
    "revive", "battle_disk", "chip",       "food",          "evolution"};
inline constexpr std::array<std::string_view, FoodValueCount> FoodValueNames{"energy",
                                                                             "tiredness_relief",
                                                                             "happiness",
                                                                             "discipline",
                                                                             "weight",
                                                                             "lifetime",
                                                                             "max_hp",
                                                                             "max_mp",
                                                                             "hp",
                                                                             "mp",
                                                                             "offense",
                                                                             "defense",
                                                                             "speed",
                                                                             "brain",
                                                                             "training_boost_flag",
                                                                             "training_boost_value",
                                                                             "training_boost_hours",
                                                                             "sickness_chance"};
inline constexpr std::array<std::string_view, ChipValueCount> ChipValueNames{
    "max_hp", "max_mp", "offense", "defense", "speed", "brain", "lifetime"};

// One item's effect as the executable gives it.
struct ItemEffectCode {
  ItemHandler handler = ItemHandler::None;
  // HP or MP healed by the healing handlers (HEALING_VALUES).
  std::optional<std::int16_t> healing;
  // The Digimon type an evolution item leads to (EVOLUTION_ITEM_TARGET), for the items of its
  // range.
  std::optional<std::uint8_t> evolutionTarget;
  // The locals of a food or chip case, in FoodValue or ChipValue order; empty when the case does
  // more than set them (and `skipped` says why).
  std::vector<std::int32_t> values;
  std::string skipped;
};

// Reads the effect of every item: ITEM_FUNCTIONS has one entry per item of ITEM_PARA.
core::Result<std::vector<ItemEffectCode>> readItemEffects(const MemoryImage& image,
                                                          const ExecutableLayout& layout);

} // namespace dwe::formats::exe
