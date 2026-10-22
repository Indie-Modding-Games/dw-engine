#pragma once

#include <dwe/game/evolution/inputs.hpp>
#include <dwe/game/evolution/result.hpp>
#include <dwe/game/evolution/rules.hpp>
#include <dwe/game/evolution/state.hpp>
#include <dwe/game/evolution/tables.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace dwe::game::evolution {

// What evolving into a target does to the partner (evl.c: EVL_applyEvolution, EVL_scaleBaseStats):
// stats, weight, counters, lifetime, the move it learns and its equipped moves. The stats are not
// capped here: the partner caps the change when it adds it (EVL_clampBaseStats).
class EvolutionEffects {
public:
  EvolutionEffects(const EvolutionRules& rules, const EvolutionTables& tables)
      : rules_(&rules), tables_(&tables) {}

  // The result of evolving into `target`, whose reference row is `reference`.
  [[nodiscard]] EvolutionResult apply(const EvolutionInputs& inputs, const EvolutionState& state,
                                      Species target, const StatReference& reference,
                                      EvolutionReason reason) const;

  // The result of a rebirth as `baby` (evolution.c: reincarnatePartner): only the form and the
  // moves change.
  [[nodiscard]] static EvolutionResult rebirth(const EvolutionInputs& inputs, Species baby);

private:
  // Whether the stats and the life bonus are left alone: an item evolution, and after one with the
  // quirk.
  [[nodiscard]] bool gainsSuppressed(const EvolutionState& state, EvolutionReason reason) const;
  // Whether the target scales the stats instead of gaining (EVL_scaleBaseStats).
  [[nodiscard]] bool scalesStats(const EvolutionInputs& inputs, Species target) const;
  [[nodiscard]] static BaseStats rebuiltStats(const EvolutionInputs& inputs);
  [[nodiscard]] BaseStats scaledStats(const BaseStats& stats, const StatReference& reference) const;
  [[nodiscard]] BaseStats gainedStats(const BaseStats& stats, const BaseStats& reference) const;
  [[nodiscard]] std::int32_t gainedStat(std::int32_t stat, std::int32_t reference) const;
  [[nodiscard]] std::int32_t tamerLevelChance(Level level) const;
  // Whether the partner is in the virus form its inputs name.
  [[nodiscard]] static bool inVirusForm(const EvolutionInputs& inputs);

  // The move the new form learns: the weakest of its first specialty's block.
  [[nodiscard]] std::optional<std::string> learnedMove(const SpeciesData& form) const;
  [[nodiscard]] EquippedMoves equippedMoves(const SpeciesData& form,
                                            const std::optional<std::string>& learned) const;

  const EvolutionRules* rules_;
  const EvolutionTables* tables_;
};

} // namespace dwe::game::evolution
