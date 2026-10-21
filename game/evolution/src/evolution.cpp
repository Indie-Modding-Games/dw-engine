#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/messages.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <algorithm>
#include <utility>

#include "evolution_effects.hpp"

namespace dwe::game::evolution {

namespace {

// The checks that every form and item the rules name is in the tables.
class RuleIdCheck {
public:
    explicit RuleIdCheck(const EvolutionTables& tables) : tables_(&tables) {}

    void form(std::string_view id, std::string_view where) {
        if (!error_ && !tables_->find(id)) {
            error_ = core::Error{messages::formatMessage(messages::UnknownForm, where, id)};
        }
    }

    void forms(const std::vector<std::string>& ids, std::string_view where) {
        for (const std::string& id : ids) {
            form(id, where);
        }
    }

    [[nodiscard]] const std::optional<core::Error>& error() const { return error_; }

private:
    const EvolutionTables* tables_;
    std::optional<core::Error> error_;
};

std::optional<core::Error> checkRuleIds(const EvolutionRules& rules, const EvolutionTables& tables) {
    RuleIdCheck check{tables};
    for (const auto& [baby, target] : rules.babyTargets) {
        check.form(baby, messages::BabyTargetsWhere);
        check.form(target, messages::BabyTargetsWhere);
    }
    check.form(rules.fallback.rookieTarget, messages::FallbackWhere);
    for (const auto* specials : {&rules.specials.wakeUp, &rules.specials.praiseScold}) {
        for (const SpecialRule& special : *specials) {
            check.form(special.target, messages::SpecialWhere);
            check.forms(special.from, messages::SpecialWhere);
        }
    }
    check.form(rules.virus.target, messages::VirusWhere);
    for (const auto& [item, target] : rules.items.fixed) {
        check.form(target, messages::ItemsWhere);
    }
    check.forms(rules.statGains.scaledTargets, messages::ScaledWhere);
    check.forms(rules.rebirth.babies, messages::RebirthWhere);
    return check.error();
}

std::int32_t fieldValue(SpecialField field, const EvolutionInputs& inputs, const EvolutionTables& tables,
                        const PraiseScold& event) {
    switch (field) {
    case SpecialField::Level:
        return static_cast<std::int32_t>(tables.levelOf(inputs.species));
    case SpecialField::EvolutionTimer:
        return inputs.evolutionTimer;
    case SpecialField::Discipline:
        return inputs.discipline;
    case SpecialField::Happiness:
        return inputs.happiness;
    case SpecialField::Tiredness:
        return inputs.tiredness;
    case SpecialField::Battles:
        return inputs.battles;
    case SpecialField::Defense:
        return inputs.stats.defense;
    case SpecialField::Map:
        return inputs.map;
    case SpecialField::DisciplineBefore:
        return event.disciplineBefore;
    case SpecialField::HappinessBefore:
        return event.happinessBefore;
    }
    return 0;
}

bool needsEvent(SpecialField field) {
    return field == SpecialField::DisciplineBefore || field == SpecialField::HappinessBefore;
}

bool compares(const SpecialRequirement& requirement, std::int32_t value) {
    return requirement.comparison == Comparison::Equals ? value == requirement.value : value >= requirement.value;
}

} // namespace

Evolution::Evolution(EvolutionRules rules, EvolutionTables tables, std::unique_ptr<EvolutionSelection> selection,
                     std::unique_ptr<RequirementScoring> scoring)
    : rules_(std::move(rules)), tables_(std::move(tables)), selection_(std::move(selection)),
      scoring_(std::move(scoring)) {}

core::Result<Evolution> Evolution::create(EvolutionRules rules, EvolutionTables tables,
                                          std::unique_ptr<EvolutionSelection> selection,
                                          std::unique_ptr<RequirementScoring> scoring) {
    if (auto problem = checkRuleIds(rules, tables); problem) {
        return std::unexpected{*problem};
    }
    return Evolution{std::move(rules), std::move(tables), std::move(selection), std::move(scoring)};
}

core::Result<Evolution> Evolution::load(const assets::VirtualFileSystem& vfs, const ruleset::Ruleset& ruleset) {
    auto section = ruleset.requireSection(EvolutionSection);
    if (!section) {
        return std::unexpected{section.error()};
    }
    auto rules = readEvolutionRules(**section);
    if (!rules) {
        return std::unexpected{rules.error()};
    }
    auto selection = selectionStrategies().create(**section, SelectionKey);
    if (!selection) {
        return std::unexpected{selection.error()};
    }
    auto scoring = scoringStrategies().create(**section, ScoringKey);
    if (!scoring) {
        return std::unexpected{scoring.error()};
    }
    auto tables = EvolutionTables::load(vfs, ruleset);
    if (!tables) {
        return std::unexpected{tables.error()};
    }
    return create(std::move(*rules), std::move(*tables), std::move(*selection), std::move(*scoring));
}

std::optional<std::int32_t> Evolution::checkHours(Level level) const {
    switch (level) {
    case Level::Baby:
        return rules_.checkHours.baby;
    case Level::InTraining:
        return rules_.checkHours.inTraining;
    case Level::Rookie:
        return rules_.checkHours.rookie;
    case Level::Champion:
        return rules_.checkHours.champion;
    case Level::None:
    case Level::Ultimate:
        return std::nullopt;
    }
    return std::nullopt;
}

Species Evolution::require(std::string_view id) const {
    return tables_.find(id).value_or(0);
}

std::optional<Species> Evolution::naturalTarget(const EvolutionInputs& inputs, const EvolutionState& state) const {
    const auto hours = checkHours(tables_.levelOf(inputs.species));
    if (!hours || inputs.evolutionTimer < *hours) {
        return std::nullopt;
    }
    return selection_->select({.tables = &tables_, .rules = &rules_, .inputs = &inputs, .state = &state}, *scoring_);
}

// evolution.c: handleSpecialEvolutions: one roll for every rule; the last rule that holds wins.
std::optional<Species> Evolution::specialTarget(const std::vector<SpecialRule>& specials, const EvolutionInputs& inputs,
                                                const std::optional<PraiseScold>& event,
                                                ruleset::Random& random) const {
    if (!inputs.tamerFree) {
        return std::nullopt;
    }
    const std::int32_t roll = random.below(rules_.specials.rollLimit);
    const PraiseScold noEvent{};
    std::optional<Species> target;
    for (const SpecialRule& special : specials) {
        const bool rolled = !special.rollBelow || roll < *special.rollBelow;
        const bool fromMatches =
            special.from.empty() || std::ranges::any_of(special.from, [&](const std::string& from) {
                return tables_.find(from) == inputs.species;
            });
        const bool scoldMatches = !special.scolded || (event && event->scolded);
        const bool requirementsHold = std::ranges::all_of(special.requirements, [&](const SpecialRequirement& need) {
            return (!needsEvent(need.field) || event) &&
                   compares(need, fieldValue(need.field, inputs, tables_, event.value_or(noEvent)));
        });
        if (rolled && fromMatches && scoldMatches && requirementsHold) {
            target = require(special.target);
        }
    }
    return target;
}

std::optional<Species> Evolution::timerSpecialTarget(const EvolutionInputs& inputs, std::optional<Species> normal,
                                                     ruleset::Random& random) const {
    const TimerSpecialRule& onTimer = rules_.specials.praiseScoldOnTimer;
    if (static_cast<std::int32_t>(tables_.levelOf(inputs.species)) != onTimer.level ||
        inputs.evolutionTimer != onTimer.hours) {
        return normal;
    }
    const auto special = specialTarget(rules_.specials.praiseScold, inputs, std::nullopt, random);
    if (special || rules_.quirks.on(TimerSpecialOverridesNormal)) {
        return special;
    }
    return normal;
}

NaturalCheck Evolution::checkNatural(const EvolutionInputs& inputs, const EvolutionState& state,
                                     std::int64_t hoursCrossed, ruleset::Random& random) const {
    const Level level = tables_.levelOf(inputs.species);
    const std::int32_t timer = inputs.evolutionTimer;
    const std::int64_t earlierHours =
        std::min<std::int64_t>(std::max<std::int64_t>(hoursCrossed - 1, 0), std::max(timer, 0));
    const Species virusForm = require(rules_.virus.target);
    for (auto hour = timer - static_cast<std::int32_t>(earlierHours); hour <= timer; ++hour) {
        EvolutionInputs atHour = inputs;
        atHour.evolutionTimer = hour;
        std::optional<Species> target;
        EvolutionReason reason = EvolutionReason::Normal;
        if (inputs.tamerFree) {
            const auto normal = naturalTarget(atHour, state);
            target = timerSpecialTarget(atHour, normal, random);
            if (target != normal) {
                reason = EvolutionReason::Special;
            }
        }
        // partner_impl.c: tickConditions: the virus bar is checked last, wins, and does not wait for the tamer.
        if (inputs.virusBar >= rules_.virus.barLimit && inputs.species != virusForm) {
            target = virusForm;
            reason = EvolutionReason::Virus;
        }
        if (target) {
            if (auto evolution = evolve(atHour, state, *target, reason); evolution) {
                // The hours crossed after the one it evolved at still count.
                evolution->evolutionTimer += timer - hour;
                return {.evolutionTimer = evolution->evolutionTimer, .evolution = evolution};
            }
        }
        // evolution.c: getFreshEvolutionTarget sets the counter to 0 for any baby it is asked about.
        const auto babyHours = checkHours(Level::Baby);
        if (inputs.tamerFree && level == Level::Baby && babyHours && hour >= *babyHours &&
            rules_.quirks.on(UnlistedBabyRestartsTimer)) {
            return {.evolutionTimer = timer - hour, .evolution = std::nullopt};
        }
    }
    return {.evolutionTimer = timer, .evolution = std::nullopt};
}

std::optional<EvolutionResult> Evolution::wakeUp(const EvolutionInputs& inputs, const EvolutionState& state,
                                                 ruleset::Random& random) const {
    const auto target = specialTarget(rules_.specials.wakeUp, inputs, std::nullopt, random);
    if (!target) {
        return std::nullopt;
    }
    return evolve(inputs, state, *target, EvolutionReason::Special);
}

std::optional<EvolutionResult> Evolution::praiseOrScold(const EvolutionInputs& inputs, const EvolutionState& state,
                                                        const PraiseScold& event, ruleset::Random& random) const {
    const auto target = specialTarget(rules_.specials.praiseScold, inputs, event, random);
    if (!target) {
        return std::nullopt;
    }
    return evolve(inputs, state, *target, EvolutionReason::Special);
}

std::optional<EvolutionResult> Evolution::useItem(const EvolutionInputs& inputs, const EvolutionState& state,
                                                  std::string_view item) const {
    if (const auto fixed = rules_.items.fixed.find(item); fixed != rules_.items.fixed.end()) {
        return evolve(inputs, state, require(fixed->second), EvolutionReason::Item);
    }
    const auto target = tables_.itemTarget(item);
    if (!target) {
        return std::nullopt;
    }
    const auto targetLevel = static_cast<std::int32_t>(tables_.levelOf(*target));
    if (targetLevel - rules_.items.levelStep != static_cast<std::int32_t>(tables_.levelOf(inputs.species))) {
        return std::nullopt;
    }
    return evolve(inputs, state, *target, EvolutionReason::Item);
}

std::optional<EvolutionResult> Evolution::returnFromVirus(const EvolutionInputs& inputs,
                                                          const EvolutionState& state) const {
    if (!inputs.virusBackup) {
        return std::nullopt;
    }
    return evolve(inputs, state, inputs.virusBackup->species, EvolutionReason::Script);
}

std::optional<EvolutionResult> Evolution::evolveByScript(const EvolutionInputs& inputs, const EvolutionState& state,
                                                         Species target) const {
    return evolve(inputs, state, target, EvolutionReason::Script);
}

std::optional<EvolutionResult> Evolution::rebirth(const EvolutionInputs& inputs, ruleset::Random& random) const {
    const std::vector<std::string>& babies = rules_.rebirth.babies;
    if (babies.empty()) {
        return std::nullopt;
    }
    const auto egg = static_cast<std::size_t>(random.below(static_cast<std::int32_t>(babies.size())));
    const Species baby = require(babies.at(egg));
    const StatReference* reference = tables_.statReference(baby);
    return EvolutionEffects::rebirth(inputs, reference == nullptr ? baby : reference->loads);
}

std::optional<EvolutionResult> Evolution::evolve(const EvolutionInputs& inputs, const EvolutionState& state,
                                                 Species target, EvolutionReason reason) const {
    const StatReference* reference = tables_.statReference(target);
    if (reference == nullptr || tables_.species(reference->loads) == nullptr) {
        return std::nullopt;
    }
    const EvolutionEffects effects{rules_, tables_};
    return effects.apply(inputs, state, target, *reference, reason);
}

} // namespace dwe::game::evolution
