#include <dwe/game/partner/messages.hpp>
#include <dwe/game/partner/strategies.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace dwe::game::partner {

namespace {

constexpr std::int32_t PercentRoll = 100;
// The original keeps the sickness chance in a byte (tickSicknessMechanics: `u8 chance`).
constexpr std::int32_t ByteRange = 256;
// Gains and multipliers are in tenths.
constexpr std::int32_t Tenths = 10;
constexpr std::int32_t Hundredths = 100;

// The original: a missed hunger window counts only when the partner did not eat enough before it closed
// (tickHungerMechanics: `energyLevel < energyThreshold`); a missed sleep and a poop on the ground always count
// (tickSleepMechanics, handleWildPoop).
class OriginalCareMistakes final : public CareMistakeRule {
public:
    [[nodiscard]] bool counts(CareMistakeCause cause, const PartnerState& state,
                              const SpeciesCare& species) const override {
        if (cause == CareMistakeCause::HungerWindow) {
            return state.hunger.energy < species.energyThreshold;
        }
        return true;
    }
};

// The original (tickSicknessMechanics): (tries - 10) * 5 + (tiredness - 50) percent, kept in a byte so that a
// negative chance wraps to a high one (the quirk sickness_chance_wraps).
class OriginalSicknessRoll final : public SicknessRoll {
public:
    [[nodiscard]] bool fallsSick(const PartnerState& state, const CareRules& rules,
                                 ruleset::Random& random) const override {
        const SicknessRules& sickness = rules.sickness;
        std::int32_t chance = ((state.health.sicknessTries - sickness.triesOffset) * sickness.triesPercent) +
                              (state.mood.tiredness - sickness.tirednessOffset);
        if (rules.quirks.sicknessChanceWraps) {
            chance = ((chance % ByteRange) + ByteRange) % ByteRange;
        } else {
            chance = std::max(chance, 0);
        }
        return random.below(PercentRoll) < chance;
    }
};

// The original (partner_impl.c: partnerWillRefuseItem).
class OriginalItemRefusal final : public ItemRefusalRule {
public:
    [[nodiscard]] bool refuses(const ItemOffer& offer, PartnerState& state, const SpeciesCare& species,
                               const CareRules& rules, ruleset::Random& random) const override {
        // The original rolls the favourite food's chance before anything else.
        const bool favoriteRoll = random.below(PercentRoll) < rules.refusal.favoriteRefusalPercent;
        if (offer.check == RefusalCheck::Always) {
            return true;
        }
        if (state.mood.scoldBoost == ScoldBoost::Used) {
            state.mood.scoldBoost = ScoldBoost::None;
            return false;
        }
        if (offer.check == RefusalCheck::Discipline) {
            return refusesForDiscipline(state, rules.refusal, random);
        }
        const bool hungry = state.conditions.has(Condition::Hungry);
        if (!hungry && species.favoriteFood == offer.item && favoriteRoll) {
            state.hunger.refusedFavorite = true;
        }
        if (!state.hunger.refusedFavorite) {
            return false;
        }
        if (hungry) {
            state.mood.scoldBoost = ScoldBoost::Ready;
        }
        return true;
    }

private:
    static bool refusesForDiscipline(PartnerState& state, const RefusalRules& refusal, ruleset::Random& random) {
        const std::int32_t roll = random.below(PercentRoll);
        const std::int32_t bonus = random.below(refusal.disciplineBonusRange) + refusal.disciplineBonusMin;
        if (roll >= refusal.disciplineBase - (state.mood.discipline + bonus)) {
            return false;
        }
        state.mood.scoldBoost = ScoldBoost::Ready;
        return true;
    }
};

// The original (dooa.c: the egg is `rand() % 4`): any baby, evenly.
class OriginalRebirthChoice final : public RebirthChoice {
public:
    [[nodiscard]] std::size_t pick(std::span<const RebirthBaby> babies, const PartnerState& /*deceased*/,
                                   ruleset::Random& random) const override {
        return static_cast<std::size_t>(random.below(static_cast<std::int32_t>(babies.size())));
    }
};

// The original (trn_reward.c: TRN_calculateTrainingMultiplier, TRN_award*TrainingGains).
class OriginalGainFormula final : public TrainingGainFormula {
public:
    [[nodiscard]] StatBlock gains(const TrainingGainInputs& inputs) const override {
        const std::int32_t multiplier = multiplierTenths(inputs);
        const TrainingBoost& boost = inputs.state.get().training.boost;
        const std::int32_t slot = inputs.request.get().slotTenths > 0 ? inputs.request.get().slotTenths
                                                                      : inputs.rules.get().slotDefaultTenths;
        StatBlock gains;
        for (const CourseGain& gain : inputs.course.get().gains) {
            std::int32_t value = 0;
            if (boost.frames > 0 && boost.stats.contains(gain.boostedBy)) {
                value = gain.base * (multiplier * boost.value) / Hundredths;
            } else {
                value = gain.base * multiplier / Tenths;
            }
            value = value * slot / Tenths;
            if (inputs.state.get().mood.tiredness >= inputs.rules.get().tiredOutTiredness) {
                value = inputs.rules.get().tiredOutGain;
            }
            gains[gain.stat] += std::min(value, inputs.rules.get().gainCap);
        }
        return gains;
    }

private:
    // The multiplier in tenths: the species' aptitude for the stat times its level's factor, with the bonuses.
    static std::int32_t multiplierTenths(const TrainingGainInputs& inputs) {
        const TrainingRules& rules = inputs.rules.get();
        const Stat stat = rules.multiplierStat(inputs.course.get());
        const auto tier = std::min(static_cast<std::size_t>(std::max(inputs.species.get().trainingAptitude, 0)),
                                   AptitudeTierCount - 1);
        const auto level = std::min(
            static_cast<std::size_t>(std::max(static_cast<int>(inputs.species.get().level) - 1, 0)), LevelCount - 1);
        std::int32_t numerator =
            rules.aptitudeTenths.at(static_cast<std::size_t>(stat)).at(tier) * rules.levelTenths.at(level);
        std::int32_t denominator = Hundredths;
        const auto apply = [&](const Ratio& ratio) {
            numerator *= ratio.multiplier;
            denominator *= ratio.divisor;
        };
        if (inputs.course.get().practiceBonuses) {
            if (inputs.request.get().gymUpgraded) {
                apply(rules.upgradeBonus);
            }
            if (inputs.state.get().training.youngSessions[stat] >= rules.practiceSessions) {
                apply(rules.practiceBonus);
            }
        }
        if (inputs.request.get().hasTrainingManual) {
            apply(rules.manualBonus);
        }
        return numerator * Tenths / denominator;
    }
};

template <typename Interface, typename Implementation>
void addOriginal(ruleset::StrategyRegistry<Interface>& registry) {
    registry.add(std::string{OriginalStrategy}, [] { return std::make_unique<Implementation>(); });
}

} // namespace

PartnerStrategyRegistries::PartnerStrategyRegistries()
    : careMistakes(std::string{messages::CareMistakeRuleText}), sicknessRolls(std::string{messages::SicknessRollText}),
      itemRefusals(std::string{messages::ItemRefusalText}), rebirthChoices(std::string{messages::RebirthChoiceText}),
      gainFormulas(std::string{messages::GainFormulaText}) {
    addOriginal<CareMistakeRule, OriginalCareMistakes>(careMistakes);
    addOriginal<SicknessRoll, OriginalSicknessRoll>(sicknessRolls);
    addOriginal<ItemRefusalRule, OriginalItemRefusal>(itemRefusals);
    addOriginal<RebirthChoice, OriginalRebirthChoice>(rebirthChoices);
    addOriginal<TrainingGainFormula, OriginalGainFormula>(gainFormulas);
}

core::Result<PartnerStrategies> createStrategies(const PartnerStrategyRegistries& registries,
                                                 const ruleset::Section& care, const ruleset::Section& life,
                                                 const ruleset::Section& training) {
    auto careMistakes = registries.careMistakes.create(care, CareMistakesPath);
    if (!careMistakes) {
        return std::unexpected{careMistakes.error()};
    }
    auto sicknessRoll = registries.sicknessRolls.create(care, SicknessRollPath);
    if (!sicknessRoll) {
        return std::unexpected{sicknessRoll.error()};
    }
    auto itemRefusal = registries.itemRefusals.create(care, ItemRefusalPath);
    if (!itemRefusal) {
        return std::unexpected{itemRefusal.error()};
    }
    auto rebirthChoice = registries.rebirthChoices.create(life, RebirthChoicePath);
    if (!rebirthChoice) {
        return std::unexpected{rebirthChoice.error()};
    }
    auto gainFormula = registries.gainFormulas.create(training, GainFormulaPath);
    if (!gainFormula) {
        return std::unexpected{gainFormula.error()};
    }
    return PartnerStrategies{.careMistakes = std::move(*careMistakes),
                             .sicknessRoll = std::move(*sicknessRoll),
                             .itemRefusal = std::move(*itemRefusal),
                             .rebirthChoice = std::move(*rebirthChoice),
                             .gainFormula = std::move(*gainFormula)};
}

} // namespace dwe::game::partner
