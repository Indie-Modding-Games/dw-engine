#include <dwe/game/partner/care_rules.hpp>
#include <dwe/game/partner/messages.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "section_reading.hpp"

namespace dwe::game::partner {

namespace {

using assets::YamlReader;
using core::YamlNode;
using messages::formatMessage;
using namespace reading;

// The groups of the care section.
constexpr std::string_view LimitsKey = "limits";
constexpr std::string_view StartKey = "start";
constexpr std::string_view HungerKey = "hunger";
constexpr std::string_view RefusalKey = "refusal";
constexpr std::string_view SleepKey = "sleep";
constexpr std::string_view ToiletKey = "toilet";
constexpr std::string_view SicknessKey = "sickness";
constexpr std::string_view MoodKey = "mood";
constexpr std::string_view TirednessGroupKey = "tiredness";
constexpr std::string_view BattleKey = "battle";
constexpr std::string_view YearKey = "year";
constexpr std::string_view StrategiesKey = "strategies";

// limits
constexpr std::string_view HappinessMinKey = "happiness_min";
constexpr std::string_view HappinessMaxKey = "happiness_max";
constexpr std::string_view DisciplineMaxKey = "discipline_max";
constexpr std::string_view TirednessMaxKey = "tiredness_max";
constexpr std::string_view WeightMinKey = "weight_min";
constexpr std::string_view WeightMaxKey = "weight_max";
constexpr std::string_view HpMpMaxKey = "hp_mp_max";
constexpr std::string_view StatMaxKey = "stat_max";
constexpr std::string_view StatMinKey = "stat_min";

// hunger
constexpr std::string_view BabyIntervalKey = "baby_interval_hours";
constexpr std::string_view InTrainingIntervalKey = "in_training_interval_hours";
constexpr std::string_view WindowMinutesKey = "window_minutes";
constexpr std::string_view WindowStepFramesKey = "window_step_frames";
constexpr std::string_view StarvingWeightLossKey = "starving_weight_loss_minutes";
constexpr std::string_view TiredHungerTirednessKey = "tired_hunger_tiredness";
constexpr std::string_view TiredHungerFramesKey = "tired_hunger_frames";
constexpr std::string_view TiredHungerMinutesKey = "tired_hunger_minutes";
constexpr std::string_view FedHappinessKey = "fed_happiness";
constexpr std::string_view FedDisciplineKey = "fed_discipline";
constexpr std::string_view OverfedHappinessKey = "overfed_happiness";
constexpr std::string_view OverfedDisciplineKey = "overfed_discipline";
constexpr std::string_view FavoriteEnergyKey = "favorite_energy_percent";
constexpr std::string_view FavoriteHappinessKey = "favorite_happiness";

// refusal
constexpr std::string_view DisciplineBaseKey = "discipline_base";
constexpr std::string_view DisciplineBonusMinKey = "discipline_bonus_min";
constexpr std::string_view DisciplineBonusRangeKey = "discipline_bonus_range";
constexpr std::string_view FavoriteRefusalKey = "favorite_refusal_percent";

// sleep
constexpr std::string_view TirednessPerMinuteKey = "tiredness_percent_per_minute";
constexpr std::string_view TirednessThresholdKey = "tiredness_threshold";
constexpr std::string_view AwakeCutMinutesKey = "awake_cut_minutes";
constexpr std::string_view BabyAwakeKey = "baby_awake_hours";
constexpr std::string_view InTrainingAwakeKey = "in_training_awake_hours";
constexpr std::string_view YoungVariationKey = "young_awake_variation_hours";
constexpr std::string_view PatternsKey = "patterns";
constexpr std::string_view BabyPenaltyKey = "baby_sleepy_penalty";
constexpr std::string_view InTrainingPenaltyKey = "in_training_sleepy_penalty";
constexpr std::string_view AdultPenaltyKey = "adult_sleepy_penalty";
constexpr std::string_view MissedSleepTamerKey = "missed_sleep_tamer_level_percent";
constexpr std::string_view RegenerationKey = "regeneration";
constexpr std::string_view SickPenaltyKey = "sick_penalty";
constexpr std::string_view InjuredPenaltyKey = "injured_penalty";
constexpr std::string_view HungryWeightLossKey = "hungry_weight_loss";
constexpr std::string_view SleepyHourKey = "sleepy_hour";
constexpr std::string_view SleepyMinuteKey = "sleepy_minute";
constexpr std::string_view WakeHourKey = "wake_hour";
constexpr std::string_view WakeMinuteKey = "wake_minute";
constexpr std::string_view AwakeHoursKey = "awake_hours";
constexpr std::string_view AsleepHoursKey = "asleep_hours";
constexpr std::string_view EveryFramesKey = "every_frames";
constexpr std::string_view HpMpPercentKey = "hp_mp_percent";
constexpr std::string_view HpMpRangeKey = "hp_mp_percent_range";
constexpr std::string_view TirednessPercentKey = "tiredness_percent";
constexpr std::string_view TirednessRangeKey = "tiredness_percent_range";
constexpr std::string_view WeightDivisorKey = "weight_divisor";
constexpr std::string_view PillowKey = "pillow_percent";
constexpr std::string_view LikedAreaPercentKey = "liked_area_percent";
constexpr std::string_view DislikedAreaPercentKey = "disliked_area_percent";

// toilet
constexpr std::string_view PoopStepKey = "poop_step_minutes";
constexpr std::string_view UrgeOffsetKey = "urge_discipline_offset";
constexpr std::string_view UrgeFramesKey = "urge_frames_per_hundred";
constexpr std::string_view ToiletHappinessKey = "toilet_happiness";
constexpr std::string_view ToiletDisciplineKey = "toilet_discipline";
constexpr std::string_view WildHappinessKey = "wild_happiness";
constexpr std::string_view WildDisciplineKey = "wild_discipline";
constexpr std::string_view WildTamerKey = "wild_tamer_level_percent";
constexpr std::string_view WeightLossRollKey = "weight_loss_roll";
constexpr std::string_view WeightLossDivisorKey = "weight_loss_divisor";
constexpr std::string_view VirusBarLimitKey = "virus_bar_limit";
constexpr std::string_view VirusFormKey = "virus_form";
constexpr std::string_view PoopEatersKey = "poop_eaters";
constexpr std::string_view EatingKey = "eating";
constexpr std::string_view EatingLargestKey = "eating_largest";
constexpr std::string_view PilesPerMapKey = "piles_per_map";
constexpr std::string_view PilesTotalKey = "piles_total";
constexpr std::string_view BelowSizeKey = "below_size";
constexpr std::string_view HpDivisorKey = "hp_divisor";
constexpr std::string_view MpDivisorKey = "mp_divisor";
constexpr std::string_view WeightKey = "weight";
constexpr std::string_view CurePercentKey = "cure_percent";

// sickness
constexpr std::string_view MissedSleepHoursKey = "missed_sleep_hours";
constexpr std::string_view TriesOffsetKey = "tries_offset";
constexpr std::string_view TriesPercentKey = "tries_percent";
constexpr std::string_view TirednessOffsetKey = "tiredness_offset";
constexpr std::string_view DislikedAreaHoursKey = "disliked_area_hours";
constexpr std::string_view InjuryHoursKey = "injury_hours";
constexpr std::string_view UntreatedHoursKey = "untreated_hours";
constexpr std::string_view OnSickHappinessKey = "on_sick_happiness";
constexpr std::string_view HourlyPenaltyKey = "hourly_penalty";

// mood
constexpr std::string_view ScoldDisciplineKey = "scold_discipline";
constexpr std::string_view ScoldHappinessKey = "scold_happiness";
constexpr std::string_view BoostedScoldDisciplineKey = "boosted_scold_discipline";
constexpr std::string_view BoostedScoldHappinessKey = "boosted_scold_happiness";
constexpr std::string_view PraiseDisciplineKey = "praise_discipline";
constexpr std::string_view PraiseHappinessKey = "praise_happiness";
constexpr std::string_view PraiseDivisorKey = "praise_discipline_divisor";
constexpr std::string_view SulkCheckKey = "sulk_check_frames";
constexpr std::string_view SulkRecoveryKey = "sulk_recovery_frames";
constexpr std::string_view SulkStatLossFramesKey = "sulk_stat_loss_frames";
constexpr std::string_view SulkStatLossPercentKey = "sulk_stat_loss_percent";
constexpr std::string_view AreaEffectFramesKey = "area_effect_frames";
constexpr std::string_view LikedAreaKey = "liked_area";
constexpr std::string_view DislikedAreaKey = "disliked_area";
constexpr std::string_view AreaTimerLimitKey = "area_timer_limit_frames";

// tiredness
constexpr std::string_view TiredThresholdKey = "tired_threshold";
constexpr std::string_view TiredHappinessKey = "tired_happiness";
constexpr std::string_view TiredHappinessFramesKey = "tired_happiness_frames";
constexpr std::string_view SubTirednessThresholdKey = "sub_tiredness_threshold";

// battle
constexpr std::string_view WonHappinessKey = "won_happiness";
constexpr std::string_view FledKey = "fled";
constexpr std::string_view LostKey = "lost";
constexpr std::string_view BattleTirednessKey = "tiredness";
constexpr std::string_view LowMpPercentKey = "low_mp_percent";
constexpr std::string_view LowMpTirednessKey = "low_mp_tiredness";
constexpr std::string_view FoodMinutesKey = "food_minutes";
constexpr std::string_view DurationMinutesKey = "duration_minutes";
constexpr std::string_view PoopStepsKey = "poop_steps";

// year
constexpr std::string_view HappyHappinessKey = "happy_happiness";
constexpr std::string_view HappyTamerKey = "happy_tamer_level_percent";
constexpr std::string_view UnhappyTamerKey = "unhappy_tamer_level_percent";

// strategies
constexpr std::string_view CareMistakesKey = "care_mistakes";
constexpr std::string_view SicknessRollKey = "sickness_roll";
constexpr std::string_view ItemRefusalKey = "item_refusal";

// quirks
constexpr std::string_view SleepingHungryKeepsWeight = "sleeping_hungry_keeps_weight";
constexpr std::string_view SleepAddsDefaultHours = "sleep_adds_default_hours_to_evolution";
constexpr std::string_view SicknessChanceWraps = "sickness_chance_wraps";
constexpr std::string_view YoungSleepWithoutVariation = "young_sleep_without_variation";
constexpr std::string_view BattleMidnightSkipsAge = "battle_midnight_skips_age";
constexpr std::string_view ToiletDoublesPoopInterval = "toilet_doubles_poop_interval";
constexpr std::string_view PostBattleMpTruncated = "post_battle_mp_tiredness_truncated";
constexpr std::string_view FoodClearsTrainingBoost = "food_clears_training_boost";
constexpr std::string_view SkippedHungerCountsTwice = "skipped_hunger_window_counts_twice";
constexpr std::string_view SkippedHoursAddTries = "skipped_hours_add_sickness_tries";

CareLimits readLimits(YamlReader& reader, const YamlNode& root) {
    const YamlNode* node = nullptr;
    if (!group(reader, root, LimitsKey,
               {HappinessMinKey, HappinessMaxKey, DisciplineMaxKey, TirednessMaxKey, WeightMinKey, WeightMaxKey,
                HpMpMaxKey, StatMaxKey, StatMinKey},
               node)) {
        return {};
    }
    CareLimits limits{.happinessMin = integer(reader, *node, HappinessMinKey),
                      .happinessMax = integer(reader, *node, HappinessMaxKey),
                      .disciplineMax = positive(reader, *node, DisciplineMaxKey),
                      .tirednessMax = positive(reader, *node, TirednessMaxKey),
                      .weightMin = positive(reader, *node, WeightMinKey),
                      .weightMax = positive(reader, *node, WeightMaxKey),
                      .hpMpMax = positive(reader, *node, HpMpMaxKey),
                      .statMax = positive(reader, *node, StatMaxKey),
                      .statMin = positive(reader, *node, StatMinKey)};
    if (reader.ok() && limits.happinessMin > limits.happinessMax) {
        reader.fail(*node, formatMessage(messages::MinAboveMax, HappinessMinKey, HappinessMaxKey));
    }
    if (reader.ok() && limits.weightMin > limits.weightMax) {
        reader.fail(*node, formatMessage(messages::MinAboveMax, WeightMinKey, WeightMaxKey));
    }
    if (reader.ok() && limits.statMin > limits.statMax) {
        reader.fail(*node, formatMessage(messages::MinAboveMax, StatMinKey, StatMaxKey));
    }
    return limits;
}

CareStart readStart(YamlReader& reader, const YamlNode& root) {
    const YamlNode* node = nullptr;
    if (!group(reader, root, StartKey, {DisciplineKey, HappinessKey}, node)) {
        return {};
    }
    return {.discipline = integer(reader, *node, DisciplineKey), .happiness = integer(reader, *node, HappinessKey)};
}

HungerRules readHunger(YamlReader& reader, const YamlNode& root) {
    const YamlNode* node = nullptr;
    if (!group(reader, root, HungerKey,
               {BabyIntervalKey, InTrainingIntervalKey, WindowMinutesKey, WindowStepFramesKey, StarvingWeightLossKey,
                TiredHungerTirednessKey, TiredHungerFramesKey, TiredHungerMinutesKey, FedHappinessKey, FedDisciplineKey,
                OverfedHappinessKey, OverfedDisciplineKey, FavoriteEnergyKey, FavoriteHappinessKey},
               node)) {
        return {};
    }
    return {.babyIntervalHours = positive(reader, *node, BabyIntervalKey),
            .inTrainingIntervalHours = positive(reader, *node, InTrainingIntervalKey),
            .windowMinutes = positive(reader, *node, WindowMinutesKey),
            .windowStepFrames = positive(reader, *node, WindowStepFramesKey),
            .starvingWeightLossMinutes = positive(reader, *node, StarvingWeightLossKey),
            .tiredHungerTiredness = nonNegative(reader, *node, TiredHungerTirednessKey),
            .tiredHungerFrames = nonNegative(reader, *node, TiredHungerFramesKey),
            .tiredHungerMinutes = nonNegative(reader, *node, TiredHungerMinutesKey),
            .fed = {.happiness = integer(reader, *node, FedHappinessKey),
                    .discipline = integer(reader, *node, FedDisciplineKey)},
            .overfed = {.happiness = integer(reader, *node, OverfedHappinessKey),
                        .discipline = integer(reader, *node, OverfedDisciplineKey)},
            .favoriteEnergyPercent = nonNegative(reader, *node, FavoriteEnergyKey),
            .favoriteHappiness = integer(reader, *node, FavoriteHappinessKey)};
}

RefusalRules readRefusal(YamlReader& reader, const YamlNode& root) {
    const YamlNode* node = nullptr;
    if (!group(reader, root, RefusalKey,
               {DisciplineBaseKey, DisciplineBonusMinKey, DisciplineBonusRangeKey, FavoriteRefusalKey}, node)) {
        return {};
    }
    return {.disciplineBase = integer(reader, *node, DisciplineBaseKey),
            .disciplineBonusMin = integer(reader, *node, DisciplineBonusMinKey),
            .disciplineBonusRange = nonNegative(reader, *node, DisciplineBonusRangeKey),
            .favoriteRefusalPercent = nonNegative(reader, *node, FavoriteRefusalKey)};
}

SleepPattern readPattern(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(
            node, messages::SleepPatternText,
            {SleepyHourKey, SleepyMinuteKey, WakeHourKey, WakeMinuteKey, AwakeHoursKey, AsleepHoursKey})) {
        return {};
    }
    return {.sleepyHour = nonNegative(reader, node, SleepyHourKey),
            .sleepyMinute = nonNegative(reader, node, SleepyMinuteKey),
            .wakeHour = nonNegative(reader, node, WakeHourKey),
            .wakeMinute = nonNegative(reader, node, WakeMinuteKey),
            .awakeHours = positive(reader, node, AwakeHoursKey),
            .asleepHours = positive(reader, node, AsleepHoursKey)};
}

SleepyPenalty readPenalty(YamlReader& reader, const YamlNode& parent, std::string_view key) {
    const YamlNode* node = nullptr;
    if (!group(reader, parent, key, {EveryFramesKey, HappinessKey, DisciplineKey}, node)) {
        return {};
    }
    return {.everyFrames = positive(reader, *node, EveryFramesKey),
            .happiness = integer(reader, *node, HappinessKey),
            .discipline = integer(reader, *node, DisciplineKey)};
}

SleepRegeneration readRegeneration(YamlReader& reader, const YamlNode& parent) {
    const YamlNode* node = nullptr;
    if (!group(reader, parent, RegenerationKey,
               {HpMpPercentKey, HpMpRangeKey, TirednessPercentKey, TirednessRangeKey, WeightDivisorKey, PillowKey,
                LikedAreaPercentKey, DislikedAreaPercentKey},
               node)) {
        return {};
    }
    return {.hpMpPercent = nonNegative(reader, *node, HpMpPercentKey),
            .hpMpPercentRange = nonNegative(reader, *node, HpMpRangeKey),
            .tirednessPercent = nonNegative(reader, *node, TirednessPercentKey),
            .tirednessPercentRange = nonNegative(reader, *node, TirednessRangeKey),
            .weightDivisor = positive(reader, *node, WeightDivisorKey),
            .pillowPercent = nonNegative(reader, *node, PillowKey),
            .likedAreaPercent = nonNegative(reader, *node, LikedAreaPercentKey),
            .dislikedAreaPercent = nonNegative(reader, *node, DislikedAreaPercentKey)};
}

SleepRules readSleep(YamlReader& reader, const YamlNode& root) {
    const YamlNode* node = nullptr;
    if (!group(reader, root, SleepKey,
               {TirednessPerMinuteKey, TirednessThresholdKey, AwakeCutMinutesKey, BabyAwakeKey, InTrainingAwakeKey,
                YoungVariationKey, PatternsKey, BabyPenaltyKey, InTrainingPenaltyKey, AdultPenaltyKey,
                MissedSleepTamerKey, RegenerationKey, SickPenaltyKey, InjuredPenaltyKey, HungryWeightLossKey},
               node)) {
        return {};
    }
    SleepRules rules;
    rules.tirednessPercentPerMinute = nonNegative(reader, *node, TirednessPerMinuteKey);
    rules.tirednessThreshold = positive(reader, *node, TirednessThresholdKey);
    rules.awakeCutMinutes = positive(reader, *node, AwakeCutMinutesKey);
    rules.babyAwakeHours = nonNegative(reader, *node, BabyAwakeKey);
    rules.inTrainingAwakeHours = nonNegative(reader, *node, InTrainingAwakeKey);
    rules.youngAwakeVariationHours = nonNegative(reader, *node, YoungVariationKey);
    const YamlNode& patterns = reader.field(*node, PatternsKey);
    if (reader.ok()) {
        for (const YamlNode& pattern : reader.sequence(patterns, PatternsKey)) {
            rules.patterns.push_back(readPattern(reader, pattern));
        }
        if (reader.ok() && rules.patterns.empty()) {
            reader.fail(patterns, formatMessage(messages::NoneListed, PatternsKey));
        }
    }
    rules.babySleepyPenalty = readPenalty(reader, *node, BabyPenaltyKey);
    rules.inTrainingSleepyPenalty = readPenalty(reader, *node, InTrainingPenaltyKey);
    rules.adultSleepyPenalty = readPenalty(reader, *node, AdultPenaltyKey);
    rules.missedSleepTamerLevelPercent = nonNegative(reader, *node, MissedSleepTamerKey);
    rules.regeneration = readRegeneration(reader, *node);
    rules.sickPenalty = moodChange(reader, *node, SickPenaltyKey);
    rules.injuredPenalty = moodChange(reader, *node, InjuredPenaltyKey);
    rules.hungryWeightLoss = nonNegative(reader, *node, HungryWeightLossKey);
    return rules;
}

PoopMeal readMeal(YamlReader& reader, const YamlNode& node, bool largest) {
    if (largest) {
        if (!reader.expectKeys(node, messages::PoopMealText, {HpDivisorKey, MpDivisorKey, WeightKey, CurePercentKey})) {
            return {};
        }
    } else if (!reader.expectKeys(node, messages::PoopMealText,
                                  {BelowSizeKey, HpDivisorKey, MpDivisorKey, WeightKey, CurePercentKey})) {
        return {};
    }
    return {.belowSize = largest ? 0 : positive(reader, node, BelowSizeKey),
            .hpDivisor = positive(reader, node, HpDivisorKey),
            .mpDivisor = positive(reader, node, MpDivisorKey),
            .weight = integer(reader, node, WeightKey),
            .curePercent = nonNegative(reader, node, CurePercentKey)};
}

std::vector<std::string> readNames(YamlReader& reader, const YamlNode& parent, std::string_view key) {
    std::vector<std::string> names;
    const YamlNode& node = reader.field(parent, key);
    if (!reader.ok()) {
        return names;
    }
    for (const YamlNode& item : reader.sequence(node, key)) {
        names.push_back(reader.text(item));
    }
    return names;
}

ToiletRules readToilet(YamlReader& reader, const YamlNode& root) {
    const YamlNode* node = nullptr;
    if (!group(reader, root, ToiletKey,
               {PoopStepKey, UrgeOffsetKey, UrgeFramesKey, ToiletHappinessKey, ToiletDisciplineKey, WildHappinessKey,
                WildDisciplineKey, WildTamerKey, WeightLossRollKey, WeightLossDivisorKey, VirusBarLimitKey,
                VirusFormKey, PoopEatersKey, EatingKey, EatingLargestKey, PilesPerMapKey, PilesTotalKey},
               node)) {
        return {};
    }
    ToiletRules rules;
    rules.poopStepMinutes = positive(reader, *node, PoopStepKey);
    rules.urgeDisciplineOffset = integer(reader, *node, UrgeOffsetKey);
    rules.urgeFramesPerHundred = nonNegative(reader, *node, UrgeFramesKey);
    rules.toilet = {.happiness = integer(reader, *node, ToiletHappinessKey),
                    .discipline = integer(reader, *node, ToiletDisciplineKey)};
    rules.wild = {.happiness = integer(reader, *node, WildHappinessKey),
                  .discipline = integer(reader, *node, WildDisciplineKey)};
    rules.wildTamerLevelPercent = nonNegative(reader, *node, WildTamerKey);
    rules.weightLossRoll = nonNegative(reader, *node, WeightLossRollKey);
    rules.weightLossDivisor = positive(reader, *node, WeightLossDivisorKey);
    rules.virusBarLimit = positive(reader, *node, VirusBarLimitKey);
    const YamlNode& form = reader.field(*node, VirusFormKey);
    rules.virusForm = reader.ok() ? reader.text(form) : std::string{};
    rules.poopEaters = readNames(reader, *node, PoopEatersKey);
    const YamlNode& eating = reader.field(*node, EatingKey);
    if (reader.ok()) {
        for (const YamlNode& meal : reader.sequence(eating, EatingKey)) {
            rules.eating.push_back(readMeal(reader, meal, false));
            if (reader.ok() && rules.eating.size() > 1 &&
                rules.eating.back().belowSize <= rules.eating[rules.eating.size() - 2].belowSize) {
                reader.fail(meal, messages::EatingNotRising);
            }
        }
    }
    const YamlNode& largest = reader.field(*node, EatingLargestKey);
    if (reader.ok()) {
        rules.eatingLargest = readMeal(reader, largest, true);
    }
    rules.pilesPerMap = positive(reader, *node, PilesPerMapKey);
    rules.pilesTotal = positive(reader, *node, PilesTotalKey);
    return rules;
}

SicknessRules readSickness(YamlReader& reader, const YamlNode& root) {
    const YamlNode* node = nullptr;
    if (!group(reader, root, SicknessKey,
               {MissedSleepHoursKey, TriesOffsetKey, TriesPercentKey, TirednessOffsetKey, DislikedAreaHoursKey,
                InjuryHoursKey, UntreatedHoursKey, OnSickHappinessKey, HourlyPenaltyKey},
               node)) {
        return {};
    }
    return {.missedSleepHours = positive(reader, *node, MissedSleepHoursKey),
            .triesOffset = integer(reader, *node, TriesOffsetKey),
            .triesPercent = nonNegative(reader, *node, TriesPercentKey),
            .tirednessOffset = integer(reader, *node, TirednessOffsetKey),
            .dislikedAreaHours = positive(reader, *node, DislikedAreaHoursKey),
            .injuryHours = positive(reader, *node, InjuryHoursKey),
            .untreatedHours = positive(reader, *node, UntreatedHoursKey),
            .onSickHappiness = integer(reader, *node, OnSickHappinessKey),
            .hourlyPenalty = moodChange(reader, *node, HourlyPenaltyKey)};
}

MoodRules readMood(YamlReader& reader, const YamlNode& root) {
    const YamlNode* node = nullptr;
    if (!group(reader, root, MoodKey,
               {ScoldDisciplineKey, ScoldHappinessKey, BoostedScoldDisciplineKey, BoostedScoldHappinessKey,
                PraiseDisciplineKey, PraiseHappinessKey, PraiseDivisorKey, SulkCheckKey, SulkRecoveryKey,
                SulkStatLossFramesKey, SulkStatLossPercentKey, AreaEffectFramesKey, LikedAreaKey, DislikedAreaKey,
                AreaTimerLimitKey},
               node)) {
        return {};
    }
    return {.scold = {.happiness = integer(reader, *node, ScoldHappinessKey),
                      .discipline = integer(reader, *node, ScoldDisciplineKey)},
            .boostedScold = {.happiness = integer(reader, *node, BoostedScoldHappinessKey),
                             .discipline = integer(reader, *node, BoostedScoldDisciplineKey)},
            .praise = {.happiness = integer(reader, *node, PraiseHappinessKey),
                       .discipline = integer(reader, *node, PraiseDisciplineKey)},
            .praiseDisciplineDivisor = positive(reader, *node, PraiseDivisorKey),
            .sulkCheckFrames = positive(reader, *node, SulkCheckKey),
            .sulkRecoveryFrames = positive(reader, *node, SulkRecoveryKey),
            .sulkStatLossFrames = positive(reader, *node, SulkStatLossFramesKey),
            .sulkStatLossPercent = nonNegative(reader, *node, SulkStatLossPercentKey),
            .areaEffectFrames = positive(reader, *node, AreaEffectFramesKey),
            .likedArea = moodChange(reader, *node, LikedAreaKey),
            .dislikedArea = moodChange(reader, *node, DislikedAreaKey),
            .areaTimerLimitFrames = positive(reader, *node, AreaTimerLimitKey)};
}

TirednessRules readTiredness(YamlReader& reader, const YamlNode& root) {
    const YamlNode* node = nullptr;
    if (!group(reader, root, TirednessGroupKey,
               {TiredThresholdKey, TiredHappinessKey, TiredHappinessFramesKey, SubTirednessThresholdKey}, node)) {
        return {};
    }
    return {.tiredThreshold = nonNegative(reader, *node, TiredThresholdKey),
            .tiredHappiness = integer(reader, *node, TiredHappinessKey),
            .tiredHappinessFrames = positive(reader, *node, TiredHappinessFramesKey),
            .subTirednessThreshold = positive(reader, *node, SubTirednessThresholdKey)};
}

BattleCareRules readBattle(YamlReader& reader, const YamlNode& root) {
    const YamlNode* node = nullptr;
    if (!group(reader, root, BattleKey,
               {WonHappinessKey, FledKey, LostKey, BattleTirednessKey, LowMpPercentKey, LowMpTirednessKey,
                FoodMinutesKey, DurationMinutesKey, PoopStepsKey},
               node)) {
        return {};
    }
    return {.wonHappiness = integer(reader, *node, WonHappinessKey),
            .fled = moodChange(reader, *node, FledKey),
            .lost = moodChange(reader, *node, LostKey),
            .tiredness = integer(reader, *node, BattleTirednessKey),
            .lowMpPercent = nonNegative(reader, *node, LowMpPercentKey),
            .lowMpTiredness = nonNegative(reader, *node, LowMpTirednessKey),
            .foodMinutes = nonNegative(reader, *node, FoodMinutesKey),
            .durationMinutes = positive(reader, *node, DurationMinutesKey),
            .poopSteps = nonNegative(reader, *node, PoopStepsKey)};
}

YearRules readYear(YamlReader& reader, const YamlNode& root) {
    const YamlNode* node = nullptr;
    if (!group(reader, root, YearKey, {HappyHappinessKey, HappyTamerKey, UnhappyTamerKey}, node)) {
        return {};
    }
    return {.happyHappiness = integer(reader, *node, HappyHappinessKey),
            .happyTamerLevelPercent = nonNegative(reader, *node, HappyTamerKey),
            .unhappyTamerLevelPercent = nonNegative(reader, *node, UnhappyTamerKey)};
}

CareStrategyNames readStrategyNames(YamlReader& reader, const YamlNode& root) {
    const YamlNode* node = nullptr;
    if (!group(reader, root, StrategiesKey, {CareMistakesKey, SicknessRollKey, ItemRefusalKey}, node)) {
        return {};
    }
    const auto name = [&](std::string_view key) {
        const YamlNode& value = reader.field(*node, key);
        return reader.ok() ? reader.text(value) : std::string{};
    };
    return {.careMistakes = name(CareMistakesKey),
            .sicknessRoll = name(SicknessRollKey),
            .itemRefusal = name(ItemRefusalKey)};
}

CareQuirks readCareQuirks(YamlReader& reader, const YamlNode& root) {
    const YamlNode& node = reader.field(root, ruleset::QuirksKey);
    if (!reader.ok()) {
        return {};
    }
    const ruleset::Quirks quirks = ruleset::readQuirks(
        reader, node,
        {SleepingHungryKeepsWeight, SleepAddsDefaultHours, SicknessChanceWraps, YoungSleepWithoutVariation,
         BattleMidnightSkipsAge, ToiletDoublesPoopInterval, PostBattleMpTruncated, FoodClearsTrainingBoost,
         SkippedHungerCountsTwice, SkippedHoursAddTries});
    return {.sleepingHungryKeepsWeight = quirks.on(SleepingHungryKeepsWeight),
            .sleepAddsDefaultHoursToEvolution = quirks.on(SleepAddsDefaultHours),
            .sicknessChanceWraps = quirks.on(SicknessChanceWraps),
            .youngSleepWithoutVariation = quirks.on(YoungSleepWithoutVariation),
            .battleMidnightSkipsAge = quirks.on(BattleMidnightSkipsAge),
            .toiletDoublesPoopInterval = quirks.on(ToiletDoublesPoopInterval),
            .postBattleMpTirednessTruncated = quirks.on(PostBattleMpTruncated),
            .foodClearsTrainingBoost = quirks.on(FoodClearsTrainingBoost),
            .skippedHungerWindowCountsTwice = quirks.on(SkippedHungerCountsTwice),
            .skippedHoursAddSicknessTries = quirks.on(SkippedHoursAddTries)};
}

} // namespace

core::Result<CareRules> readCareRules(const ruleset::Section& section) {
    YamlReader reader;
    const YamlNode& root = section.node;
    if (!reader.expectKeys(root, messages::CareSectionText,
                           {LimitsKey, StartKey, HungerKey, RefusalKey, SleepKey, ToiletKey, SicknessKey, MoodKey,
                            TirednessGroupKey, BattleKey, YearKey, StrategiesKey, ruleset::QuirksKey})) {
        return std::unexpected{reader.error(section.file)};
    }
    CareRules rules{.limits = readLimits(reader, root),
                    .start = readStart(reader, root),
                    .hunger = readHunger(reader, root),
                    .refusal = readRefusal(reader, root),
                    .sleep = readSleep(reader, root),
                    .toilet = readToilet(reader, root),
                    .sickness = readSickness(reader, root),
                    .mood = readMood(reader, root),
                    .tiredness = readTiredness(reader, root),
                    .battle = readBattle(reader, root),
                    .year = readYear(reader, root),
                    .strategies = readStrategyNames(reader, root),
                    .quirks = readCareQuirks(reader, root)};
    return reader.finish(std::move(rules), section.file);
}

} // namespace dwe::game::partner
