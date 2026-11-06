#pragma once

#include <dwe/game/gym/gym_rules.hpp>
#include <dwe/session/session_system.hpp>

namespace dwe::session {

// The town gym's rules in the session: their section check, and the rules read after each accepted
// ruleset change. The courses themselves run in the game, which owns the field they move.
class GymAdapter final : public SessionSystem {
public:
  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;

  [[nodiscard]] const game::gym::GymRules& rules() const { return rules_; }

private:
  game::gym::GymRules rules_;
};

} // namespace dwe::session
