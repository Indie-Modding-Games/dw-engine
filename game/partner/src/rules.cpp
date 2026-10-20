#include <dwe/game/partner/rules.hpp>

#include <memory>
#include <string>
#include <utility>

namespace dwe::game::partner {

core::Result<PartnerRules> loadPartnerRules(const ruleset::Ruleset& ruleset,
                                            const PartnerStrategyRegistries& registries) {
    auto care = ruleset.requireSection(CareSection);
    if (!care) {
        return std::unexpected{care.error()};
    }
    auto life = ruleset.requireSection(LifeSection);
    if (!life) {
        return std::unexpected{life.error()};
    }
    auto training = ruleset.requireSection(TrainingSection);
    if (!training) {
        return std::unexpected{training.error()};
    }
    auto careRules = readCareRules(**care);
    if (!careRules) {
        return std::unexpected{careRules.error()};
    }
    auto lifeRules = readLifeRules(**life);
    if (!lifeRules) {
        return std::unexpected{lifeRules.error()};
    }
    auto trainingRules = readTrainingRules(**training);
    if (!trainingRules) {
        return std::unexpected{trainingRules.error()};
    }
    auto strategies = createStrategies(registries, **care, **life, **training);
    if (!strategies) {
        return std::unexpected{strategies.error()};
    }
    return PartnerRules{.care = std::move(*careRules),
                        .life = std::move(*lifeRules),
                        .training = std::move(*trainingRules),
                        .strategies = std::move(*strategies)};
}

ruleset::SectionChecks partnerSectionChecks(const std::shared_ptr<const PartnerStrategyRegistries>& registries) {
    ruleset::SectionChecks checks;
    checks.emplace(std::string{CareSection}, [registries](const ruleset::Section& section) -> core::Result<void> {
        auto rules = readCareRules(section);
        if (!rules) {
            return std::unexpected{rules.error()};
        }
        if (auto checked = registries->careMistakes.check(section, CareMistakesPath); !checked) {
            return checked;
        }
        if (auto checked = registries->sicknessRolls.check(section, SicknessRollPath); !checked) {
            return checked;
        }
        return registries->itemRefusals.check(section, ItemRefusalPath);
    });
    checks.emplace(std::string{LifeSection}, [registries](const ruleset::Section& section) -> core::Result<void> {
        auto rules = readLifeRules(section);
        if (!rules) {
            return std::unexpected{rules.error()};
        }
        return registries->rebirthChoices.check(section, RebirthChoicePath);
    });
    checks.emplace(std::string{TrainingSection}, [registries](const ruleset::Section& section) -> core::Result<void> {
        auto rules = readTrainingRules(section);
        if (!rules) {
            return std::unexpected{rules.error()};
        }
        return registries->gainFormulas.check(section, GainFormulaPath);
    });
    return checks;
}

} // namespace dwe::game::partner
