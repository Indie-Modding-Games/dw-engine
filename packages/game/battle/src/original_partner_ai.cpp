#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "original_strategies.hpp"
#include "targeting.hpp"
#include "usability.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;
// The values the original gives a slot that does not take part in a ranking.
constexpr std::int32_t UnrankedHigh = -1;
constexpr std::int32_t UnrankedLowMp = 10000;
constexpr std::int32_t UnrankedCheapest = 1000;

using SlotValues = std::array<std::int32_t, TechniqueSlots>;

// The values sorted with the slots they belong to, and the rank of each place: equal neighbours
// share a rank (battle_main.c: BTL_sortScoresDescending, BTL_sortScoresAscending,
// BTL_calculateScoreRanks). A selection sort, as the original, so equal values keep the original's
// order.
struct Ranking {
  SlotValues values{};
  std::array<std::size_t, TechniqueSlots> keys{};
  std::array<std::size_t, TechniqueSlots> ranks{};
};

void sortRanking(Ranking& ranking, bool descending) {
  for (std::size_t i = 0; i < TechniqueSlots; ++i) {
    std::size_t best = i;
    for (std::size_t j = i; j < TechniqueSlots; ++j) {
      const bool better = descending ? ranking.values.at(best) < ranking.values.at(j)
                                     : ranking.values.at(best) > ranking.values.at(j);
      if (better) {
        best = j;
      }
    }
    std::swap(ranking.values.at(i), ranking.values.at(best));
    std::swap(ranking.keys.at(i), ranking.keys.at(best));
  }
  std::size_t rank = 0;
  ranking.ranks.front() = rank;
  for (std::size_t i = 1; i < TechniqueSlots; ++i) {
    if (ranking.values.at(i) != ranking.values.at(i - 1)) {
      ++rank;
    }
    ranking.ranks.at(i) = rank;
  }
}

Ranking unsortedRanking() {
  Ranking ranking;
  for (std::size_t slot = 0; slot < TechniqueSlots; ++slot) {
    ranking.keys.at(slot) = slot;
  }
  return ranking;
}

// The slots with the best value, as BTL_getHighestScoredMove and BTL_getLowestScoredMove mark them:
// the only one, or nullopt and the tied ones marked.
std::optional<std::size_t> bestOf(const SlotValues& values, std::array<bool, EquippedSlots>& marks,
                                  bool highest) {
  const auto best = highest ? std::ranges::max(values) : std::ranges::min(values);
  std::size_t count = 0;
  std::size_t found = 0;
  for (std::size_t slot = 0; slot < TechniqueSlots; ++slot) {
    marks.at(slot) = values.at(slot) == best;
    if (marks.at(slot)) {
      ++count;
      found = slot;
    }
  }
  return count == 1 ? std::optional<std::size_t>{found} : std::nullopt;
}

class OriginalPartnerAi final : public PartnerAi {
public:
  MoveDecision chooseMove(const BattleView& view, MoveStyle style,
                          const std::array<bool, EquippedSlots>& usable) override {
    switch (style) {
    case MoveStyle::Strongest:
      return {.slot = strongestOrCheapest(view, usable, true)};
    case MoveStyle::Cheapest:
      return {.slot = strongestOrCheapest(view, usable, false)};
    case MoveStyle::YourCall:
      break;
    }
    return yourCall(view, usable);
  }

  // battle_main.c: BTL_selectFighterTarget, the partner's own choice.
  std::optional<FighterId> chooseTarget(const BattleView& view) override {
    const BattleState& state = *view.state;
    const Fighter& self = state.partner();
    const bool targetAlive = self.target && !state.fighters.at(*self.target).defeated();
    if (state.commands.current == Command::Change && targetAlive) {
      return self.target;
    }
    const PartnerAiRules& rules = view.rules->partnerAi;
    if (self.stats.brain >= rules.statusTargetBrain) {
      const std::vector<FighterId> afflicted = afflictedOthers(view, PartnerId);
      if (afflicted.size() >= 2) {
        return afflicted.at(static_cast<std::size_t>(
            view.random->below(static_cast<std::int32_t>(afflicted.size()))));
      }
      if (afflicted.size() == 1) {
        return afflicted.front();
      }
      const WeakestChoice weakest = weakestOther(view, PartnerId);
      if (!targetAlive) {
        return weakest.fighter;
      }
      const std::int32_t current = targetScore(view, PartnerId, *self.target);
      if (!weakest.fighter || current - weakest.score < rules.switchMargin) {
        return self.target;
      }
      return weakest.fighter;
    }
    if (self.stats.brain >= rules.scoredTargetBrain) {
      if (targetAlive) {
        return self.target;
      }
      return weakestOther(view, PartnerId).fighter;
    }
    return nearestOther(state, PartnerId);
  }

private:
  // battle_main.c: BTL_selectMoveByPower and BTL_selectMoveByMpCost: the strongest (or cheapest)
  // usable technique, then the most effective against the target among the tied ones, then a random
  // tied one.
  static std::optional<std::size_t>
  strongestOrCheapest(const BattleView& view, const std::array<bool, EquippedSlots>& usable,
                      bool strongest) {
    const Fighter& self = view.state->partner();
    std::array<bool, EquippedSlots> marks = usable;
    SlotValues values{};
    for (std::size_t slot = 0; slot < TechniqueSlots; ++slot) {
      const std::optional<MoveRef> equipped = self.moves.at(slot);
      if (!marks.at(slot) || !equipped) {
        values.at(slot) = strongest ? UnrankedHigh : UnrankedCheapest;
        continue;
      }
      const BattleMove& move = view.moves->at(*equipped);
      values.at(slot) = strongest ? move.power : move.mpCost;
    }
    if (auto best = bestOf(values, marks, strongest)) {
      return best;
    }
    const std::int32_t targetSpecial =
        self.target ? firstSpecial(view.state->fighters.at(*self.target)).value_or(-1) : -1;
    for (std::size_t slot = 0; slot < TechniqueSlots; ++slot) {
      const std::optional<MoveRef> equipped = self.moves.at(slot);
      values.at(slot) =
          marks.at(slot) && equipped
              ? affinityOf(view.rules->affinity, view.moves->at(*equipped).special, targetSpecial)
              : UnrankedHigh;
    }
    if (auto best = bestOf(values, marks, true)) {
      return best;
    }
    return randomUsableSlot(*view.random, marks);
  }

  // battle_main.c: BTL_selectPartnerMove.
  static MoveDecision yourCall(const BattleView& view,
                               const std::array<bool, EquippedSlots>& usable) {
    const Fighter& self = view.state->partner();
    const PartnerAiRules& rules = view.rules->partnerAi;
    std::array<std::int32_t, TechniqueSlots> weights{};
    Ranking ranking = unsortedRanking();
    const auto empty = [&](std::size_t slot) { return !self.moves.at(slot).has_value(); };
    const auto moveIn = [&](std::size_t slot) -> const BattleMove& {
      return view.moves->at(*self.moves.at(slot));
    };

    switch (self.chargeMode) {
    case ChargeMode::Aggressive:
      for (std::size_t slot = 0; slot < TechniqueSlots; ++slot) {
        ranking.values.at(slot) = usable.at(slot) ? moveIn(slot).power : UnrankedHigh;
      }
      sortRanking(ranking, true);
      rankWeights(ranking, rules.powerWeights, usable, empty, rules.emptySlotWeight, weights);
      break;
    case ChargeMode::Normal:
      for (std::size_t slot = 0; slot < TechniqueSlots; ++slot) {
        if (empty(slot)) {
          weights.at(slot) += rules.emptySlotWeight;
        }
        if (usable.at(slot)) {
          weights.at(slot) += rules.evenWeight;
        }
      }
      break;
    case ChargeMode::Cautious:
      for (std::size_t slot = 0; slot < TechniqueSlots; ++slot) {
        ranking.values.at(slot) = usable.at(slot) ? moveIn(slot).mpCost : UnrankedLowMp;
      }
      sortRanking(ranking, false);
      rankWeights(ranking, rules.mpWeights, usable, empty, rules.emptySlotWeight, weights);
      break;
    }

    const std::optional<std::int32_t> ownSpecial = firstSpecial(self);
    const std::int32_t targetSpecial =
        self.target ? firstSpecial(view.state->fighters.at(*self.target)).value_or(-1) : -1;
    for (std::size_t slot = 0; slot < TechniqueSlots; ++slot) {
      if (empty(slot)) {
        continue;
      }
      if (!usable.at(slot)) {
        weights.at(slot) = 0;
        continue;
      }
      const BattleMove& move = moveIn(slot);
      if (move.range == MoveRange::Self) {
        weights.at(slot) +=
            self.buffPriority + elementBonus(view, move.special, ownSpecial.value_or(-1));
        continue;
      }
      weights.at(slot) += elementBonus(view, move.special, targetSpecial);
      if (move.range != MoveRange::Area) {
        continue;
      }
      const std::int32_t living = view.state->livingEnemies();
      const bool crowd = self.stats.brain >= rules.areaBrain && living >= rules.areaMinEnemies;
      if (crowd) {
        weights.at(slot) +=
            living * rules.areaWeightPerEnemy.at(static_cast<std::size_t>(self.chargeMode));
        continue;
      }
      const bool lowHp = self.hp * Hundred / std::max(1, self.stats.maxHp) < rules.lowHpPercent;
      if (self.chargeMode == ChargeMode::Cautious && lowHp) {
        if (view.rules->quirks.on(quirk::LowHpWeightsLoopBug)) {
          // The original reuses the slot's index inside: it writes one value over the ranking it
          // already sorted, ranks again, checks the slots instead of the ranked keys, and leaves
          // the outer loop.
          ranking.values.at(slot) = moveIn(slot).distanceSquared > INT32_MAX
                                        ? INT32_MAX
                                        : static_cast<std::int32_t>(moveIn(slot).distanceSquared);
          sortRanking(ranking, true);
          for (std::size_t place = 0; place < TechniqueSlots; ++place) {
            if (!empty(place) && usable.at(place)) {
              weights.at(ranking.keys.at(place)) = rules.reachWeights.at(ranking.ranks.at(place));
            }
          }
          break;
        }
        Ranking reach = unsortedRanking();
        for (std::size_t other = 0; other < TechniqueSlots; ++other) {
          reach.values.at(other) = usable.at(other)
                                       ? static_cast<std::int32_t>(std::min<std::int64_t>(
                                             moveIn(other).distanceSquared, INT32_MAX))
                                       : UnrankedHigh;
        }
        sortRanking(reach, true);
        for (std::size_t place = 0; place < TechniqueSlots; ++place) {
          const std::size_t key = reach.keys.at(place);
          if (!empty(key) && usable.at(key)) {
            weights.at(key) = rules.reachWeights.at(reach.ranks.at(place));
          }
        }
      }
    }

    const std::size_t picked = weightedPick(*view.random, weights);
    // A roll that finds no weight ends past the techniques, on the finisher's slot.
    if (picked < EquippedSlots && self.moves.at(picked).has_value()) {
      return {.slot = picked};
    }
    return {.slot = std::nullopt};
  }

  template <typename Empty>
  static void rankWeights(const Ranking& ranking, const std::array<std::int32_t, 4>& byRank,
                          const std::array<bool, EquippedSlots>& usable, const Empty& empty,
                          std::int32_t emptyWeight,
                          std::array<std::int32_t, TechniqueSlots>& weights) {
    for (std::size_t place = 0; place < TechniqueSlots; ++place) {
      const std::size_t key = ranking.keys.at(place);
      if (empty(key)) {
        weights.at(key) += emptyWeight;
      } else if (usable.at(key)) {
        weights.at(key) = byRank.at(ranking.ranks.at(place));
      }
    }
  }

  // battle_main.c: BTL_calculateElementBonus.
  static std::int32_t elementBonus(const BattleView& view, std::int32_t moveSpecial,
                                   std::int32_t defenderSpecial) {
    return mapAffinity(view.rules->affinity.moveWeightBonus,
                       affinityOf(view.rules->affinity, moveSpecial, defenderSpecial));
  }
};

} // namespace

std::unique_ptr<PartnerAi> makeOriginalPartnerAi() {
  return std::make_unique<OriginalPartnerAi>();
}

} // namespace dwe::game::battle
