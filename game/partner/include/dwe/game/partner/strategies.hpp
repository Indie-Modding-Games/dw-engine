#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/care_rules.hpp>
#include <dwe/game/partner/effects.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/life_rules.hpp>
#include <dwe/game/partner/species.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/game/partner/training_rules.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/ruleset/strategy.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace dwe::game::partner {

// The rules of the partner that are logic rather than numbers; the sections name the implementation to use
// (docs/architecture/rulesets.md, "Estrategias"). `original` is the original game's behaviour.
inline constexpr std::string_view OriginalStrategy = "original";

// The base of the partner's strategies: owned through a pointer, never copied.
class PartnerStrategy {
public:
    PartnerStrategy() = default;
    PartnerStrategy(const PartnerStrategy&) = delete;
    PartnerStrategy& operator=(const PartnerStrategy&) = delete;
    PartnerStrategy(PartnerStrategy&&) = delete;
    PartnerStrategy& operator=(PartnerStrategy&&) = delete;
    virtual ~PartnerStrategy() = default;
};

// Whether a piece of care the tamer missed counts as a care mistake.
class CareMistakeRule : public PartnerStrategy {
public:
    [[nodiscard]] virtual bool counts(CareMistakeCause cause, const PartnerState& state,
                                      const SpeciesCare& species) const = 0;
};

// Whether staying awake makes the partner sick this hour (tickSicknessMechanics). The tries are already counted.
class SicknessRoll : public PartnerStrategy {
public:
    [[nodiscard]] virtual bool fallsSick(const PartnerState& state, const CareRules& rules,
                                         ruleset::Random& random) const = 0;
};

// Whether the partner refuses an offered item (partnerWillRefuseItem). It may set the refusal marks of the state
// (the refused favourite food, the boosted scold).
class ItemRefusalRule : public PartnerStrategy {
public:
    [[nodiscard]] virtual bool refuses(const ItemOffer& offer, PartnerState& state, const SpeciesCare& species,
                                       const CareRules& rules, ruleset::Random& random) const = 0;
};

// Which baby the partner is reborn as: an index into the babies.
class RebirthChoice : public PartnerStrategy {
public:
    [[nodiscard]] virtual std::size_t pick(std::span<const RebirthBaby> babies, const PartnerState& deceased,
                                           ruleset::Random& random) const = 0;
};

// What a training session needs to work out its gains.
struct TrainingGainInputs {
    std::reference_wrapper<const TrainingRules> rules;
    std::reference_wrapper<const TrainingCourse> course;
    std::reference_wrapper<const PartnerState> state;
    std::reference_wrapper<const SpeciesCare> species;
    std::reference_wrapper<const TrainingRequest> request;
};

// The stats a training session raises (TRN_calculateTrainingMultiplier, TRN_award*TrainingGains), before the cap
// of every gain (care.limits.stat_gain_max) that the partner applies.
class TrainingGainFormula : public PartnerStrategy {
public:
    [[nodiscard]] virtual StatBlock gains(const TrainingGainInputs& inputs) const = 0;
};

// Where the sections name each strategy.
inline constexpr std::string_view CareMistakesPath = "strategies.care_mistakes";
inline constexpr std::string_view SicknessRollPath = "strategies.sickness_roll";
inline constexpr std::string_view ItemRefusalPath = "strategies.item_refusal";
inline constexpr std::string_view RebirthChoicePath = "rebirth.baby_choice";
inline constexpr std::string_view GainFormulaPath = "gain_formula";

// The implementations of each strategy by name, with `original` registered; a mod or a test adds its own.
struct PartnerStrategyRegistries {
    PartnerStrategyRegistries();

    ruleset::StrategyRegistry<CareMistakeRule> careMistakes;
    ruleset::StrategyRegistry<SicknessRoll> sicknessRolls;
    ruleset::StrategyRegistry<ItemRefusalRule> itemRefusals;
    ruleset::StrategyRegistry<RebirthChoice> rebirthChoices;
    ruleset::StrategyRegistry<TrainingGainFormula> gainFormulas;
};

// The strategies a ruleset picks.
struct PartnerStrategies {
    std::unique_ptr<CareMistakeRule> careMistakes;
    std::unique_ptr<SicknessRoll> sicknessRoll;
    std::unique_ptr<ItemRefusalRule> itemRefusal;
    std::unique_ptr<RebirthChoice> rebirthChoice;
    std::unique_ptr<TrainingGainFormula> gainFormula;
};

// Builds the strategies the care, life and training sections name.
core::Result<PartnerStrategies> createStrategies(const PartnerStrategyRegistries& registries,
                                                 const ruleset::Section& care, const ruleset::Section& life,
                                                 const ruleset::Section& training);

} // namespace dwe::game::partner
