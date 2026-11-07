#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/battle/rules.hpp>

#include <doctest/doctest.h>

#include "condition_point.hpp"

namespace {

dwe::assets::DigimonRow row(const char* id, std::uint32_t node) {
  dwe::assets::DigimonRow out;
  out.id = id;
  out.conditionFx = dwe::assets::ConditionFxPoint{.node = node, .offset = {}};
  return out;
}

} // namespace

TEST_CASE("condition point: a variant hangs its effects on its original type's point") {
  // effect_common.c: translateConditionFXToEntity reads CONDITION_FX_OFFSETS[getOriginalType]:
  // two of the canyon shop's four customers are variants whose own row is zero (node 0, no offset),
  // so the mood bubble went into the model's root instead of over its head.
  const dwe::assets::DigimonTable table{{row("base", 4), row("copy", 0), row("plain", 3)}};
  dwe::game::battle::EffectRules effects;
  effects.originalTypes.emplace("copy", "base");

  const auto* copy = dwe::app::conditionPointRow(table, &effects, "copy");
  REQUIRE(copy != nullptr);
  CHECK(copy->id == "base");
  CHECK(copy->conditionFx->node == 4);
  // A species of no variant uses its own row, with or without the rules.
  CHECK(dwe::app::conditionPointRow(table, &effects, "plain")->id == "plain");
  CHECK(dwe::app::conditionPointRow(table, nullptr, "copy")->id == "copy");
  // A variant whose original type the table lacks keeps its own; an unknown species has none.
  effects.originalTypes.emplace("plain", "missing");
  CHECK(dwe::app::conditionPointRow(table, &effects, "plain")->id == "plain");
  CHECK(dwe::app::conditionPointRow(table, &effects, "unknown") == nullptr);
}
