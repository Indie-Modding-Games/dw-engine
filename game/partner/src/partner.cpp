#include <dwe/game/partner/messages.hpp>
#include <dwe/game/partner/partner.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "partner_constants.hpp"

namespace dwe::game::partner {

using messages::formatMessage;

Partner::Partner(const PartnerRules& rules, const SpeciesTable& species, std::uint64_t seed)
    : rules_(&rules), table_(&species), random_(seed) {}

core::Result<const SpeciesCare*> Partner::findSpecies(std::string_view species) const {
    const SpeciesCare* found = table_->find(species);
    if (found == nullptr) {
        return core::fail(formatMessage(messages::UnknownSpecies, species));
    }
    const std::size_t patterns = care().sleep.patterns.size();
    if (found->sleepPattern < 0 || std::cmp_greater_equal(found->sleepPattern, patterns)) {
        return core::fail(formatMessage(messages::BadSleepPattern, species, found->sleepPattern, patterns));
    }
    return found;
}

core::Result<void> Partner::start(std::string_view species, const PartnerStats& stats,
                                  const ruleset::GameClock& clock) {
    auto found = findSpecies(species);
    if (!found) {
        return std::unexpected{found.error()};
    }
    useClock(clock);
    species_ = *found;
    state_ = {};
    state_.species = species_->id;
    state_.stats = stats;
    events_.clear();
    const ruleset::GameTime now = clock.time();
    // partner.c: initializePartner.
    startCare(now.hour, now.minute);
    state_.toilet.virusBar = 0;
    state_.hunger.energy = species_->energyThreshold;
    state_.training.boost = {};
    state_.evolution = {};
    return {};
}

core::Result<void> Partner::restore(PartnerState state) {
    auto found = findSpecies(state.species);
    if (!found) {
        return std::unexpected{found.error()};
    }
    species_ = *found;
    state_ = std::move(state);
    events_.clear();
    return {};
}

core::Result<void> Partner::changeSpecies(std::string_view species, const ruleset::GameClock& clock) {
    auto found = findSpecies(species);
    if (!found) {
        return std::unexpected{found.error()};
    }
    useClock(clock);
    species_ = *found;
    state_.species = species_->id;
    // evl.c: initializeEvolvedPartner.
    const ruleset::GameTime now = clock.time();
    state_.conditions.clearAll();
    setSleepTimes(now.hour);
    resetCare(now.hour, now.minute);
    state_.life.protectedHour.reset();
    return {};
}

void Partner::startCare(std::int32_t hour, std::int32_t minute) {
    state_.conditions.clearAll();
    setSleepTimes(hour);
    state_.sleep.missedSleepHours = 0;
    resetCare(hour, minute);
    state_.mood.discipline = care().start.discipline;
    state_.mood.happiness = care().start.happiness;
    state_.life.remainingLifetime = life().lifetimeHours;
    state_.life.age = 0;
    state_.life.lives = life().lives;
    state_.life.protectedHour.reset();
    state_.life.phase = LifePhase::Alive;
    state_.evolution.hours = 0;
}

core::Result<void> Partner::rebirth(const ruleset::GameClock& clock, std::int32_t tamerLevel) {
    if (alive()) {
        return core::fail(std::string{messages::StillAlive});
    }
    const std::vector<RebirthBaby>& babies = life().rebirth.babies;
    if (babies.empty()) {
        return core::fail(std::string{messages::NoBabies});
    }
    const std::size_t index = strategies().rebirthChoice->pick(babies, state_, random_);
    if (index >= babies.size()) {
        return core::fail(formatMessage(messages::BadBabyPick, index, babies.size()));
    }
    const RebirthBaby& baby = babies[index];
    auto found = findSpecies(baby.digimon);
    if (!found) {
        return std::unexpected{found.error()};
    }
    useClock(clock);
    // evolution.c: initializeReincarnatedPartner. Only a natural death passes stats on.
    StatBlock base = baby.stats;
    if (state_.life.phase == LifePhase::DiedNaturally) {
        for (const Stat stat : AllStats) {
            base[stat] +=
                state_.stats.base[stat] * tamerLevel * life().rebirth.inheritPercentPerTamerLevel / constants::Percent;
        }
    }
    species_ = *found;
    state_.species = species_->id;
    state_.stats = {.base = base, .currentHp = base[Stat::Hp], .currentMp = base[Stat::Mp]};
    const ruleset::GameTime now = clock.time();
    startCare(now.hour, now.minute);
    state_.evolution.careMistakes = 0;
    state_.evolution.battles = 0;
    emit(EventKind::Reborn, static_cast<std::int32_t>(index));
    return {};
}

std::vector<PartnerEvent> Partner::takeEvents() {
    return std::exchange(events_, {});
}

void Partner::emit(EventKind kind, std::int32_t value) {
    events_.push_back({.kind = kind, .value = value});
}

void Partner::setCondition(Condition condition, EventKind event) {
    if (state_.conditions.has(condition)) {
        return;
    }
    state_.conditions.set(condition);
    emit(event);
}

void Partner::changeMood(const MoodChange& change) {
    state_.mood.happiness += change.happiness;
    state_.mood.discipline += change.discipline;
    state_.mood.tiredness += change.tiredness;
}

void Partner::rollTamerLevel(std::int32_t percent, std::int32_t amount) {
    // tamer.c: addTamerLevel. The session keeps the level and its limits.
    if (random_.below(constants::Percent) < percent) {
        emit(EventKind::TamerLevelChanged, amount);
    }
}

void Partner::countCareMistake(CareMistakeCause cause) {
    if (!strategies().careMistakes->counts(cause, state_, *species_)) {
        return;
    }
    ++state_.evolution.careMistakes;
    emit(EventKind::CareMistake, static_cast<std::int32_t>(cause));
}

void Partner::clampBoundaries() {
    // partner_impl.c: tickConditionBoundaries.
    const CareLimits& limits = care().limits;
    MoodState& mood = state_.mood;
    mood.happiness = std::clamp(mood.happiness, limits.happinessMin, limits.happinessMax);
    mood.discipline = std::clamp(mood.discipline, 0, limits.disciplineMax);
    mood.tiredness = std::clamp(mood.tiredness, 0, limits.tirednessMax);
    state_.hunger.energy = std::clamp(state_.hunger.energy, 0, std::max(species_->energyCap, 0));
    state_.weight = std::clamp(state_.weight, limits.weightMin, limits.weightMax);
}

void Partner::fallSick() {
    state_.conditions.set(Condition::Sick);
    ++state_.health.timesSick;
    state_.health.sickHours = 1;
}

void Partner::cureInjury() {
    state_.conditions.clear(Condition::Injured);
    state_.health.injuredHours = 0;
}

void Partner::cureSickness() {
    state_.conditions.clear(Condition::Sick);
    state_.health.sickHours = 0;
    state_.mood.areaFrames = 0;
}

void Partner::loseLife() {
    --state_.life.lives;
    emit(EventKind::LostLife, state_.life.lives);
    if (state_.life.lives <= 0) {
        state_.life.remainingLifetime = 0;
        die(LifePhase::DiedOtherwise);
    }
}

void Partner::die(LifePhase phase) {
    state_.life.phase = phase;
    emit(EventKind::Died, phase == LifePhase::DiedNaturally ? 1 : 0);
}

void Partner::addLifetime(std::int32_t hours) {
    // item.c: modifyLifetime.
    state_.life.remainingLifetime = std::max(state_.life.remainingLifetime + hours, 0);
}

void Partner::spendLifetime(std::int32_t hours) {
    state_.life.remainingLifetime = std::max(state_.life.remainingLifetime - hours, 0);
}

void Partner::loseUnhappyLifetime() {
    const UnhappyLifetime& unhappy = life().unhappyLifetime;
    if (state_.mood.happiness >= unhappy.happinessBelow) {
        return;
    }
    const std::int32_t hours =
        ((state_.mood.happiness - unhappy.happinessBelow) / -unhappy.happinessDivisor) + unhappy.extraHours;
    spendLifetime(hours);
}

bool Partner::inProtectedHour(std::int32_t hour) const {
    return state_.life.protectedHour == hour;
}

bool Partner::young() const {
    return species_->level < Level::Rookie;
}

const SleepPattern& Partner::sleepPattern() const {
    const std::vector<SleepPattern>& patterns = care().sleep.patterns;
    const auto index = std::min(static_cast<std::size_t>(std::max(species_->sleepPattern, 0)), patterns.size() - 1);
    return patterns[index];
}

std::int64_t Partner::framesPerHour() const {
    return clockRules_.framesPerMinute * ruleset::MinutesPerHour;
}

std::int32_t Partner::windowSteps(std::int32_t minutes) const {
    return static_cast<std::int32_t>(minutes * clockRules_.framesPerMinute / care().hunger.windowStepFrames);
}

std::int32_t Partner::awakeCutsPerHour() const {
    return static_cast<std::int32_t>(ruleset::MinutesPerHour) / care().sleep.awakeCutMinutes;
}

void Partner::setSleepTimes(std::int32_t hour) {
    // partner_impl.c: setSleepTimes.
    const SleepRules& rules = care().sleep;
    const SleepPattern& pattern = sleepPattern();
    SleepState& sleep = state_.sleep;
    if (young()) {
        const std::int32_t base = species_->level == Level::Baby ? rules.babyAwakeHours : rules.inTrainingAwakeHours;
        // The original rolls randomLimit(1), always 0 (the quirk young_sleep_without_variation).
        const std::int32_t variation =
            care().quirks.youngSleepWithoutVariation ? 1 : rules.youngAwakeVariationHours + 1;
        const std::int32_t awake = base + random_.below(variation);
        sleep.sleepyHour = (hour + awake) % constants::HoursPerDay;
        sleep.sleepyMinute = 0;
        sleep.wakeHour = (sleep.sleepyHour + pattern.asleepHours) % constants::HoursPerDay;
        sleep.wakeMinute = 0;
        sleep.awakeHours = awake;
        sleep.asleepHours = pattern.asleepHours;
    } else {
        sleep.sleepyHour = pattern.sleepyHour;
        sleep.sleepyMinute = pattern.sleepyMinute;
        sleep.wakeHour = pattern.wakeHour;
        sleep.wakeMinute = pattern.wakeMinute;
        sleep.awakeHours = pattern.awakeHours;
        sleep.asleepHours = pattern.asleepHours;
    }
    sleep.awakeCuts = sleep.awakeHours * awakeCutsPerHour();
    sleep.sleepyHoursForSickness = 0;
    sleep.tirednessCounter = 0;
}

void Partner::scheduleNextSleep() {
    // partner_impl.c: tickSleepMechanics and handleSleeping set the next night alike.
    SleepState& sleep = state_.sleep;
    if (young()) {
        sleep.sleepyHour = (sleep.wakeHour + sleep.awakeHours) % constants::HoursPerDay;
        sleep.sleepyMinute = 0;
        sleep.wakeHour = (sleep.sleepyHour + sleep.asleepHours) % constants::HoursPerDay;
        sleep.wakeMinute = 0;
        return;
    }
    const SleepPattern& pattern = sleepPattern();
    sleep.sleepyHour = pattern.sleepyHour;
    sleep.sleepyMinute = pattern.sleepyMinute;
    sleep.wakeHour = pattern.wakeHour;
    sleep.wakeMinute = pattern.wakeMinute;
}

std::int32_t Partner::nextAdultMealHour(std::int32_t hour) const {
    // partner_impl.c: setFoodTimer, the later versions' search: after the meal it last set, then the first meal of
    // the day still to come, then the first of tomorrow. -1 ends the list.
    const auto& hours = species_->hungerHours;
    const std::int32_t current = state_.hunger.nextHungerHour;
    const auto last = hours.size() - 1;
    for (std::size_t index = 0; index < hours.size(); ++index) {
        if (hour <= current) {
            if (current < hours[index]) {
                return hours[index];
            }
        } else if (hour < hours[index]) {
            return hours[index];
        }
        if (index == last || hours[index + 1] == constants::NoMealHour) {
            for (std::size_t next = 0; next < hours.size(); ++next) {
                if (hour < hours[next]) {
                    return hours[next];
                }
                if (next == last || hours[next + 1] == constants::NoMealHour) {
                    return hours[0];
                }
            }
        }
    }
    return hours[0];
}

void Partner::setFoodTimer(std::int32_t hour, std::int32_t minute) {
    // partner_impl.c: setFoodTimer.
    const HungerRules& rules = care().hunger;
    std::int32_t next = 0;
    if (species_->level == Level::Baby || species_->level == Level::InTraining) {
        const std::int32_t interval =
            species_->level == Level::Baby ? rules.babyIntervalHours : rules.inTrainingIntervalHours;
        next = ((hour / interval) * interval) + interval;
        if (next >= constants::HoursPerDay) {
            next -= constants::HoursPerDay;
        }
    } else {
        next = nextAdultMealHour(hour);
    }
    state_.hunger.nextHungerHour = next;
    if (hour <= next) {
        state_.hunger.foodLevel = ((next - hour) * constants::MinutesPerHour) - minute;
    } else {
        state_.hunger.foodLevel =
            ((constants::HoursPerDay - hour) * constants::MinutesPerHour) + (next * constants::MinutesPerHour) - minute;
    }
}

void Partner::resetCare(std::int32_t hour, std::int32_t minute) {
    // partner_impl.c: resetPartnerPara.
    state_.toilet.poopLevel = species_->poopInterval;
    state_.toilet.urgeFrames = constants::NoUrge;
    state_.mood.tiredness = 0;
    state_.mood.subTiredness = 0;
    state_.hunger.tiredHungerFrames = 0;
    state_.health = {};
    state_.mood.areaFrames = 0;
    setFoodTimer(hour, minute);
    state_.hunger.windowSteps = constants::NoWindow;
    state_.hunger.emptyStomachMinutes = 0;
    state_.weight = species_->defaultWeight;
    state_.evolution.careMistakes = 0;
    state_.evolution.battles = 0;
}

} // namespace dwe::game::partner
