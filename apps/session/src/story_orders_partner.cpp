#include <dwe/core/log.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/game_session.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "story_game_context.hpp"
#include "story_game_messages.hpp"

// The story's orders and reads of the partner (script_interp.c: scriptInstruction10to27,
// scriptInstruction28to3F, scriptInstruction64to7E; script_value.c: getStatsPointer,
// enforceStatsLimits, scriptCompareStat).
namespace dwe::session {

namespace {

namespace msg = story_game_messages;

using game::partner::Condition;
using game::partner::PartnerState;
using game::partner::Stat;
using script::ApiValue;

// deal_damage takes a percent of the maximum HP and leaves at least this much (script_interp.c:
// 1975).
constexpr std::int64_t Percent = 100;
constexpr std::int32_t LeastHpLeft = 1;

enum class StatChange : std::uint8_t { Set, Add, Reduce };

core::Result<ScriptStat> statOf(std::int64_t stat) {
  if (stat < 0 || std::cmp_greater_equal(stat, ScriptStatCount)) {
    return core::fail(msg::formatMessage(msg::NoSuchStat, stat, ScriptStatCount - 1));
  }
  return static_cast<ScriptStat>(stat);
}

std::optional<Stat> baseStatOf(ScriptStat stat) {
  switch (stat) {
  case ScriptStat::Offense:
    return Stat::Offense;
  case ScriptStat::Defense:
    return Stat::Defense;
  case ScriptStat::Speed:
    return Stat::Speed;
  case ScriptStat::Brain:
    return Stat::Brain;
  case ScriptStat::MaxHp:
    return Stat::Hp;
  case ScriptStat::MaxMp:
    return Stat::Mp;
  default:
    return std::nullopt;
  }
}

// The partner's number that a stat names, or nullptr for the ones the partner does not keep.
std::int32_t* partnerField(PartnerState& partner, ScriptStat stat) {
  if (const std::optional<Stat> base = baseStatOf(stat)) {
    return &partner.stats.base[*base];
  }
  switch (stat) {
  case ScriptStat::CurrentHp:
    return &partner.stats.currentHp;
  case ScriptStat::CurrentMp:
    return &partner.stats.currentMp;
  case ScriptStat::Tiredness:
    return &partner.mood.tiredness;
  case ScriptStat::Happiness:
    return &partner.mood.happiness;
  case ScriptStat::Discipline:
    return &partner.mood.discipline;
  case ScriptStat::Energy:
    return &partner.hunger.energy;
  case ScriptStat::Virus:
    return &partner.toilet.virusBar;
  case ScriptStat::Lifetime:
    return &partner.life.remainingLifetime;
  case ScriptStat::Weight:
    return &partner.weight;
  case ScriptStat::Lives:
    return &partner.life.lives;
  default:
    return nullptr;
  }
}

// The highest value set_stat and add_stat leave (script_value.c:356 enforceStatsLimits).
std::int64_t limitOf(const StoryGameContext& context, ScriptStat stat) {
  const PartnerState& partner = context.game().partner().state();
  if (stat == ScriptStat::CurrentHp) {
    return partner.stats.base[Stat::Hp];
  }
  if (stat == ScriptStat::CurrentMp) {
    return partner.stats.base[Stat::Mp];
  }
  return context.rules.statLimits.at(static_cast<std::size_t>(stat));
}

void writeStat(const StoryGameContext& context, ScriptStat stat, std::int64_t value) {
  GameSession& session = context.game();
  const auto number = static_cast<std::int32_t>(std::clamp<std::int64_t>(
      value, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
  if (stat == ScriptStat::TamerLevel) {
    session.setTamerLevel(number);
    return;
  }
  PartnerState partner = session.partner().state();
  std::int32_t* field = partnerField(partner, stat);
  if (field == nullptr) {
    core::logWarning("{}", msg::formatMessage(msg::StatNotKept, static_cast<int>(stat)));
    return;
  }
  *field = number;
  session.partner().takeStoryChange(std::move(partner));
}

core::Result<ApiValue> changeStat(const StoryGameContext& context, std::int64_t number,
                                  std::int64_t amount, StatChange change) {
  const auto stat = statOf(number);
  if (!stat) {
    return std::unexpected{stat.error()};
  }
  const std::int64_t current = scriptStatOf(context.game(), *stat).value_or(0);
  std::int64_t value = 0;
  switch (change) {
  case StatChange::Set:
    value = std::min(amount, limitOf(context, *stat));
    break;
  case StatChange::Add:
    value = std::min(current + amount, limitOf(context, *stat));
    break;
  case StatChange::Reduce: {
    // reduce_stat has no limit, and a floor of 0, or happiness_floor for the happiness.
    const std::int64_t floor = *stat == ScriptStat::Happiness ? context.rules.happinessFloor : 0;
    value = std::max(current - amount, floor);
    break;
  }
  }
  core::logDebug("[port:story] stat change {} (script_interp.c:1179-1225): stat {} {} by {} -> {}",
                 static_cast<int>(change), number, current, amount, value);
  writeStat(context, *stat, value);
  context.game().partner().keepEnergyInBounds(context.game().clock());
  return ApiValue{};
}

core::Result<ApiValue> readStat(const StoryGameContext& context, std::int64_t number) {
  const auto stat = statOf(number);
  if (!stat) {
    return std::unexpected{stat.error()};
  }
  const std::optional<std::int64_t> value = scriptStatOf(context.game(), *stat);
  if (!value) {
    core::logWarning("{}", msg::formatMessage(msg::StatNotKept, number));
  }
  return ApiValue{value.value_or(0)};
}

core::Result<ApiValue> removeCondition(const StoryGameContext& context, std::int64_t mask) {
  // script_interp.c:1893: the bits go, their timers stay.
  PartnerState partner = context.game().partner().state();
  for (std::size_t condition = 0; condition < game::partner::ConditionCount; ++condition) {
    if ((static_cast<std::uint64_t>(mask) & (1U << condition)) != 0) {
      partner.conditions.clear(static_cast<Condition>(condition));
    }
  }
  core::logDebug("[port:story] remove_condition (script_interp.c:1893): mask {:#x}", mask);
  context.game().partner().takeStoryChange(std::move(partner));
  return ApiValue{};
}

core::Result<ApiValue> hasCondition(const StoryGameContext& context, std::int64_t mask) {
  const game::partner::Conditions& conditions = context.game().partner().state().conditions;
  for (std::size_t condition = 0; condition < game::partner::ConditionCount; ++condition) {
    if ((static_cast<std::uint64_t>(mask) & (1U << condition)) != 0 &&
        conditions.has(static_cast<Condition>(condition))) {
      return ApiValue{true};
    }
  }
  return ApiValue{false};
}

core::Result<ApiValue> dealDamage(const StoryGameContext& context, std::int64_t variable) {
  const auto percent = context.variable(variable);
  if (!percent) {
    return std::unexpected{percent.error()};
  }
  PartnerState partner = context.game().partner().state();
  std::int64_t damage = partner.stats.base[Stat::Hp] * *percent / Percent;
  // script_interp.c:1978: the HP never goes under 1; at 0 HP this heals to 1, as the original.
  if (partner.stats.currentHp - damage < LeastHpLeft) {
    damage = partner.stats.currentHp - LeastHpLeft;
  }
  core::logDebug("[port:story] deal_damage (script_interp.c:1970): {}% of {} = {}, HP {} -> {}; "
                 "the freeze and the damage number are not drawn yet",
                 *percent, partner.stats.base[Stat::Hp], damage, partner.stats.currentHp,
                 partner.stats.currentHp - damage);
  partner.stats.currentHp -= static_cast<std::int32_t>(damage);
  context.game().partner().takeStoryChange(std::move(partner));
  return ApiValue{};
}

core::Result<BattleAdapter*> movesOf(const StoryGameContext& context) {
  if (context.desc.battle == nullptr) {
    return core::fail(std::string{msg::NoMoves});
  }
  return context.desc.battle;
}

core::Result<ApiValue> learnMove(const StoryGameContext& context, std::int64_t move) {
  auto battle = movesOf(context);
  if (!battle) {
    return std::unexpected{battle.error()};
  }
  core::logDebug("[port:story] learn_move (script_interp.c:2475, utils2.c:320): move {}", move);
  return done((*battle)->learnMoveNumber(move));
}

core::Result<ApiValue> knowsMove(const StoryGameContext& context, std::int64_t move) {
  auto battle = movesOf(context);
  if (!battle) {
    return std::unexpected{battle.error()};
  }
  const auto known = (*battle)->knowsMoveNumber(move);
  if (!known) {
    return std::unexpected{known.error()};
  }
  return ApiValue{*known};
}

const assets::DigimonRow* speciesByNumber(const StoryGameContext& context, std::int64_t number) {
  if (!context.digimon) {
    return nullptr;
  }
  const auto found =
      std::ranges::find(context.digimon->digimon, number, &assets::DigimonRow::index);
  return found == context.digimon->digimon.end() ? nullptr : &*found;
}

core::Result<ApiValue> storePartnerSpecies(const StoryGameContext& context, std::int64_t variable) {
  const std::string& species = context.game().partner().state().species;
  const assets::DigimonRow* row = context.digimon ? context.digimon->find(species) : nullptr;
  if (row == nullptr) {
    return core::fail(msg::formatMessage(msg::PartnerUnnumbered, species));
  }
  core::logDebug("[port:story] store_partner_species (script_interp.c:811): {} is {}", species,
                 row->index);
  return done(context.setVariable(variable, row->index));
}

core::Result<ApiValue> storeSpeciesType(const StoryGameContext& context, std::int64_t source,
                                        std::int64_t target) {
  const auto number = context.variable(source);
  if (!number) {
    return std::unexpected{number.error()};
  }
  const assets::DigimonRow* row = speciesByNumber(context, *number);
  if (row == nullptr) {
    return core::fail(msg::formatMessage(msg::NoSuchSpecies, *number));
  }
  core::logDebug("[port:story] store_species_type (script_interp.c:1268): species {} type {}",
                 *number, row->type);
  return done(context.setVariable(target, row->type));
}

} // namespace

core::Result<void> BattleAdapter::learnMoveNumber(std::int64_t number) {
  if (catalog_ == nullptr) {
    return core::fail(std::string{msg::NoMoves});
  }
  for (game::battle::MoveRef move = 0; move < catalog_->size(); ++move) {
    if (catalog_->at(move).index != number) {
      continue;
    }
    learn(moves_, catalog_->at(move).id);
    for (const game::battle::MoveRef together : catalog_->learnedWith(move)) {
      core::logDebug("[port:story] learnMove (utils2.c:322): {} comes with {}",
                     catalog_->at(move).id, catalog_->at(together).id);
      learn(moves_, catalog_->at(together).id);
    }
    return {};
  }
  return core::fail(msg::formatMessage(msg::NoSuchMove, number));
}

core::Result<bool> BattleAdapter::knowsMoveNumber(std::int64_t number) const {
  if (catalog_ == nullptr) {
    return core::fail(std::string{msg::NoMoves});
  }
  for (game::battle::MoveRef move = 0; move < catalog_->size(); ++move) {
    if (catalog_->at(move).index == number) {
      return knows(moves_, catalog_->at(move).id);
    }
  }
  return core::fail(msg::formatMessage(msg::NoSuchMove, number));
}

std::optional<std::int64_t> scriptStatOf(const GameSession& session, ScriptStat stat) {
  if (stat == ScriptStat::TamerLevel) {
    return session.tamer().level;
  }
  PartnerState partner = session.partner().state();
  const std::int32_t* field = partnerField(partner, stat);
  if (field == nullptr) {
    return std::nullopt;
  }
  return *field;
}

core::Result<void> publishPartnerOrders(script::ApiRegistry& api, StoryGameContext& context) {
  namespace names = story_game_api;
  const StoryGameContext* game = &context;
  using Operands1 = std::array<std::int64_t, 1>;
  using Operands2 = std::array<std::int64_t, 2>;
  const auto statOrder = [game](StatChange change) {
    return integerOrder<2>([game, change](const Operands2& operands) {
      return changeStat(*game, operands[0], operands[1], change);
    });
  };
  const auto oneOperand =
      [game](core::Result<ApiValue> (*run)(const StoryGameContext&, std::int64_t)) {
        return integerOrder<1>(
            [game, run](const Operands1& operands) { return run(*game, operands[0]); });
      };
  return publishOrders(
      api, {{names::SetStat, statOrder(StatChange::Set)},
            {names::AddStat, statOrder(StatChange::Add)},
            {names::ReduceStat, statOrder(StatChange::Reduce)},
            {names::Stat, oneOperand(&readStat)},
            {names::RemoveCondition, oneOperand(&removeCondition)},
            {names::HasCondition, oneOperand(&hasCondition)},
            {names::DealDamage, oneOperand(&dealDamage)},
            {names::LearnMove, oneOperand(&learnMove)},
            {names::KnowsMove, oneOperand(&knowsMove)},
            {names::StorePartnerSpecies, oneOperand(&storePartnerSpecies)},
            {names::StoreSpeciesType, integerOrder<2>([game](const Operands2& operands) {
               return storeSpeciesType(*game, operands[0], operands[1]);
             })}});
}

} // namespace dwe::session
