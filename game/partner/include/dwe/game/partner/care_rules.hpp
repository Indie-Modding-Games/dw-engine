#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::partner {

// The care section (rules/<id>/care.yaml, docs/reference/care.md). The units are in the names, as in the YAML:
// frames of the original's loop, game minutes and hours, percents.
inline constexpr std::string_view CareSection = "care";

struct CareLimits {
    std::int32_t happinessMin = 0;
    std::int32_t happinessMax = 0;
    std::int32_t disciplineMax = 0;
    std::int32_t tirednessMax = 0;
    std::int32_t weightMin = 0;
    std::int32_t weightMax = 0;
    std::int32_t hpMpMax = 0;
    std::int32_t statMax = 0;
    std::int32_t statMin = 0;
    // The most a single gain may raise a stat, whatever raises it.
    std::int32_t statGainMax = 0;
};

struct CareStart {
    std::int32_t discipline = 0;
    std::int32_t happiness = 0;
};

// A change of happiness, discipline and tiredness.
struct MoodChange {
    std::int32_t happiness = 0;
    std::int32_t discipline = 0;
    std::int32_t tiredness = 0;
};

struct HungerRules {
    std::int32_t babyIntervalHours = 0;
    std::int32_t inTrainingIntervalHours = 0;
    std::int32_t windowMinutes = 0;
    std::int32_t windowStepFrames = 0;
    std::int32_t starvingWeightLossMinutes = 0;
    std::int32_t tiredHungerTiredness = 0;
    std::int32_t tiredHungerFrames = 0;
    std::int32_t tiredHungerMinutes = 0;
    MoodChange fed;
    MoodChange overfed;
    std::int32_t favoriteEnergyPercent = 0;
    std::int32_t favoriteHappiness = 0;
};

struct RefusalRules {
    std::int32_t disciplineBase = 0;
    std::int32_t disciplineBonusMin = 0;
    std::int32_t disciplineBonusRange = 0;
    std::int32_t favoriteRefusalPercent = 0;
};

// A sleep schedule of SLEEP_PATTERN.
struct SleepPattern {
    std::int32_t sleepyHour = 0;
    std::int32_t sleepyMinute = 0;
    std::int32_t wakeHour = 0;
    std::int32_t wakeMinute = 0;
    std::int32_t awakeHours = 0;
    std::int32_t asleepHours = 0;
};

// What being sleepy and not in bed costs, every so many frames.
struct SleepyPenalty {
    std::int32_t everyFrames = 0;
    std::int32_t happiness = 0;
    std::int32_t discipline = 0;
};

struct SleepRegeneration {
    std::int32_t hpMpPercent = 0;
    std::int32_t hpMpPercentRange = 0;
    std::int32_t tirednessPercent = 0;
    std::int32_t tirednessPercentRange = 0;
    std::int32_t weightDivisor = 0;
    std::int32_t pillowPercent = 0;
    std::int32_t likedAreaPercent = 0;
    std::int32_t dislikedAreaPercent = 0;
};

struct SleepRules {
    std::int32_t tirednessPercentPerMinute = 0;
    std::int32_t tirednessThreshold = 0;
    std::int32_t awakeCutMinutes = 0;
    std::int32_t babyAwakeHours = 0;
    std::int32_t inTrainingAwakeHours = 0;
    std::int32_t youngAwakeVariationHours = 0;
    std::vector<SleepPattern> patterns;
    SleepyPenalty babySleepyPenalty;
    SleepyPenalty inTrainingSleepyPenalty;
    SleepyPenalty adultSleepyPenalty;
    std::int32_t missedSleepTamerLevelPercent = 0;
    SleepRegeneration regeneration;
    MoodChange sickPenalty;
    MoodChange injuredPenalty;
    std::int32_t hungryWeightLoss = 0;
};

// Eating a poop pile of a size range (handleEatingPoop).
struct PoopMeal {
    // The tier applies below this size; the largest tier has none.
    std::int32_t belowSize = 0;
    std::int32_t hpDivisor = 0;
    std::int32_t mpDivisor = 0;
    std::int32_t weight = 0;
    std::int32_t curePercent = 0;
};

struct ToiletRules {
    std::int32_t poopStepMinutes = 0;
    std::int32_t urgeDisciplineOffset = 0;
    std::int32_t urgeFramesPerHundred = 0;
    MoodChange toilet;
    MoodChange wild;
    std::int32_t wildTamerLevelPercent = 0;
    std::int32_t weightLossRoll = 0;
    std::int32_t weightLossDivisor = 0;
    std::int32_t virusBarLimit = 0;
    // The Digimon id of the virus form, which gets no virus.
    std::string virusForm;
    std::vector<std::string> poopEaters;
    std::vector<PoopMeal> eating;
    PoopMeal eatingLargest;
    std::int32_t pilesPerMap = 0;
    std::int32_t pilesTotal = 0;
};

struct SicknessRules {
    std::int32_t missedSleepHours = 0;
    std::int32_t triesOffset = 0;
    std::int32_t triesPercent = 0;
    std::int32_t tirednessOffset = 0;
    std::int32_t dislikedAreaHours = 0;
    std::int32_t injuryHours = 0;
    std::int32_t untreatedHours = 0;
    std::int32_t onSickHappiness = 0;
    MoodChange hourlyPenalty;
};

struct MoodRules {
    MoodChange scold;
    MoodChange boostedScold;
    MoodChange praise;
    std::int32_t praiseDisciplineDivisor = 0;
    std::int32_t sulkCheckFrames = 0;
    std::int32_t sulkRecoveryFrames = 0;
    std::int32_t sulkStatLossFrames = 0;
    std::int32_t sulkStatLossPercent = 0;
    std::int32_t areaEffectFrames = 0;
    MoodChange likedArea;
    MoodChange dislikedArea;
    std::int32_t areaTimerLimitFrames = 0;
};

struct TirednessRules {
    std::int32_t tiredThreshold = 0;
    std::int32_t tiredHappiness = 0;
    std::int32_t tiredHappinessFrames = 0;
    std::int32_t subTirednessThreshold = 0;
};

struct YearRules {
    std::int32_t happyHappiness = 0;
    std::int32_t happyTamerLevelPercent = 0;
    std::int32_t unhappyTamerLevelPercent = 0;
};

// The original's oddities of the care section (docs/reference/care.md, "Rarezas"); true reproduces the original.
struct CareQuirks {
    bool sleepingHungryKeepsWeight = true;
    bool sleepAddsDefaultHoursToEvolution = true;
    bool sicknessChanceWraps = true;
    bool youngSleepWithoutVariation = true;
    bool battleMidnightSkipsAge = true;
    bool toiletDoublesPoopInterval = true;
    bool foodClearsTrainingBoost = true;
    bool skippedHungerWindowCountsTwice = true;
    bool skippedHoursAddSicknessTries = true;
    bool skippedHoursUnhappinessOnlyAtMidnight = true;
};

// The names of the strategies the section picks.
struct CareStrategyNames {
    std::string careMistakes;
    std::string sicknessRoll;
    std::string itemRefusal;
};

struct CareRules {
    CareLimits limits;
    CareStart start;
    HungerRules hunger;
    RefusalRules refusal;
    SleepRules sleep;
    ToiletRules toilet;
    SicknessRules sickness;
    MoodRules mood;
    TirednessRules tiredness;
    YearRules year;
    CareStrategyNames strategies;
    CareQuirks quirks;
};

// Reads the care section; errors name the file, line and column.
core::Result<CareRules> readCareRules(const ruleset::Section& section);

} // namespace dwe::game::partner
