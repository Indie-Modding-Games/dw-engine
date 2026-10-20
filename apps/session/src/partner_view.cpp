#include <dwe/session/partner_view.hpp>

#include <algorithm>

namespace dwe::session {

float CareMeter::fill() const {
    if (max <= min) {
        return 0.0F;
    }
    const auto span = static_cast<float>(max - min);
    return std::clamp(static_cast<float>(value - min) / span, 0.0F, 1.0F);
}

PartnerView viewOf(const game::partner::Partner& partner, const game::partner::CareRules& rules) {
    const game::partner::PartnerState& state = partner.state();
    const game::partner::CareLimits& limits = rules.limits;
    return {
        .species = state.species,
        .level = partner.species().level,
        .alive = partner.alive(),
        .ageDays = state.life.age,
        .lives = state.life.lives,
        .lifetimeHoursLeft = state.life.remainingLifetime,
        .stats = state.stats.base,
        .currentHp = state.stats.currentHp,
        .currentMp = state.stats.currentMp,
        .happiness = {.value = state.mood.happiness, .min = limits.happinessMin, .max = limits.happinessMax},
        .discipline = {.value = state.mood.discipline, .min = 0, .max = limits.disciplineMax},
        .tiredness = {.value = state.mood.tiredness, .min = 0, .max = limits.tirednessMax},
        .energy = {.value = state.hunger.energy, .min = 0, .max = partner.species().energyCap},
        .weight = {.value = state.weight, .min = limits.weightMin, .max = limits.weightMax},
        .virusBar = {.value = state.toilet.virusBar, .min = 0, .max = rules.toilet.virusBarLimit},
        .minutesToMeal = state.hunger.foodLevel,
        .stepsToToilet = state.toilet.poopLevel,
        .conditions = state.conditions,
        .careMistakes = state.evolution.careMistakes,
        .battles = state.evolution.battles,
    };
}

} // namespace dwe::session
