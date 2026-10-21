#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

namespace dwe::game::partner::messages {

using core::formatMessage;

// The error messages of the partner module, as std::format templates for core::formatMessage.

// What the readers call each part of the sections in their errors.
inline constexpr std::string_view CareSectionText = "the care section";
inline constexpr std::string_view LifeSectionText = "the life section";
inline constexpr std::string_view TrainingSectionText = "the training section";
inline constexpr std::string_view GroupText = "{}";
inline constexpr std::string_view SleepPatternText = "a sleep pattern";
inline constexpr std::string_view PoopMealText = "a poop meal";
inline constexpr std::string_view RebirthBabyText = "a rebirth baby";
inline constexpr std::string_view RatioText = "a ratio";
inline constexpr std::string_view StatsText = "stats";
inline constexpr std::string_view PlaceText = "a training place";
inline constexpr std::string_view CourseText = "a training course";
inline constexpr std::string_view GainText = "a training gain";
inline constexpr std::string_view StrategiesText = "strategies";

// What each strategy is, for the registries' errors.
inline constexpr std::string_view CareMistakeRuleText = "care mistake rule";
inline constexpr std::string_view SicknessRollText = "sickness roll";
inline constexpr std::string_view ItemRefusalText = "item refusal rule";
inline constexpr std::string_view RebirthChoiceText = "rebirth choice";
inline constexpr std::string_view GainFormulaText = "training gain formula";

// Values.
inline constexpr std::string_view NotPositive = "{} must be more than zero";
inline constexpr std::string_view Negative = "{} must not be negative";
inline constexpr std::string_view UnknownStat = "unknown stat '{}'; known: hp, mp, offense, defense, speed, brain";
inline constexpr std::string_view MinAboveMax = "{} must not be above {}";
inline constexpr std::string_view NoneListed = "{} must list at least one";
inline constexpr std::string_view RepeatedId = "{} '{}' is listed twice";
inline constexpr std::string_view EatingNotRising = "the eating tiers must rise in below_size";

// Species.
inline constexpr std::string_view RaiseWithoutDigimon = "the raise row of '{}' has no digimon row";
inline constexpr std::string_view NoTableRole = "ruleset {} names no '{}' table";
inline constexpr std::string_view UnknownSpecies = "'{}' has no raise row";
inline constexpr std::string_view BadSleepPattern = "'{}' has sleep cycle {}, but the care section has {} patterns";
inline constexpr std::string_view BadTrainingAptitude = "'{}' has training type {}, but there are {} tiers";

// Commands.
inline constexpr std::string_view UnknownPlace = "unknown training place '{}'";
inline constexpr std::string_view UnknownCourse = "training place '{}' has no course '{}'";
inline constexpr std::string_view NotAlive = "the partner is not alive";
inline constexpr std::string_view StillAlive = "the partner is still alive";
inline constexpr std::string_view NoBabies = "the life section lists no rebirth babies";
inline constexpr std::string_view BadBabyPick = "the rebirth choice picked baby {} of {}";

// The saved state.
inline constexpr std::string_view StateText = "the partner state";
inline constexpr std::string_view StateVersionText = "version";
inline constexpr std::string_view UnsupportedVersion =
    "the partner state is version {}; this build reads version {} (older ones are migrated by the save)";
inline constexpr std::string_view MissingStateKey = "the partner state has no '{}'";
inline constexpr std::string_view UnknownEnum = "unknown {} '{}'";

} // namespace dwe::game::partner::messages
