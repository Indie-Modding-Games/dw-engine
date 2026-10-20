#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/effects.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/rules.hpp>
#include <dwe/game/partner/species.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/random.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace dwe::game::partner {

// What a training session gave.
struct TrainingResult {
    StatBlock gains;
};

// The partner: its state and the care, life and training rules that change it (docs/reference/care.md). Time comes
// from ruleset::GameClock: tick runs the care rules on every frame the clock crossed, so a fast-forward of days does
// the same as playing them. Commands come from the player (feed, scold, sleep...) and from the session (battles,
// time skips, evolution). What happened comes out as events.
class Partner {
public:
    // The rules, the species and the random numbers are the partner's for its lifetime; the seed makes a raising
    // repeatable.
    Partner(const PartnerRules& rules, const SpeciesTable& species, std::uint64_t seed);

    // Starts a new game with a species and its stats (initializePartner).
    core::Result<void> start(std::string_view species, const PartnerStats& stats, const ruleset::GameClock& clock);

    // Takes a loaded state.
    core::Result<void> restore(PartnerState state);

    // New rules after a ruleset change; the state stays.
    void setRules(const PartnerRules& rules) { rules_ = &rules; }

    // Where the partner's random numbers are, for the session's save: a loaded game rolls what the saved one would.
    [[nodiscard]] std::uint64_t randomState() const { return random_.state(); }
    void restoreRandomState(std::uint64_t state) { random_.restoreState(state); }

    [[nodiscard]] const PartnerState& state() const { return state_; }
    [[nodiscard]] const SpeciesCare& species() const { return *species_; }
    [[nodiscard]] bool alive() const { return state_.life.phase == LifePhase::Alive; }

    // Runs the care rules on the frames an advance of the clock crossed (clock.advance or advanceFrames; not
    // jumpTo, which the commands that skip time do themselves).
    void tick(const ruleset::GameClock& clock, const ruleset::ClockAdvance& advance, const CareContext& context);

    // The events since the last call.
    [[nodiscard]] std::vector<PartnerEvent> takeEvents();

    // Whether the partner refuses an offered item; a refusal readies a stronger scold.
    [[nodiscard]] bool refuses(const ItemOffer& offer);

    // Eats a food it did not refuse (item.c: handleFood, then partnerHandleFoodFeed); a meal that ends the hunger
    // sets the next one from the clock's time.
    void eat(const FoodEffect& food, const ruleset::GameClock& clock);

    // A medicine or a bandage (item.c: handleStatusItems, handleMedicineHealing); true if it cured a sickness.
    bool cure(const CureEffect& cure);

    void scold();
    void praise();

    // Whether the partner is sleepy enough to be put to bed.
    [[nodiscard]] bool canSleep() const;

    // Sleeps until its wake hour: the clock jumps there and the night is accounted (partner.c: partnerTickSleep,
    // sleepRegen, handleSleeping). Emits Slept, and NewDay if midnight passed.
    void sleep(ruleset::GameClock& clock, const SleepContext& context);

    // Goes at a toilet (handleToilet).
    void useToilet();

    // The portable potty item (item.c: handlePortaPotty).
    void usePortableToilet();

    // Whether this species eats the poop it finds (tickPoopDetection).
    [[nodiscard]] bool eatsPoop() const;

    // Eats a poop pile of this size (handleEatingPoop).
    void eatPoop(std::int32_t pileSize);

    // A battle ended (tamer.c: startBattle, advanceBattleTime): its effects on the care, and 20 minutes pass.
    void finishBattle(const BattleOutcome& outcome, ruleset::GameClock& clock);

    // A training session: its gains, and an hour passes with its own accounting.
    core::Result<TrainingResult> train(const TrainingRequest& request, ruleset::GameClock& clock);

    // A script's time skip (resting, waiting): the hours' effects on the care, before the session moves the clock
    // (partner_impl.c: skipHours).
    void skipHours(std::int32_t hours, const ruleset::GameClock& clock);

    // The partner entered a map: the area effects count from 0 (setupPartnerOnWarp).
    void enterMap();

    // The partner evolved into another species: its care starts again (evl.c: initializeEvolvedPartner). The
    // evolution system sets the stats and the counters it owns.
    core::Result<void> changeSpecies(std::string_view species, const ruleset::GameClock& clock);

    // The stats after a battle, an evolution or an item.
    void setStats(const PartnerStats& stats) { state_.stats = stats; }

    // Changes the remaining lifetime (item.c: modifyLifetime; +96 hours on reaching ultimate without an item).
    void addLifetime(std::int32_t hours);

    // The evolution counters the evolution system resets.
    void setEvolutionCounters(const EvolutionCounters& counters) { state_.evolution = counters; }

    // After the death: the partner is reborn as a baby (evolution.c: reincarnatePartner,
    // initializeReincarnatedPartner). A natural death passes on a part of the stats, as much as the tamer's level.
    core::Result<void> rebirth(const ruleset::GameClock& clock, std::int32_t tamerLevel);

private:
    // One frame of the clock.
    struct FrameTime {
        std::int64_t frameOfDay = 0;
        std::int64_t day = 0;
        std::int32_t hour = 0;
        std::int32_t minute = 0;
    };

    [[nodiscard]] const CareRules& care() const { return rules_->care; }
    [[nodiscard]] const LifeRules& life() const { return rules_->life; }
    [[nodiscard]] const PartnerStrategies& strategies() const { return rules_->strategies; }
    [[nodiscard]] FrameTime frameAt(std::int64_t elapsedFrame) const;
    [[nodiscard]] static bool every(const FrameTime& time, std::int64_t frames);
    [[nodiscard]] bool everyMinutes(const FrameTime& time, std::int64_t minutes) const;
    [[nodiscard]] bool hourly(const FrameTime& time) const;
    [[nodiscard]] std::int64_t framesPerHour() const;
    [[nodiscard]] std::int32_t windowSteps(std::int32_t minutes) const;
    [[nodiscard]] std::int32_t awakeCutsPerHour() const;
    [[nodiscard]] const SleepPattern& sleepPattern() const;
    [[nodiscard]] bool young() const;
    [[nodiscard]] const SleepyPenalty& sleepyPenalty() const;
    [[nodiscard]] bool inProtectedHour(std::int32_t hour) const;
    [[nodiscard]] core::Result<const SpeciesCare*> findSpecies(std::string_view species) const;
    void useClock(const ruleset::GameClock& clock) { clockRules_ = clock.rules(); }

    void emit(EventKind kind, std::int32_t value = 0);
    void setCondition(Condition condition, EventKind event);
    void changeMood(const MoodChange& change);
    void rollTamerLevel(std::int32_t percent, std::int32_t amount);
    void countCareMistake(CareMistakeCause cause);
    void clampBoundaries();
    void fallSick();
    void loseLife();
    void die(LifePhase phase);
    void spendLifetime(std::int32_t hours);
    void loseUnhappyLifetime();
    void cureInjury();
    void cureSickness();

    // The clock's own rules for a frame (clock.c: tickGameClock).
    void stepClock(const FrameTime& time);
    void startDay(const FrameTime& time);

    // The care rules of a frame (partner_impl.c: tickConditions, tickPoopingMechanic), in the original's order.
    void stepCare(const FrameTime& time, const CareContext& context);
    void tickDeath(const FrameTime& time, const CareContext& context);
    void tickSleep(const FrameTime& time);
    void cutAwakeTime();
    void missSleep();
    void tickSickness(const FrameTime& time, const CareContext& context);
    void tickTiredness(const FrameTime& time, const CareContext& context);
    void tickHunger(const FrameTime& time);
    void closeHungerWindow(std::int32_t hour, std::int32_t minute);
    void tickSulking(const FrameTime& time);
    void loseStatsToSulking();
    void tickTimers();
    void tickUnhappyLifetime(const FrameTime& time);
    void tickVirusBar();
    void tickPoop(const FrameTime& time, const CareContext& context);
    void poopOnGround();
    void leavePoopPile();

    // Schedules (setSleepTimes, setFoodTimer, resetPartnerPara).
    void setSleepTimes(std::int32_t hour);
    void scheduleNextSleep();
    void setFoodTimer(std::int32_t hour, std::int32_t minute);
    [[nodiscard]] std::int32_t nextAdultMealHour(std::int32_t hour) const;
    void resetCare(std::int32_t hour, std::int32_t minute);

    // Sleep.
    void regenerateWhileAsleep(std::int32_t hour, std::int32_t minute, const SleepContext& context);
    [[nodiscard]] std::int32_t hoursUntilWake(std::int32_t hour) const;
    void wakeUp(std::int32_t hour, std::int32_t minute, std::int32_t hoursSlept);

    // Food and the toilet.
    void applyFood(const FoodEffect& food);
    void answerHunger(const FoodEffect& food, std::int32_t hour, std::int32_t minute);
    void rollFoodSickness(std::int32_t percent);
    void loseWeightAfterPoop();
    [[nodiscard]] std::int32_t poopLevelAfterGoing() const;

    // Battles, skipped time and training.
    void addPostBattleTiredness();
    void rollBattleInjury();
    void passBattleTime(ruleset::GameClock& clock);
    void passHungerWindow(std::int32_t steps);
    void passTrainingHour(const TrainingPlace& place, const TrainingCourse& course, ruleset::GameClock& clock);
    void addTrainingGains(const StatBlock& gains);
    void addDays(std::int64_t days, bool ages);

    // Starts the care of a species (initializePartner, initializeEvolvedPartner, initializeReincarnatedPartner).
    void startCare(std::int32_t hour, std::int32_t minute);

    const PartnerRules* rules_;
    const SpeciesTable* table_;
    const SpeciesCare* species_ = nullptr;
    ruleset::ClockRules clockRules_;
    ruleset::Random random_;
    PartnerState state_;
    std::vector<PartnerEvent> events_;
};

} // namespace dwe::game::partner
