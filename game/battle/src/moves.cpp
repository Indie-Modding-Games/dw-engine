#include <dwe/assets/move_table.hpp>
#include <dwe/game/battle/messages.hpp>
#include <dwe/game/battle/moves.hpp>

#include <algorithm>
#include <utility>

namespace dwe::game::battle {

namespace {

core::Result<MoveRef> requireMove(const MoveCatalog& catalog, std::string_view id) {
    const auto found = catalog.find(id);
    if (!found) {
        return std::unexpected{core::Error{messages::formatMessage(messages::RuleMoveMissing, id)}};
    }
    return *found;
}

BattleMove battleMoveOf(const assets::MoveEntry& entry, const MoveRules& rules) {
    const bool finisher = entry.index >= rules.finisherFirstIndex && entry.index <= rules.finisherLastIndex;
    BattleMove move{.id = entry.id,
                    .index = entry.index,
                    .power = entry.power,
                    .mpCost = entry.mpCost * rules.mpCostMultiplier,
                    .distanceSquared = entry.distance,
                    .range = static_cast<MoveRange>(std::clamp(entry.range, 0, static_cast<int>(MoveRange::Self))),
                    .special = entry.special,
                    .status =
                        static_cast<StatusEffect>(std::clamp(entry.status, 0, static_cast<int>(StatusEffect::Flatten))),
                    .accuracy = entry.accuracy,
                    .statusChance = entry.statusChance,
                    .iframes = entry.iframes,
                    .needsTarget = (entry.flags & NeedsTargetFlag) != 0,
                    .sharedCooldown = (entry.flags & SharedCooldownFlag) != 0,
                    .finisher = finisher,
                    .counter = entry.id == rules.counter,
                    .learnable = entry.index < rules.learnableBelowIndex,
                    .learnChances = entry.learnChances,
                    .buff = std::nullopt};
    const auto buff = std::ranges::find(rules.buffs, entry.id, &BuffRule::move);
    if (buff != rules.buffs.end()) {
        move.buff = BuffPercents{.offense = buff->offense, .defense = buff->defense, .speed = buff->speed};
    }
    return move;
}

} // namespace

core::Result<MoveCatalog> MoveCatalog::build(const assets::MoveTable& table, const BattleRules& rules) {
    MoveCatalog catalog;
    catalog.moves_.reserve(table.moves.size());
    for (const assets::MoveEntry& entry : table.moves) {
        catalog.moves_.push_back(battleMoveOf(entry, rules.moves));
    }
    for (const BuffRule& buff : rules.moves.buffs) {
        if (auto found = requireMove(catalog, buff.move); !found) {
            return std::unexpected{found.error()};
        }
    }
    if (auto counter = requireMove(catalog, rules.moves.counter); !counter) {
        return std::unexpected{counter.error()};
    }
    auto shot = requireMove(catalog, rules.moves.flattenedShot);
    if (!shot) {
        return std::unexpected{shot.error()};
    }
    catalog.flattenedShot_ = *shot;
    for (const auto& [first, second] : rules.moves.learnedTogether) {
        auto a = requireMove(catalog, first);
        auto b = requireMove(catalog, second);
        if (!a || !b) {
            return std::unexpected{!a ? a.error() : b.error()};
        }
        catalog.learnedTogether_.emplace_back(*a, *b);
    }
    return catalog;
}

std::optional<MoveRef> MoveCatalog::find(std::string_view id) const {
    const auto found = std::ranges::find(moves_, id, &BattleMove::id);
    if (found == moves_.end()) {
        return std::nullopt;
    }
    return static_cast<MoveRef>(found - moves_.begin());
}

std::vector<MoveRef> MoveCatalog::learnedWith(MoveRef move) const {
    std::vector<MoveRef> together;
    for (const auto& [first, second] : learnedTogether_) {
        if (first == move) {
            together.push_back(second);
        } else if (second == move) {
            together.push_back(first);
        }
    }
    return together;
}

} // namespace dwe::game::battle
