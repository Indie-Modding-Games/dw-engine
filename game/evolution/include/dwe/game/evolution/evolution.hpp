#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/evolution/inputs.hpp>
#include <dwe/game/evolution/result.hpp>
#include <dwe/game/evolution/rules.hpp>
#include <dwe/game/evolution/state.hpp>
#include <dwe/game/evolution/strategies.hpp>
#include <dwe/game/evolution/tables.hpp>
#include <dwe/ruleset/random.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::ruleset {
class Ruleset;
} // namespace dwe::ruleset

namespace dwe::game::evolution {

// When and into what the partner evolves (docs/reference/evolution.md). It holds the rules, the tables and the
// strategies; every call is a pure question about the inputs it is given, and the session applies the result.
class Evolution {
public:
    // Builds it from rules and tables with the given strategies; every form and item the rules name must exist.
    static core::Result<Evolution> create(EvolutionRules rules, EvolutionTables tables,
                                          std::unique_ptr<EvolutionSelection> selection,
                                          std::unique_ptr<RequirementScoring> scoring);

    // Reads the evolution section and the tables of a ruleset and builds it with the strategies the section names.
    static core::Result<Evolution> load(const assets::VirtualFileSystem& vfs, const ruleset::Ruleset& ruleset);

    [[nodiscard]] const EvolutionRules& rules() const { return rules_; }
    [[nodiscard]] const EvolutionTables& tables() const { return tables_; }

    // The periodic check (partner_impl.c: tickConditions), for the session to call while the partner is in its
    // normal state, the clock runs and no map change or time-of-day fade is under way. `inputs.evolutionTimer` is the
    // counter now and `hoursCrossed` the hours it went up since the last call: the counter values in between are
    // checked one by one, so a fast-forward finds what playing would have. The virus bar is not part of it: the
    // partner reports a full bar and the session calls fillVirusBar, skipping this check in that tick.
    [[nodiscard]] NaturalCheck checkNatural(const EvolutionInputs& inputs, const EvolutionState& state,
                                            std::int64_t hoursCrossed, ruleset::Random& random) const;

    // The partner's virus bar filled up (partner_impl.c: tickConditions; the care section owns the limit and the
    // form, given in `inputs.virusForm`): into the virus form, whatever the tamer does; nothing when the partner is in
    // it already or the inputs name no form. The result keeps the form and stats to come back to.
    [[nodiscard]] std::optional<EvolutionResult> fillVirusBar(const EvolutionInputs& inputs,
                                                              const EvolutionState& state) const;

    // The special rules on waking up (handleSpecialEvolutions, mode 2).
    [[nodiscard]] std::optional<EvolutionResult> wakeUp(const EvolutionInputs& inputs, const EvolutionState& state,
                                                        ruleset::Random& random) const;

    // The special rules after a praise or a scold (handleSpecialEvolutions, mode 3).
    [[nodiscard]] std::optional<EvolutionResult> praiseOrScold(const EvolutionInputs& inputs,
                                                               const EvolutionState& state, const PraiseScold& event,
                                                               ruleset::Random& random) const;

    // Using an item on the partner (item.c: handleEvoItems): nothing when it is not an evolution item or does not
    // fit the partner's level.
    [[nodiscard]] std::optional<EvolutionResult> useItem(const EvolutionInputs& inputs, const EvolutionState& state,
                                                         std::string_view item) const;

    // Back from the virus form to the form kept when entering it (partner.c: callDigimonRoutine, routine 2): nothing
    // when the inputs carry no virus backup.
    [[nodiscard]] std::optional<EvolutionResult> returnFromVirus(const EvolutionInputs& inputs,
                                                                 const EvolutionState& state) const;

    // Into a form an event script names (partner.c: callDigimonRoutine, routine 10).
    [[nodiscard]] std::optional<EvolutionResult> evolveByScript(const EvolutionInputs& inputs,
                                                                const EvolutionState& state, Species target) const;

    // A new baby after death (dooa.c, evolution.c: reincarnatePartner): the egg is picked at random. Only the form,
    // the equipped moves and the raised mark come from here; the partner applies its own rebirth rules
    // (docs/reference/care.md), and the other fields repeat the inputs.
    [[nodiscard]] std::optional<EvolutionResult> rebirth(const EvolutionInputs& inputs, ruleset::Random& random) const;

private:
    Evolution(EvolutionRules rules, EvolutionTables tables, std::unique_ptr<EvolutionSelection> selection,
              std::unique_ptr<RequirementScoring> scoring);

    [[nodiscard]] std::optional<Species> naturalTarget(const EvolutionInputs& inputs,
                                                       const EvolutionState& state) const;
    [[nodiscard]] std::optional<Species> specialTarget(const std::vector<SpecialRule>& specials,
                                                       const EvolutionInputs& inputs,
                                                       const std::optional<PraiseScold>& event,
                                                       ruleset::Random& random) const;
    // At the special counter hour, the praise and scold rules replace or keep the normal target.
    [[nodiscard]] std::optional<Species>
    timerSpecialTarget(const EvolutionInputs& inputs, std::optional<Species> normal, ruleset::Random& random) const;
    [[nodiscard]] std::optional<std::int32_t> checkHours(Level level) const;
    [[nodiscard]] Species require(std::string_view id) const;

    // Applies EVL_applyEvolution's rules for a target.
    [[nodiscard]] std::optional<EvolutionResult> evolve(const EvolutionInputs& inputs, const EvolutionState& state,
                                                        Species target, EvolutionReason reason) const;

    EvolutionRules rules_;
    EvolutionTables tables_;
    std::unique_ptr<EvolutionSelection> selection_;
    std::unique_ptr<RequirementScoring> scoring_;
};

} // namespace dwe::game::evolution
