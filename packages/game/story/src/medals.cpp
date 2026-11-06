#include <dwe/core/log.hpp>
#include <dwe/game/story/medals.hpp>

namespace dwe::game::story {

namespace {

bool everyNumber(std::int32_t first, std::int32_t end,
                 const std::function<bool(std::int32_t)>& holds) {
  if (!holds) {
    return false;
  }
  for (std::int32_t number = first; number < end; ++number) {
    if (!holds(number)) {
      return false;
    }
  }
  return true;
}

// Gives `medal` when the player lacks it and `holds()` says its condition is met.
template <typename Condition>
core::Result<void> give(StoryState& state, const MedalRules& rules, std::int32_t medal,
                        EarnedMedals& earned, const Condition& holds) {
  auto held = hasMedal(state, rules, medal);
  if (!held) {
    return std::unexpected{held.error()};
  }
  if (*held || !holds()) {
    return {};
  }
  if (auto unlocked = unlockMedal(state, rules, medal); !unlocked) {
    return unlocked;
  }
  core::logDebug("[port:medals] checkMedalConditions (tamer.c:721): medal {} given", medal);
  earned.medals.push_back(medal);
  return {};
}

} // namespace

core::Result<bool> hasMedal(const StoryState& state, const MedalRules& rules, std::int32_t medal) {
  if (medal < 0 || medal >= rules.medalLimit) {
    return false;
  }
  return state.trigger(rules.firstTrigger + medal);
}

core::Result<void> unlockMedal(StoryState& state, const MedalRules& rules, std::int32_t medal) {
  if (medal < 0 || medal >= rules.medalLimit) {
    return {};
  }
  return state.setTrigger(rules.firstTrigger + medal, true);
}

core::Result<EarnedMedals> checkMedalConditions(StoryState& state, const MedalRules& rules,
                                                const MedalFacts& facts) {
  EarnedMedals earned;
  core::Result<void> given;
  if (const auto& moves = rules.allMoves) {
    given = give(state, rules, moves->medal, earned,
                 [&] { return everyNumber(0, moves->count, facts.knowsMove); });
  }
  if (given && rules.maxStats) {
    given = give(state, rules, rules.maxStats->medal, earned,
                 [&] { return facts.stats == rules.maxStats->stats; });
  }
  if (given && rules.money) {
    given = give(state, rules, rules.money->medal, earned,
                 [&] { return facts.money == rules.money->amount; });
  }
  if (given && rules.year) {
    given = give(state, rules, rules.year->medal, earned,
                 [&] { return facts.year == rules.year->year; });
  }
  if (const auto& raised = rules.allRaised; given && raised) {
    const std::size_t before = earned.medals.size();
    given = give(state, rules, raised->medal, earned,
                 [&] { return everyNumber(raised->first, raised->end, facts.raised); });
    if (earned.medals.size() > before) {
      earned.tamerLevels += raised->tamerLevels;
    }
  }
  if (given && rules.fish) {
    given = give(state, rules, rules.fish->medal, earned,
                 [&] { return facts.fishCaught >= rules.fish->count; });
  }
  if (const auto& cards = rules.allCards; given && cards) {
    given = give(state, rules, cards->medal, earned, [&] {
      return everyNumber(0, cards->count, [&](std::int32_t card) {
        return facts.cardCopies && facts.cardCopies(card) > 0;
      });
    });
  }
  if (!given) {
    return std::unexpected{given.error()};
  }
  return earned;
}

} // namespace dwe::game::story
