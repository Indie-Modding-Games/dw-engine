#include <dwe/game/partner/partner.hpp>

#include <algorithm>
#include <cstdint>

#include "partner_constants.hpp"

// What the tamer does with the partner: feed it, cure it, scold and praise it, put it to bed, take
// it to the toilet.
namespace dwe::game::partner {

bool Partner::refuses(const ItemOffer& offer) {
  const bool refused = strategies().itemRefusal->refuses(offer, state_, *species_, care(), random_);
  if (refused) {
    emit(EventKind::Refused);
  }
  return refused;
}

void Partner::eat(const FoodEffect& food, const ruleset::GameClock& clock) {
  useClock(clock);
  applyFood(food);
  const ruleset::GameTime now = clock.time();
  answerHunger(food, now.hour, now.minute);
}

void Partner::applyFood(const FoodEffect& food) {
  // item.c: handleFood.
  const CareLimits& limits = care().limits;
  const HungerRules& hunger = care().hunger;
  std::int32_t energy = food.energy;
  std::int32_t happiness = food.happiness;
  if (species_->favoriteFood == food.item) {
    energy = energy * hunger.favoriteEnergyPercent / constants::Percent;
    happiness += hunger.favoriteHappiness;
  }
  state_.hunger.energy = std::min(state_.hunger.energy + energy, species_->energyCap);
  state_.mood.tiredness = std::max(state_.mood.tiredness - food.tirednessRelief, 0);
  state_.mood.happiness = std::min(state_.mood.happiness + happiness, limits.happinessMax);
  state_.mood.discipline = std::min(state_.mood.discipline + food.discipline, limits.disciplineMax);
  state_.weight = std::clamp(state_.weight + food.weight, limits.weightMin, limits.weightMax);
  --state_.toilet.poopLevel;
  // Every food sets the training boost, so one without a boost ends the one running
  // (food_clears_training_boost).
  if (care().quirks.foodClearsTrainingBoost || food.boost.hours > 0) {
    state_.training.boost = {.stats = food.boost.stats,
                             .value = food.boost.value,
                             .frames =
                                 static_cast<std::int32_t>(food.boost.hours * framesPerHour())};
  }
  rollFoodSickness(food.sicknessPercent);
  state_.life.remainingLifetime += food.lifetimeHours;
  addStatGains(food.permanentGain);
  PartnerStats& stats = state_.stats;
  stats.currentHp = std::min(stats.currentHp + food.hpHeal, stats.base[Stat::Hp]);
  stats.currentMp = std::min(stats.currentMp + food.mpHeal, stats.base[Stat::Mp]);
}

void Partner::rollFoodSickness(std::int32_t percent) {
  // item.c: handleItemSickness.
  const std::int32_t roll = random_.below(constants::Percent);
  if (roll >= percent || state_.conditions.has(Condition::Sick)) {
    return;
  }
  fallSick();
  if (state_.conditions.has(Condition::Injured)) {
    cureInjury();
  }
  emit(EventKind::GotSick);
}

void Partner::answerHunger(const FoodEffect& food, std::int32_t hour, std::int32_t minute) {
  // partner_impl.c: partnerHandleFoodFeed. Only a meal counts; a hungry partner that ate enough is
  // fed, one that was not hungry was overfed.
  if (!food.meal) {
    return;
  }
  if (!state_.conditions.has(Condition::Hungry)) {
    changeMood(care().hunger.overfed);
    return;
  }
  if (state_.hunger.energy < species_->energyThreshold) {
    return;
  }
  changeMood(care().hunger.fed);
  state_.conditions.clear(Condition::Hungry);
  setFoodTimer(hour, minute);
  state_.hunger.windowSteps = 0;
  emit(EventKind::HungerWindowClosed, 1);
}

bool Partner::cure(const CureEffect& cure) {
  // item.c: handleStatusItems, handleMedicineHealing. A fainted partner takes no medicine.
  if (state_.stats.currentHp == 0) {
    return false;
  }
  if (state_.conditions.has(Condition::Injured) &&
      random_.below(constants::Percent) < cure.injuryPercent) {
    cureInjury();
  }
  if (state_.conditions.has(Condition::Sick) &&
      random_.below(constants::Percent) < cure.sicknessPercent) {
    cureSickness();
    return true;
  }
  return false;
}

void Partner::scold() {
  // partner_impl.c: handlePraiseScold. A scold right after a refusal or a poop on the ground counts
  // more.
  const MoodRules& rules = care().mood;
  const std::int32_t discipline = state_.mood.discipline;
  const std::int32_t happiness = state_.mood.happiness;
  if (state_.mood.scoldBoost == ScoldBoost::Ready) {
    changeMood(rules.boostedScold);
    state_.mood.scoldBoost = ScoldBoost::Used;
  } else {
    changeMood(rules.scold);
  }
  state_.hunger.refusedFavorite = false;
  state_.conditions.clear(Condition::Sulking);
  if (species_->level == Level::Rookie && discipline == 0 &&
      happiness == care().limits.happinessMin) {
    emit(EventKind::ScoldedAtRockBottom);
  }
}

void Partner::praise() {
  const MoodRules& rules = care().mood;
  const std::int32_t discipline = state_.mood.discipline;
  state_.mood.discipline += rules.praise.discipline;
  state_.mood.happiness += (discipline / rules.praiseDisciplineDivisor) + rules.praise.happiness;
}

bool Partner::canSleep() const {
  return alive() && state_.conditions.has(Condition::Sleepy);
}

void Partner::sleep(ruleset::GameClock& clock, const SleepContext& context) {
  if (!canSleep()) {
    return;
  }
  useClock(clock);
  const ruleset::GameTime before = clock.time();
  regenerateWhileAsleep(before.hour, before.minute, context);
  // partner_impl.c: handleSleeping.
  const std::int32_t hoursSlept = hoursUntilWake(before.hour);
  const ruleset::ClockAdvance advance =
      clock.jumpTo(state_.sleep.wakeHour, state_.sleep.wakeMinute);
  addDays(advance.days, true);
  state_.evolution.hours +=
      care().quirks.sleepAddsDefaultHoursToEvolution ? state_.sleep.asleepHours : hoursSlept;
  spendLifetime(hoursSlept);
  const ruleset::GameTime after = clock.time();
  wakeUp(after.hour, after.minute, hoursSlept);
  emit(EventKind::Slept, hoursSlept);
}

std::int32_t Partner::hoursUntilWake(std::int32_t hour) const {
  const std::int32_t wake = state_.sleep.wakeHour;
  return hour < wake ? wake - hour : wake + (constants::HoursPerDay - hour);
}

void Partner::regenerateWhileAsleep(std::int32_t hour, std::int32_t minute,
                                    const SleepContext& context) {
  // partner_impl.c: sleepRegen. A full night heals most of the HP and MP and the tiredness; a short
  // one less.
  const SleepRegeneration& rules = care().sleep.regeneration;
  std::int32_t hoursSlept = hoursUntilWake(hour);
  if (minute > 0 && hour != state_.sleep.sleepyHour) {
    ++hoursSlept;
  }
  std::int32_t factor = hoursSlept * constants::Percent / std::max(state_.sleep.asleepHours, 1);
  if (context.hasPillow) {
    factor = factor * rules.pillowPercent / constants::Percent;
  }
  if (context.area == AreaResponse::Liked) {
    factor = factor * rules.likedAreaPercent / constants::Percent;
  } else if (context.area == AreaResponse::Disliked) {
    factor = factor * rules.dislikedAreaPercent / constants::Percent;
  }
  PartnerStats& stats = state_.stats;
  const std::int32_t maxHp = stats.base[Stat::Hp];
  const std::int32_t maxMp = stats.base[Stat::Mp];
  const std::int32_t healPercent = random_.below(rules.hpMpPercentRange) + rules.hpMpPercent;
  stats.currentHp += factor * (maxHp * healPercent / constants::Percent) / constants::Percent;
  stats.currentMp += factor * (maxMp * healPercent / constants::Percent) / constants::Percent;
  const std::int32_t restPercent =
      random_.below(rules.tirednessPercentRange) + rules.tirednessPercent;
  MoodState& mood = state_.mood;
  mood.tiredness -=
      factor * (mood.tiredness * restPercent / constants::Percent) / constants::Percent;
  stats.currentHp = std::min(stats.currentHp, maxHp);
  stats.currentMp = std::min(stats.currentMp, maxMp);
  mood.tiredness = std::max(mood.tiredness, 0);
  state_.weight -= species_->defaultWeight / rules.weightDivisor;
  state_.weight = std::max(state_.weight, care().limits.weightMin);
}

void Partner::wakeUp(std::int32_t hour, std::int32_t minute, std::int32_t hoursSlept) {
  scheduleNextSleep();
  const bool wasHungry = state_.conditions.has(Condition::Hungry);
  // The original searches on from the meal hour set before sleeping (wake_searches_from_last_meal);
  // searching from the wake hour is the same search with that hour as the last meal.
  if (!care().quirks.wakeSearchesFromLastMeal) {
    state_.hunger.nextHungerHour = hour;
  }
  setFoodTimer(hour, minute);
  state_.hunger.windowSteps = 0;
  state_.conditions.clear(Condition::Hungry);
  state_.sleep.awakeCuts = state_.sleep.awakeHours * awakeCutsPerHour();
  state_.sleep.tirednessCounter = 0;
  state_.sleep.sleepyHoursForSickness = 0;
  state_.conditions.clear(Condition::Sleepy);
  state_.life.protectedHour = hour;
  // Waking up needing the toilet leaves a pile, but is no care mistake.
  if (state_.conditions.has(Condition::NeedsToilet)) {
    leavePoopPile();
    state_.conditions.clear(Condition::NeedsToilet);
    state_.toilet.poopLevel = species_->poopInterval;
    state_.toilet.urgeFrames = constants::NoUrge;
  }
  // The original clears the hunger before it checks it (sleeping_hungry_keeps_weight).
  if (wasHungry && !care().quirks.sleepingHungryKeepsWeight) {
    state_.weight -= care().sleep.hungryWeightLoss;
  }
  if (state_.conditions.has(Condition::Sick)) {
    state_.health.sickHours += hoursSlept;
    changeMood(care().sleep.sickPenalty);
  }
  if (state_.conditions.has(Condition::Injured)) {
    state_.health.injuredHours += hoursSlept;
    changeMood(care().sleep.injuredPenalty);
  }
}

void Partner::addDays(std::int64_t days, bool ages) {
  if (days <= 0) {
    return;
  }
  if (ages) {
    state_.life.age += static_cast<std::int32_t>(days);
  }
  emit(EventKind::NewDay, static_cast<std::int32_t>(days));
}

std::int32_t Partner::poopLevelAfterGoing() const {
  // The original waits twice the interval after the toilet or a poop on the ground
  // (toilet_doubles_poop_interval).
  const std::int32_t interval = species_->poopInterval;
  return care().quirks.toiletDoublesPoopInterval ? interval + interval : interval;
}

void Partner::loseWeightAfterPoop() {
  // partner_impl.c: handlePoopWeightLoss.
  const ToiletRules& rules = care().toilet;
  state_.weight -=
      (random_.below(rules.weightLossRoll) + species_->poopSize) / rules.weightLossDivisor;
  state_.weight = std::max(state_.weight, care().limits.weightMin);
}

void Partner::useToilet() {
  // partner_impl.c: handleToilet.
  if (!state_.conditions.has(Condition::NeedsToilet)) {
    return;
  }
  changeMood(care().toilet.toilet);
  state_.toilet.poopLevel = poopLevelAfterGoing();
  state_.conditions.clear(Condition::NeedsToilet);
  loseWeightAfterPoop();
  emit(EventKind::UsedToilet);
}

void Partner::usePortableToilet() {
  // item.c: handlePortaPotty: the interval is not doubled.
  if (!state_.conditions.has(Condition::NeedsToilet)) {
    return;
  }
  state_.toilet.poopLevel = species_->poopInterval;
  state_.conditions.clear(Condition::NeedsToilet);
  loseWeightAfterPoop();
  emit(EventKind::UsedToilet);
}

bool Partner::eatsPoop() const {
  return std::ranges::find(care().toilet.poopEaters, state_.species) !=
         care().toilet.poopEaters.end();
}

void Partner::eatPoop(std::int32_t pileSize) {
  // partner_impl.c: handleEatingPoop. The bigger the pile, the more it heals.
  const ToiletRules& rules = care().toilet;
  const auto tier = std::ranges::find_if(
      rules.eating, [&](const PoopMeal& meal) { return pileSize < meal.belowSize; });
  const PoopMeal& meal = tier == rules.eating.end() ? rules.eatingLargest : *tier;
  PartnerStats& stats = state_.stats;
  stats.currentHp += stats.base[Stat::Hp] / meal.hpDivisor;
  stats.currentMp += stats.base[Stat::Mp] / meal.mpDivisor;
  state_.weight += meal.weight;
  stats.currentHp = std::min(stats.currentHp, stats.base[Stat::Hp]);
  stats.currentMp = std::min(stats.currentMp, stats.base[Stat::Mp]);
  state_.weight = std::min(state_.weight, care().limits.weightMax);
  if (random_.below(constants::Percent) >= meal.curePercent) {
    return;
  }
  if (state_.conditions.has(Condition::Injured)) {
    cureInjury();
  }
  if (state_.conditions.has(Condition::Sick)) {
    cureSickness();
  }
}

void Partner::enterMap() {
  // partner_impl.c: setupPartnerOnWarp.
  state_.mood.areaFrames = 0;
}

} // namespace dwe::game::partner
