#include <dwe/assets/item_table.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/session/field_facts.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <cstdint>
#include <optional>

namespace dwe::session {

namespace {

using game::partner::PartnerState;
using game::partner::Stat;

// The stats the scripts read, by the number they give them (script.h: SCRIPT_STAT_*;
// script_value.c: getStatsPointer). The counters the session does not keep yet (merit, battles and
// tournaments) are not known.
enum class ScriptStat : std::uint8_t {
  Offense,
  Defense,
  Speed,
  Brain,
  MaxHp,
  MaxMp,
  CurrentHp,
  CurrentMp,
  Tiredness,
  Happiness,
  Discipline,
  Energy,
  Virus,
  Lifetime,
  Merit,
  StartedBattles,
  FledBattles,
  TournamentsWon,
  TournamentWins,
  TournamentsLost,
  Weight,
  TamerLevel,
  Lives,
};

std::optional<std::int64_t> statOf(const GameSession& session, ScriptStat stat) {
  const PartnerState& partner = session.partner().state();
  switch (stat) {
  case ScriptStat::Offense:
    return partner.stats.base[Stat::Offense];
  case ScriptStat::Defense:
    return partner.stats.base[Stat::Defense];
  case ScriptStat::Speed:
    return partner.stats.base[Stat::Speed];
  case ScriptStat::Brain:
    return partner.stats.base[Stat::Brain];
  case ScriptStat::MaxHp:
    return partner.stats.base[Stat::Hp];
  case ScriptStat::MaxMp:
    return partner.stats.base[Stat::Mp];
  case ScriptStat::CurrentHp:
    return partner.stats.currentHp;
  case ScriptStat::CurrentMp:
    return partner.stats.currentMp;
  case ScriptStat::Tiredness:
    return partner.mood.tiredness;
  case ScriptStat::Happiness:
    return partner.mood.happiness;
  case ScriptStat::Discipline:
    return partner.mood.discipline;
  case ScriptStat::Energy:
    return partner.hunger.energy;
  case ScriptStat::Virus:
    return partner.toilet.virusBar;
  case ScriptStat::Lifetime:
    return partner.life.remainingLifetime;
  case ScriptStat::Weight:
    return partner.weight;
  case ScriptStat::Lives:
    return partner.life.lives;
  case ScriptStat::TamerLevel:
    return session.tamer().level;
  case ScriptStat::Merit:
  case ScriptStat::StartedBattles:
  case ScriptStat::FledBattles:
  case ScriptStat::TournamentsWon:
  case ScriptStat::TournamentWins:
  case ScriptStat::TournamentsLost:
    break;
  }
  return std::nullopt;
}

// The partner's conditions as the original's bits (params.h: CONDITION_*), which game/partner's
// Condition follows.
std::uint32_t conditionBits(const game::partner::Conditions& conditions) {
  std::uint32_t bits = 0;
  for (std::size_t condition = 0; condition < game::partner::ConditionCount; ++condition) {
    if (conditions.has(static_cast<game::partner::Condition>(condition))) {
      bits |= 1U << condition;
    }
  }
  return bits;
}

} // namespace

game::field::GuardFacts fieldGuardFacts(const GameSession& session,
                                        const InventoryAdapter* inventory,
                                        const StoryAdapter& story) {
  game::field::GuardFacts facts{[&story](std::int32_t trigger) -> std::optional<bool> {
    const auto set = story.state().trigger(trigger);
    return set ? std::optional<bool>{*set} : std::nullopt;
  }};
  if (inventory != nullptr) {
    facts.itemCount = [inventory](std::int32_t item) -> std::optional<std::int64_t> {
      const assets::ItemEntry* entry = inventory->items().findByOriginalIndex(item);
      if (entry == nullptr) {
        return std::nullopt;
      }
      return inventory->inventory().countOf(inventory->items().handleOf(*entry));
    };
    facts.money = [inventory]() -> std::optional<std::int64_t> {
      return inventory->inventory().money();
    };
  }
  facts.stat = [&session](std::int32_t stat) -> std::optional<std::int64_t> {
    if (!session.playing() || stat < 0 || stat > static_cast<std::int32_t>(ScriptStat::Lives)) {
      return std::nullopt;
    }
    return statOf(session, static_cast<ScriptStat>(stat));
  };
  facts.conditions = [&session]() -> std::optional<std::uint32_t> {
    if (!session.playing()) {
      return std::nullopt;
    }
    return conditionBits(session.partner().state().conditions);
  };
  return facts;
}

game::field::TriggerWriter fieldTriggerWriter(StoryAdapter& story) {
  return [&story](std::int32_t trigger, bool set) {
    if (auto written = story.state().setTrigger(trigger, set); !written) {
      core::logWarning("{}", written.error().message);
    }
  };
}

} // namespace dwe::session
