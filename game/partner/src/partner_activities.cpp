#include <dwe/game/partner/messages.hpp>
#include <dwe/game/partner/partner.hpp>

#include <algorithm>
#include <cstdint>
#include <string>

#include "partner_constants.hpp"

// What takes the partner's time in one go: battles, training and the scripts' time skips. Each passes its time with
// its own accounting instead of the frame by frame rules, as the original does.
namespace dwe::game::partner {

using messages::formatMessage;

void Partner::finishBattle(const BattleOutcome& outcome, ruleset::GameClock& clock) {
    // tamer.c: startBattle, after the battle.
    useClock(clock);
    const BattleCareRules& rules = care().battle;
    // battle_ui.c: the results of a won battle roll the injury before the tamer's accounting.
    if (outcome.result == BattleResult::Won && outcome.tookDamage) {
        rollBattleInjury();
    }
    switch (outcome.result) {
    case BattleResult::Lost:
        changeMood(rules.lost);
        loseLife();
        break;
    case BattleResult::Fled:
        changeMood(rules.fled);
        addPostBattleTiredness();
        break;
    case BattleResult::Won:
        state_.mood.happiness += rules.wonHappiness;
        addPostBattleTiredness();
        ++state_.evolution.battles;
        break;
    }
    clampBoundaries();
    passBattleTime(clock);
}

void Partner::addPostBattleTiredness() {
    // partner_impl.c: handlePostBattleTiredness. A battle that used much MP tires more.
    const BattleCareRules& rules = care().battle;
    MoodState& mood = state_.mood;
    mood.tiredness += rules.tiredness;
    const std::int32_t maxMp = state_.stats.base[Stat::Mp];
    const std::int32_t used = maxMp - state_.stats.currentMp;
    if (maxMp > 0 && state_.stats.currentMp < maxMp - (maxMp * rules.lowMpPercent / constants::Percent)) {
        // The original divides before it multiplies, so only a battle that used all the MP adds anything
        // (post_battle_mp_tiredness_truncated).
        mood.tiredness += care().quirks.postBattleMpTirednessTruncated ? (used / maxMp) * rules.lowMpTiredness
                                                                       : used * rules.lowMpTiredness / maxMp;
    }
    state_.hunger.foodLevel -= rules.foodMinutes;
}

void Partner::rollBattleInjury() {
    // battle_ui.c: handleBattleInjury. A hurt partner may come out of a won battle injured: the more tired and the
    // less HP, the likelier.
    if (state_.conditions.has(Condition::Injured)) {
        return;
    }
    const std::int32_t maxHp = std::max(state_.stats.base[Stat::Hp], 1);
    const std::int32_t chance = state_.mood.tiredness - (constants::Percent * state_.stats.currentHp / maxHp);
    if (random_.below(constants::Percent) < chance) {
        setCondition(Condition::Injured, EventKind::GotInjured);
    }
}

void Partner::passHungerWindow(std::int32_t steps) {
    // The time skips count down the hunger window without closing it: the frame rules close it later and count the
    // care mistake again (skipped_hunger_window_counts_twice).
    state_.hunger.windowSteps -= steps;
    if (state_.hunger.windowSteps <= 0 && care().quirks.skippedHungerWindowCountsTwice) {
        countCareMistake(CareMistakeCause::HungerWindow);
    }
}

void Partner::passBattleTime(ruleset::GameClock& clock) {
    // tamer.c: advanceBattleTime.
    const BattleCareRules& rules = care().battle;
    const ruleset::GameTime before = clock.time();
    const std::int32_t minutes = (before.hour * constants::MinutesPerHour) + before.minute + rules.durationMinutes;
    const std::int32_t hour = (minutes / constants::MinutesPerHour) % constants::HoursPerDay;
    const ruleset::ClockAdvance advance = clock.jumpTo(hour, minutes % constants::MinutesPerHour);
    // The battle's midnight adds no age (battle_midnight_skips_age).
    addDays(advance.days, !care().quirks.battleMidnightSkipsAge);
    if (hour != before.hour) {
        spendLifetime(1);
        if (state_.conditions.has(Condition::Sleepy)) {
            ++state_.sleep.sleepyHoursForSickness;
            ++state_.sleep.missedSleepHours;
        }
        if (state_.conditions.has(Condition::Sick) && care().quirks.skippedHoursAddSicknessTries) {
            ++state_.health.sicknessTries;
        }
        state_.health.injuredHours += state_.conditions.has(Condition::Injured) ? 1 : 0;
        state_.health.sickHours += state_.conditions.has(Condition::Sick) ? 1 : 0;
    }
    if (state_.conditions.has(Condition::Hungry)) {
        passHungerWindow(windowSteps(rules.durationMinutes));
    } else {
        state_.hunger.foodLevel -= rules.durationMinutes;
    }
    if (state_.conditions.has(Condition::NeedsToilet)) {
        state_.toilet.urgeFrames -= static_cast<std::int32_t>(rules.durationMinutes * clockRules_.framesPerMinute);
    } else {
        state_.toilet.poopLevel -= rules.poopSteps;
    }
}

void Partner::skipHours(std::int32_t hours, const ruleset::GameClock& clock) {
    // partner_impl.c: skipHours, the later versions'.
    if (hours <= 0) {
        return;
    }
    useClock(clock);
    const std::int32_t hour = clock.time().hour;
    state_.evolution.hours += hours;
    state_.life.remainingLifetime -= hours;
    addDays((hour + hours) / constants::HoursPerDay, true);
    for (std::int32_t check = unhappyChecksWhileSkipping(hour, hours); check > 0; --check) {
        loseUnhappyLifetime();
    }
    state_.life.remainingLifetime = std::max(state_.life.remainingLifetime, 0);
    if (state_.conditions.has(Condition::Sleepy)) {
        state_.sleep.sleepyHoursForSickness += hours;
        state_.sleep.missedSleepHours += hours;
    }
    if (state_.conditions.has(Condition::Hungry)) {
        passHungerWindow(windowSteps(hours * constants::MinutesPerHour));
    } else {
        state_.hunger.foodLevel -= hours * constants::MinutesPerHour;
    }
    state_.hunger.energy -= species_->energyUsage;
    if (state_.conditions.has(Condition::NeedsToilet)) {
        state_.toilet.urgeFrames -= static_cast<std::int32_t>(framesPerHour());
    } else {
        state_.toilet.poopLevel -= hours * (constants::MinutesPerHour / care().toilet.poopStepMinutes);
    }
    if (state_.conditions.has(Condition::Sick) && care().quirks.skippedHoursAddSicknessTries) {
        state_.health.sicknessTries += hours;
    }
    state_.health.injuredHours += state_.conditions.has(Condition::Injured) ? hours : 0;
    state_.health.sickHours += state_.conditions.has(Condition::Sick) ? hours : 0;
}

std::int32_t Partner::unhappyChecksWhileSkipping(std::int32_t hour, std::int32_t hours) const {
    const std::int32_t every = life().unhappyLifetime.everyHours;
    if (care().quirks.skippedHoursUnhappinessOnlyAtMidnight) {
        // The original checks (HOUR + hours % 4) == 0, true only at midnight with whole sets of four hours.
        return hour + (hours % every) == 0 ? 1 : 0;
    }
    // Once for each multiple of the interval the skipped hours cross, as playing them would.
    return ((hour + hours) / every) - (hour / every);
}

core::Result<TrainingResult> Partner::train(const TrainingRequest& request, ruleset::GameClock& clock) {
    const TrainingRules& rules = rules_->training;
    const TrainingPlace* place = rules.findPlace(request.place);
    if (place == nullptr) {
        return core::fail(formatMessage(messages::UnknownPlace, request.place));
    }
    const TrainingCourse* course = place->findCourse(request.course);
    if (course == nullptr) {
        return core::fail(formatMessage(messages::UnknownCourse, request.place, request.course));
    }
    if (!alive()) {
        return core::fail(std::string{messages::NotAlive});
    }
    useClock(clock);
    // trn.c: the sessions as a baby or in-training count towards the practice bonus.
    if (place->countsPractice && static_cast<std::int32_t>(species_->level) < rules.practiceLevelBelow) {
        ++state_.training.youngSessions[course->aptitude];
    }
    const StatBlock gains = addStatGains(strategies().gainFormula->gains(
        {.rules = rules, .course = *course, .state = state_, .species = *species_, .request = request}));
    passTrainingHour(*place, *course, clock);
    emit(EventKind::Trained, 1);
    return TrainingResult{.gains = gains};
}

void Partner::passTrainingHour(const TrainingPlace& place, const TrainingCourse& course, ruleset::GameClock& clock) {
    // trn_reward.c: TRN_advanceTrainingTime (and TRN2_ for the second place).
    const TrainingRules& rules = rules_->training;
    MoodState& mood = state_.mood;
    mood.tiredness += course.tiredness;
    state_.hunger.energy -= course.energy;
    mood.happiness -= course.happiness;
    if (mood.tiredness >= rules.overworkTiredness) {
        mood.happiness += rules.overworkHappiness;
        mood.discipline += rules.overworkDiscipline;
    }
    const ruleset::GameTime before = clock.time();
    const std::int32_t hour = (before.hour + 1) % constants::HoursPerDay;
    const ruleset::ClockAdvance advance = clock.jumpTo(hour, before.minute);
    addDays(advance.days, true);
    if (hour % life().unhappyLifetime.everyHours == 0) {
        loseUnhappyLifetime();
    }
    if (state_.conditions.has(Condition::Sleepy)) {
        ++state_.sleep.sleepyHoursForSickness;
        ++state_.sleep.missedSleepHours;
    }
    if (state_.conditions.has(Condition::Hungry)) {
        passHungerWindow(windowSteps(place.hungerWindowMinutes));
    } else {
        state_.hunger.foodLevel -= place.foodMinutes;
    }
    state_.hunger.energy -= species_->energyUsage;
    if (state_.conditions.has(Condition::NeedsToilet)) {
        state_.toilet.urgeFrames -= place.urgeFrames;
    } else {
        state_.toilet.poopLevel -= place.poopSteps;
    }
    spendLifetime(1);
    ++state_.evolution.hours;
    state_.health.injuredHours += state_.conditions.has(Condition::Injured) ? 1 : 0;
    state_.health.sickHours += state_.conditions.has(Condition::Sick) ? 1 : 0;
    clampBoundaries();
}

} // namespace dwe::game::partner
