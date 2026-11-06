#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/partner/partner.hpp>

#include <algorithm>
#include <utility>

// What the story's orders change in the partner (script_interp.c: scriptInstruction28to3F,
// scriptInstruction64to7E). The orders pick the numbers; the partner keeps its own rules after.
namespace dwe::game::partner {

void Partner::takeStoryChange(PartnerState changed) {
  DWE_ASSERT(changed.species == state_.species);
  state_ = std::move(changed);
}

void Partner::keepEnergyInBounds(const ruleset::GameClock& clock) {
  // script_interp.c:846 scriptUpdateEnergyBoundaries.
  useClock(clock);
  if (state_.hunger.energy > species_->energyCap) {
    core::logDebug("[port:partner] scriptUpdateEnergyBoundaries (script_interp.c:848): energy {} "
                   "stops at the cap {}",
                   state_.hunger.energy, species_->energyCap);
    state_.hunger.energy = species_->energyCap;
  }
  if (!state_.conditions.has(Condition::Hungry) ||
      state_.hunger.energy < species_->energyThreshold) {
    return;
  }
  const ruleset::GameTime now = clock.time();
  core::logDebug("[port:partner] scriptUpdateEnergyBoundaries (script_interp.c:851): energy {} "
                 "reached {}, hunger cleared",
                 state_.hunger.energy, species_->energyThreshold);
  state_.conditions.clear(Condition::Hungry);
  setFoodTimer(now.hour, now.minute);
  state_.hunger.windowSteps = 0;
}

} // namespace dwe::game::partner
