#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/ruleset/strategy.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace dwe::game::inventory {

// The name of the original's stacking.
inline constexpr std::string_view OriginalStacking = "original";

// Some of one item in a slot of the bag.
struct Stack {
  assets::ItemHandle item;
  std::int32_t count = 0;

  friend bool operator==(const Stack&, const Stack&) = default;
};

// What became of the items given to the bag.
struct AddResult {
  std::int32_t added = 0;
  // Taken but past the stack's limit, so gone (quirk stack_overflow_is_lost).
  std::int32_t lost = 0;
  // Not taken: they stay where they were (on the ground, in the chest, in the shop).
  std::int32_t refused = 0;

  // Whether the bag took them, as giveItem returns 1.
  [[nodiscard]] bool taken() const { return added > 0 || lost > 0; }
};

// Where items go when they enter the bag: the rule the section's `bag.stacking` names.
class StackingRule {
public:
  StackingRule() = default;
  virtual ~StackingRule() = default;
  StackingRule(const StackingRule&) = delete;
  StackingRule& operator=(const StackingRule&) = delete;
  StackingRule(StackingRule&&) = delete;
  StackingRule& operator=(StackingRule&&) = delete;

  // Puts `count` of `item` into the usable slots.
  [[nodiscard]] virtual AddResult add(std::span<std::optional<Stack>> slots,
                                      assets::ItemHandle item, std::int32_t count,
                                      std::int32_t maxStack, bool overflowIsLost) const = 0;
};

// The stacking rules by name, with `original` registered (item.c: giveItem).
ruleset::StrategyRegistry<StackingRule> stackingRules();

} // namespace dwe::game::inventory
