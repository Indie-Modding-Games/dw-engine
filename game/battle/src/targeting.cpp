#include "targeting.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <numeric>

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;
constexpr std::int32_t ScoreDivisor = 10;
// BTL_getWeakestEnemy starts from a score no fighter reaches.
constexpr std::int32_t NoScore = 1000;

} // namespace

std::optional<std::int32_t> firstSpecial(const Fighter& fighter) {
    return fighter.combatant.specials.front();
}

std::int32_t targetScore(const BattleView& view, FighterId self, FighterId other) {
    const Fighter& me = view.state->fighters.at(self);
    const Fighter& them = view.state->fighters.at(other);
    const AffinityRules& affinity = view.rules->affinity;
    const std::int32_t factor = affinityOf(affinity, firstSpecial(me).value_or(-1), firstSpecial(them).value_or(-1));
    const std::int32_t mapped = mapAffinity(affinity.targetScore, factor);
    const Fighter& scored = view.rules->quirks.on(quirk::TargetScoreUsesOwnStats) ? me : them;
    const std::int32_t hpPercent = scored.hp * Hundred / std::max(1, scored.stats.maxHp);
    return hpPercent + (mapped * (scored.stats.defense * Hundred / view.rules->limits.statCap) / ScoreDivisor);
}

WeakestChoice weakestOther(const BattleView& view, FighterId self) {
    const Fighter& me = view.state->fighters.at(self);
    WeakestChoice choice;
    std::int32_t best = NoScore;
    for (FighterId id = 0; id < view.state->fighters.size(); ++id) {
        if (id == self || view.state->fighters[id].defeated() || me.target == id) {
            continue;
        }
        const std::int32_t score = targetScore(view, self, id);
        if (view.rules->quirks.on(quirk::WeakestReportsLastScore)) {
            choice.score = score;
        }
        if (score < best) {
            best = score;
            choice.fighter = id;
            if (!view.rules->quirks.on(quirk::WeakestReportsLastScore)) {
                choice.score = score;
            }
        }
    }
    return choice;
}

std::vector<FighterId> afflictedOthers(const BattleView& view, FighterId self) {
    std::vector<FighterId> afflicted;
    for (FighterId id = 0; id < view.state->fighters.size(); ++id) {
        const Fighter& other = view.state->fighters[id];
        if (id != self && !other.defeated() && other.status.any()) {
            afflicted.push_back(id);
        }
    }
    return afflicted;
}

std::optional<FighterId> nearestOther(const BattleState& state, FighterId self) {
    std::optional<FighterId> nearest;
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    for (FighterId id = 0; id < state.fighters.size(); ++id) {
        if (id == self || state.fighters[id].defeated()) {
            continue;
        }
        const std::int64_t distance = distanceSquared(state.fighters[self].position, state.fighters[id].position);
        if (distance < best) {
            best = distance;
            nearest = id;
        }
    }
    return nearest;
}

std::size_t weightedPick(ruleset::Random& random, std::span<const std::int32_t> weights) {
    const std::int32_t total = std::accumulate(weights.begin(), weights.end(), 0);
    const std::int32_t roll = random.below(total);
    std::int32_t reached = 0;
    for (std::size_t index = 0; index < weights.size(); ++index) {
        if (weights[index] != 0) {
            reached += weights[index];
            if (roll < reached) {
                return index;
            }
        }
    }
    return weights.size();
}

} // namespace dwe::game::battle
