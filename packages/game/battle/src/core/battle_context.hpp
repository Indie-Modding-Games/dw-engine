#pragma once

#include <dwe/game/battle/battle.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace dwe::game::battle {

class BattleSystem;

struct BattleContext {
  const BattleRules* rules = nullptr;
  const MoveCatalog* moves = nullptr;
  BattleState* state = nullptr;
  BattleStrategies* strategies = nullptr;
  ruleset::Random* random = nullptr;
  BattleEvents* events = nullptr;
  const PartnerCondition* condition = nullptr;
  const BattleGround* ground = nullptr;
  const AttackFlights* flights = nullptr;
  HitParticles* particles = nullptr;
  // Whether `random` is the original's generator (Battle::useOriginalRolls).
  bool originalRolls = false;
  StatusVisuals* visuals = nullptr;
  BattleSystem* system = nullptr;

  [[nodiscard]] BattleView view() const { return view(PartnerId); }
  [[nodiscard]] BattleView view(FighterId actor) const {
    const PartnerCondition& own = conditionOf(actor);
    return {.rules = rules,
            .moves = moves,
            .state = state,
            .random = random,
            .partnerSick = own.sick,
            .partnerInjured = own.injured,
            .actor = actor};
  }
  // The care values of a players' side fighter: the partner's, or a co-op ally's own.
  [[nodiscard]] const PartnerCondition& conditionOf(FighterId id) const {
    const AllyState* ally = state->allyOf(id);
    return ally != nullptr ? ally->condition : *condition;
  }
  // The discipline a commanded fighter obeys with (vs_combat.c: VS_DISCIPLINE).
  [[nodiscard]] std::int32_t disciplineOf(FighterId id) const {
    if (const AllyState* ally = state->allyOf(id)) {
      return ally->condition.discipline;
    }
    return id == PartnerId ? condition->discipline : state->rival->discipline;
  }
  // The rolls of the status visuals and of the hit sparks: the battle's when they are the
  // original's, else their own.
  [[nodiscard]] ruleset::Random& visualRolls() const {
    return originalRolls ? *random : visuals->ownRolls();
  }
  [[nodiscard]] ruleset::Random& particleRolls() const {
    return originalRolls ? *random : particles->ownRolls();
  }
  [[nodiscard]] Fighter& fighter(FighterId id) const { return state->fighters.at(id); }
  void emit(BattleEvent event) const { events->push_back(event); }
};

} // namespace dwe::game::battle
