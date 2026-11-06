#include "usability.hpp"

#include <algorithm>

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;

std::int32_t discountPercent(const ConditionRules& condition, std::int32_t brain) {
  const auto tier =
      std::ranges::find_if(condition.mpDiscounts,
                           [brain](const MpDiscount& discount) { return brain >= discount.brain; });
  return tier == condition.mpDiscounts.end() ? 0 : tier->percent;
}

} // namespace

std::int32_t partnerMpCost(const BattleRules& rules, std::int32_t cost, std::int32_t brain,
                           bool sickOrInjured, MpCostUse use) {
  const std::int32_t discount = discountPercent(rules.condition, brain);
  const std::int32_t extra = sickOrInjured ? rules.condition.mpCostExtraPercent : 0;
  const bool checkOrder =
      use == MpCostUse::Check && rules.quirks.on(quirk::MpCheckAndPaymentRoundDifferently);
  if (checkOrder) {
    const std::int32_t discounted = cost - (cost * discount / Hundred);
    return discounted + (discounted * extra / Hundred);
  }
  const std::int32_t surcharged = cost + (cost * extra / Hundred);
  return surcharged * (Hundred - discount) / Hundred;
}

bool moveUsable(const BattleView& view, FighterId fighter, std::size_t slot) {
  const Fighter& self = view.state->fighters.at(fighter);
  const std::optional<MoveRef> ref = self.moves.at(slot);
  if (!ref) {
    return false;
  }
  const BattleMove& move = view.moves->at(*ref);
  if (move.counter || move.finisher) {
    return false;
  }
  if (move.range == MoveRange::Self && self.buffsLeft == 0) {
    return false;
  }
  if (!self.target && move.needsTarget) {
    return false;
  }
  std::int32_t cost = move.mpCost;
  if (fighter == PartnerId) {
    cost = partnerMpCost(*view.rules, cost, self.stats.brain,
                         view.partnerSick || view.partnerInjured, MpCostUse::Check);
  }
  return self.mp >= cost;
}

std::array<bool, EquippedSlots> usableSlots(const BattleView& view, FighterId fighter) {
  std::array<bool, EquippedSlots> usable{};
  for (std::size_t slot = 0; slot < EquippedSlots; ++slot) {
    usable.at(slot) = moveUsable(view, fighter, slot);
  }
  return usable;
}

bool anyUsable(const std::array<bool, EquippedSlots>& usable) {
  return std::ranges::any_of(usable, [](bool slot) { return slot; });
}

std::optional<std::size_t> randomUsableSlot(ruleset::Random& random,
                                            const std::array<bool, EquippedSlots>& usable) {
  std::array<std::size_t, EquippedSlots> picked{};
  std::size_t count = 0;
  for (std::size_t slot = 0; slot < EquippedSlots; ++slot) {
    if (usable.at(slot)) {
      picked.at(count++) = slot;
    }
  }
  if (count == 0) {
    return std::nullopt;
  }
  return picked.at(static_cast<std::size_t>(random.below(static_cast<std::int32_t>(count))));
}

} // namespace dwe::game::battle
