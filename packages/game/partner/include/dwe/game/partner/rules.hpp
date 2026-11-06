#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/care_rules.hpp>
#include <dwe/game/partner/life_rules.hpp>
#include <dwe/game/partner/strategies.hpp>
#include <dwe/game/partner/training_rules.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <memory>

namespace dwe::game::partner {

// Everything the partner reads from a ruleset: the care, life and training sections and the
// strategies they pick.
struct PartnerRules {
  CareRules care;
  LifeRules life;
  TrainingRules training;
  PartnerStrategies strategies;
};

// Reads the partner's sections of a ruleset and builds their strategies.
core::Result<PartnerRules> loadPartnerRules(const ruleset::Ruleset& ruleset,
                                            const PartnerStrategyRegistries& registries = {});

// The checks of the partner's sections, for the RulesetStore: each reads its section and its
// strategy names, so a load, reload or tuned value the partner would not accept is refused. The
// checks keep the registries.
ruleset::SectionChecks
partnerSectionChecks(const std::shared_ptr<const PartnerStrategyRegistries>& registries =
                         std::make_shared<const PartnerStrategyRegistries>());

} // namespace dwe::game::partner
