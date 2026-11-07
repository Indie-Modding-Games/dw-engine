#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/battle/rules.hpp>

#include <string_view>

namespace dwe::app {

// The species row whose condition point a character of this species uses (effect_common.c:
// translateConditionFXToEntity reads CONDITION_FX_OFFSETS[getOriginalType(type)]): a variant (a
// shop's customer, a story's or an enemy's copy of a species) borrows its original type's point,
// since its own row of the table is zero (node 0, no offset, the model's root). Without the effect
// rules, the species' own row. Null when the table has neither.
[[nodiscard]] inline const assets::DigimonRow*
conditionPointRow(const assets::DigimonTable& species, const game::battle::EffectRules* effects,
                  std::string_view id) {
  if (effects != nullptr) {
    if (const assets::DigimonRow* original = species.find(effects->originalTypeOf(id))) {
      return original;
    }
  }
  return species.find(id);
}

} // namespace dwe::app
