#include <dwe/game/partner/partner.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include "partner_constants.hpp"

// The care rules that run on every frame of the clock (partner_impl.c: tickConditions and the tick
// functions it calls, clock.c: tickGameClock). Each one runs on the frame of the day the original
// checks, so stepping through the frames a fast-forward crossed does what playing them does.
namespace dwe::game::partner {

Partner::FrameTime Partner::frameAt(std::int64_t elapsedFrame) const {
  const std::int64_t perHour = framesPerHour();
  const std::int64_t perDay = perHour * ruleset::HoursPerDay;
  const std::int64_t ofDay = elapsedFrame % perDay;
  return {.frameOfDay = ofDay,
          .day = elapsedFrame / perDay,
          .hour = static_cast<std::int32_t>(ofDay / perHour),
          .minute = static_cast<std::int32_t>((ofDay % perHour) / clockRules_.framesPerMinute)};
}

bool Partner::every(const FrameTime& time, std::int64_t frames) {
  return frames > 0 && time.frameOfDay % frames == 0;
}

bool Partner::everyMinutes(const FrameTime& time, std::int64_t minutes) const {
  return every(time, minutes * clockRules_.framesPerMinute);
}

bool Partner::hourly(const FrameTime& time) const {
  return every(time, framesPerHour());
}

void Partner::tick(const ruleset::GameClock& clock, const ruleset::ClockAdvance& advance,
                   const CareContext& context) {
  if (species_ == nullptr || advance.frames <= 0) {
    return;
  }
  useClock(clock);
  const std::int64_t last = clock.elapsedFrames();
  for (std::int64_t frame = last - advance.frames + 1; frame <= last && alive(); ++frame) {
    const FrameTime time = frameAt(frame);
    stepClock(time);
    stepCare(time, context);
  }
}

void Partner::stepClock(const FrameTime& time) {
  // clock.c: tickGameClock, on the first frame of each hour and of each day.
  if (!hourly(time)) {
    return;
  }
  ++state_.evolution.hours;
  --state_.life.remainingLifetime;
  if (time.frameOfDay == 0) {
    startDay(time);
  }
}

void Partner::startDay(const FrameTime& time) {
  ++state_.life.age;
  state_.life.remainingLifetime = std::max(state_.life.remainingLifetime, 0);
  emit(EventKind::NewDay, 1);
  if (time.day % clockRules_.daysPerYear != 0) {
    return;
  }
  // A new year: a happy partner may raise the tamer's level, an unhappy one lower it.
  const YearRules& year = care().year;
  if (state_.mood.happiness == year.happyHappiness) {
    rollTamerLevel(year.happyTamerLevelPercent, 1);
  } else if (state_.mood.happiness < 0) {
    rollTamerLevel(year.unhappyTamerLevelPercent, -1);
  }
}

void Partner::stepCare(const FrameTime& time, const CareContext& context) {
  tickDeath(time, context);
  if (!alive()) {
    return;
  }
  tickSleep(time);
  tickSickness(time, context);
  if (!alive()) {
    return;
  }
  tickTiredness(time, context);
  tickHunger(time);
  tickSulking(time);
  clampBoundaries();
  tickTimers();
  tickUnhappyLifetime(time);
  // The hour after waking holds back the virus form too, and ends when the hour does.
  if (!inProtectedHour(time.hour)) {
    state_.life.protectedHour.reset();
    tickVirusBar();
  }
  tickPoop(time, context);
}

void Partner::tickDeath(const FrameTime& time, const CareContext& context) {
  // partner_impl.c: tickDeathCondition.
  if (inProtectedHour(time.hour) || state_.life.remainingLifetime > 0 || !context.tamerIdle) {
    return;
  }
  state_.life.remainingLifetime = 0;
  die(LifePhase::DiedNaturally);
}

void Partner::tickSleep(const FrameTime& time) {
  // partner_impl.c: tickSleepMechanics.
  const SleepRules& rules = care().sleep;
  SleepState& sleep = state_.sleep;
  const bool wasSleepy = state_.conditions.has(Condition::Sleepy);
  if (everyMinutes(time, 1)) {
    sleep.tirednessCounter +=
        state_.mood.tiredness * rules.tirednessPercentPerMinute / constants::Percent;
  }
  if (!wasSleepy && sleep.tirednessCounter >= rules.tirednessThreshold &&
      everyMinutes(time, rules.awakeCutMinutes)) {
    cutAwakeTime();
  }
  if (wasSleepy) {
    const SleepyPenalty& penalty = sleepyPenalty();
    if (every(time, penalty.everyFrames)) {
      state_.mood.happiness += penalty.happiness;
      state_.mood.discipline += penalty.discipline;
    }
    if (hourly(time) && time.hour != sleep.sleepyHour) {
      ++sleep.sleepyHoursForSickness;
      ++sleep.missedSleepHours;
    }
  }
  const bool pastWake = (sleep.sleepyHour < sleep.wakeHour && sleep.sleepyHour < time.hour &&
                         sleep.wakeHour <= time.hour) ||
                        (sleep.wakeHour < sleep.sleepyHour && time.hour < sleep.sleepyHour &&
                         sleep.wakeHour <= time.hour);
  if (state_.conditions.has(Condition::Sleepy) && pastWake) {
    missSleep();
  }
  if (wasSleepy) {
    return;
  }
  const bool bedtime = sleep.wakeHour < sleep.sleepyHour
                           ? time.hour >= sleep.sleepyHour || time.hour < sleep.wakeHour
                           : sleep.wakeHour > sleep.sleepyHour && time.hour >= sleep.sleepyHour &&
                                 time.hour < sleep.wakeHour;
  if (bedtime) {
    setCondition(Condition::Sleepy, EventKind::BecameSleepy);
  }
}

const SleepyPenalty& Partner::sleepyPenalty() const {
  const SleepRules& rules = care().sleep;
  if (species_->level == Level::Baby) {
    return rules.babySleepyPenalty;
  }
  if (species_->level == Level::InTraining) {
    return rules.inTrainingSleepyPenalty;
  }
  return rules.adultSleepyPenalty;
}

void Partner::cutAwakeTime() {
  // Tiredness brings bedtime forward by a cut of the time awake.
  SleepState& sleep = state_.sleep;
  const std::int32_t cutsPerHour = awakeCutsPerHour();
  --sleep.awakeCuts;
  sleep.tirednessCounter = 0;
  const std::int32_t cutsLost = (sleep.awakeHours * cutsPerHour) - sleep.awakeCuts;
  const std::int32_t hoursLate = (cutsLost / cutsPerHour) + (cutsLost % cutsPerHour != 0 ? 1 : 0);
  sleep.sleepyHour = sleep.wakeHour - sleep.asleepHours - hoursLate;
  if (sleep.sleepyHour < 0) {
    sleep.sleepyHour += constants::HoursPerDay;
  }
  sleep.sleepyMinute = (sleep.awakeCuts % cutsPerHour) * care().sleep.awakeCutMinutes;
}

void Partner::missSleep() {
  // The wake hour came and the partner was never put to bed.
  scheduleNextSleep();
  state_.sleep.awakeCuts = state_.sleep.awakeHours * awakeCutsPerHour();
  state_.sleep.tirednessCounter = 0;
  state_.conditions.clear(Condition::Sleepy);
  countCareMistake(CareMistakeCause::MissedSleep);
  rollTamerLevel(care().sleep.missedSleepTamerLevelPercent, -1);
}

void Partner::tickSickness(const FrameTime& time, const CareContext& context) {
  // partner_impl.c: tickSicknessMechanics.
  const SicknessRules& rules = care().sickness;
  HealthState& health = state_.health;
  const bool wasSick = state_.conditions.has(Condition::Sick);
  const bool hour = hourly(time);
  if (hour && !wasSick && state_.sleep.sleepyHoursForSickness >= rules.missedSleepHours) {
    ++health.sicknessTries;
    if (strategies().sicknessRoll->fallsSick(state_, care(), random_)) {
      fallSick();
      state_.mood.happiness += rules.onSickHappiness;
      state_.sleep.sleepyHoursForSickness = 0;
    }
  }
  if (hour && !wasSick && context.area == AreaResponse::Disliked &&
      state_.mood.areaFrames > rules.dislikedAreaHours * framesPerHour()) {
    fallSick();
    state_.mood.areaFrames = 0;
  }
  if (hour && state_.conditions.has(Condition::Sick)) {
    changeMood(rules.hourlyPenalty);
  }
  if (!wasSick && health.sickHours > 0) {
    health.sickHours = 0;
  }
  if (hour) {
    health.sickHours += state_.conditions.has(Condition::Sick) ? 1 : 0;
    health.injuredHours += state_.conditions.has(Condition::Injured) ? 1 : 0;
  }
  // The hour after waking holds back what comes of a long injury or sickness.
  if (!inProtectedHour(time.hour)) {
    if (health.injuredHours >= rules.injuryHours && !wasSick && context.tamerIdle) {
      fallSick();
      cureInjury();
    }
    if (health.sickHours >= rules.untreatedHours && context.tamerIdle &&
        state_.life.remainingLifetime != 0) {
      loseLife();
      health.sickHours = 0;
    }
  }
  if (!wasSick && state_.conditions.has(Condition::Sick)) {
    emit(EventKind::GotSick);
  }
}

void Partner::tickTiredness(const FrameTime& time, const CareContext& context) {
  // partner_impl.c: tickTirednessMechanics.
  const MoodRules& mood = care().mood;
  const TirednessRules& rules = care().tiredness;
  MoodState& state = state_.mood;
  if (state.areaFrames > 0 && state.areaFrames % mood.areaEffectFrames == 0) {
    if (context.area == AreaResponse::Liked) {
      changeMood(mood.likedArea);
    } else if (context.area == AreaResponse::Disliked) {
      changeMood(mood.dislikedArea);
    }
  }
  if (state.subTiredness >= rules.subTirednessThreshold) {
    state.tiredness = std::min(state.tiredness + 1, care().limits.tirednessMax);
    state.subTiredness = 0;
  }
  if (state.tiredness >= care().hunger.tiredHungerTiredness) {
    ++state_.hunger.tiredHungerFrames;
  } else {
    state_.hunger.tiredHungerFrames = 0;
  }
  if (state.tiredness < rules.tiredThreshold) {
    state_.conditions.clear(Condition::Tired);
    return;
  }
  setCondition(Condition::Tired, EventKind::BecameTired);
  if (every(time, rules.tiredHappinessFrames)) {
    state.happiness += rules.tiredHappiness;
  }
}

void Partner::tickHunger(const FrameTime& time) {
  // partner_impl.c: tickHungerMechanics.
  const HungerRules& rules = care().hunger;
  HungerState& hunger = state_.hunger;
  if (state_.conditions.has(Condition::Hungry)) {
    if (every(time, rules.windowStepFrames)) {
      --hunger.windowSteps;
    }
  } else if (everyMinutes(time, 1)) {
    --hunger.foodLevel;
  }
  if (hourly(time)) {
    hunger.energy = std::max(hunger.energy - species_->energyUsage, 0);
  }
  if (!state_.conditions.has(Condition::Hungry) && hunger.foodLevel <= 0) {
    hunger.windowSteps = windowSteps(rules.windowMinutes);
    setCondition(Condition::Hungry, EventKind::BecameHungry);
  }
  if (state_.conditions.has(Condition::Hungry) && hunger.windowSteps <= 0) {
    closeHungerWindow(time.hour, time.minute);
  }
  if (hunger.energy <= 0) {
    if (everyMinutes(time, rules.starvingWeightLossMinutes)) {
      state_.weight = std::max(state_.weight - 1, care().limits.weightMin);
    }
    if (everyMinutes(time, 1)) {
      ++hunger.emptyStomachMinutes;
    }
  }
  if (hunger.tiredHungerFrames >= rules.tiredHungerFrames && hourly(time)) {
    hunger.foodLevel -= rules.tiredHungerMinutes;
  }
}

void Partner::closeHungerWindow(std::int32_t hour, std::int32_t minute) {
  setFoodTimer(hour, minute);
  state_.hunger.windowSteps = 0;
  state_.conditions.clear(Condition::Hungry);
  emit(EventKind::HungerWindowClosed, state_.hunger.energy >= species_->energyThreshold ? 1 : 0);
  countCareMistake(CareMistakeCause::HungerWindow);
}

void Partner::tickSulking(const FrameTime& time) {
  // partner_impl.c: tickUnhappinessMechanics.
  const MoodRules& rules = care().mood;
  MoodState& mood = state_.mood;
  if (state_.conditions.none() && mood.happiness < 0 && every(time, rules.sulkCheckFrames) &&
      random_.below(constants::Percent) < std::abs(mood.happiness) - mood.discipline) {
    setCondition(Condition::Sulking, EventKind::StartedSulking);
  }
  if (!state_.conditions.has(Condition::Sulking)) {
    return;
  }
  if (every(time, rules.sulkRecoveryFrames)) {
    ++mood.happiness;
  }
  if (every(time, rules.sulkStatLossFrames)) {
    loseStatsToSulking();
  }
  if (mood.happiness >= 0) {
    state_.conditions.clear(Condition::Sulking);
  }
}

void Partner::loseStatsToSulking() {
  PartnerStats& stats = state_.stats;
  for (const Stat stat : AllStats) {
    stats.base[stat] -= stats.base[stat] * care().mood.sulkStatLossPercent / constants::Percent;
    stats.base[stat] = std::max(stats.base[stat], care().limits.statMin);
  }
  stats.currentHp = std::min(stats.currentHp, stats.base[Stat::Hp]);
  stats.currentMp = std::min(stats.currentMp, stats.base[Stat::Mp]);
}

void Partner::tickTimers() {
  MoodState& mood = state_.mood;
  ++mood.areaFrames;
  if (mood.areaFrames > care().mood.areaTimerLimitFrames) {
    mood.areaFrames = 0;
  }
  TrainingBoost& boost = state_.training.boost;
  --boost.frames;
  if (boost.frames <= 0) {
    boost.stats = {};
    boost.frames = 0;
  }
}

void Partner::tickUnhappyLifetime(const FrameTime& time) {
  if (every(time, life().unhappyLifetime.everyHours * framesPerHour())) {
    loseUnhappyLifetime();
  }
}

void Partner::tickVirusBar() {
  // tickConditions: a full virus bar turns the partner into its virus form; the form and stats it
  // had are kept to come back from it.
  if (state_.toilet.virusBar < care().toilet.virusBarLimit ||
      state_.species == care().toilet.virusForm) {
    return;
  }
  state_.virusBackup = {.species = state_.species, .stats = state_.stats.base};
  state_.toilet.virusBar = 0;
  emit(EventKind::VirusBarFull);
}

void Partner::tickPoop(const FrameTime& time, const CareContext& context) {
  // partner_impl.c: tickPoopingMechanic.
  if (!context.tamerIdle) {
    return;
  }
  const ToiletRules& rules = care().toilet;
  ToiletState& toilet = state_.toilet;
  const bool needed = state_.conditions.has(Condition::NeedsToilet);
  if (!needed && everyMinutes(time, rules.poopStepMinutes)) {
    --toilet.poopLevel;
  }
  if (!needed && toilet.poopLevel <= 0) {
    setCondition(Condition::NeedsToilet, EventKind::NeedsToilet);
    toilet.urgeFrames = (state_.mood.discipline + rules.urgeDisciplineOffset) *
                        rules.urgeFramesPerHundred / constants::Percent;
  }
  if (!needed) {
    return;
  }
  --toilet.urgeFrames;
  if (toilet.urgeFrames <= 0) {
    poopOnGround();
  }
}

void Partner::poopOnGround() {
  // The partner held on as long as it could (handleWildPoop, after createPoopPile).
  leavePoopPile();
  state_.toilet.urgeFrames = constants::NoUrge;
  state_.mood.scoldBoost = ScoldBoost::Ready;
  state_.conditions.clear(Condition::NeedsToilet);
  countCareMistake(CareMistakeCause::PoopOnGround);
  changeMood(care().toilet.wild);
  state_.toilet.poopLevel = poopLevelAfterGoing();
  loseWeightAfterPoop();
  rollTamerLevel(care().toilet.wildTamerLevelPercent, -1);
}

void Partner::leavePoopPile() {
  // partner_impl.c: createPoopPile; the field places the pile.
  if (state_.species != care().toilet.virusForm) {
    ++state_.toilet.virusBar;
  }
  emit(EventKind::PoopedOnGround, species_->poopSize);
}

} // namespace dwe::game::partner
