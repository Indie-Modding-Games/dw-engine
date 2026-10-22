#include <dwe/game/inventory/stacking.hpp>

#include <algorithm>
#include <memory>
#include <string>

namespace dwe::game::inventory {

namespace {

constexpr std::string_view StackingWhat = "stacking rule";

// item.c: giveItem: the first stack of the item takes everything, up to the limit, or nothing if it
// is full; with no stack, the first empty slot takes everything.
class OriginalStackingRule final : public StackingRule {
public:
  [[nodiscard]] AddResult add(std::span<std::optional<Stack>> slots, assets::ItemHandle item,
                              std::int32_t count, std::int32_t maxStack,
                              bool overflowIsLost) const override {
    for (std::optional<Stack>& slot : slots) {
      if (slot.has_value() && slot->item == item) {
        return addToStack(*slot, count, maxStack, overflowIsLost);
      }
    }
    const auto empty = std::ranges::find_if(
        slots, [](const std::optional<Stack>& slot) { return !slot.has_value(); });
    if (empty == slots.end()) {
      return {.added = 0, .lost = 0, .refused = count};
    }
    *empty = Stack{.item = item, .count = 0};
    return addToStack(**empty, count, maxStack, overflowIsLost);
  }

private:
  static AddResult addToStack(Stack& stack, std::int32_t count, std::int32_t maxStack,
                              bool overflowIsLost) {
    if (stack.count >= maxStack) {
      return {.added = 0, .lost = 0, .refused = count};
    }
    const std::int32_t added = std::min(count, maxStack - stack.count);
    stack.count += added;
    const std::int32_t rest = count - added;
    return overflowIsLost ? AddResult{.added = added, .lost = rest, .refused = 0}
                          : AddResult{.added = added, .lost = 0, .refused = rest};
  }
};

} // namespace

ruleset::StrategyRegistry<StackingRule> stackingRules() {
  ruleset::StrategyRegistry<StackingRule> registry{std::string{StackingWhat}};
  registry.add(std::string{OriginalStacking},
               [] { return std::make_unique<OriginalStackingRule>(); });
  return registry;
}

} // namespace dwe::game::inventory
