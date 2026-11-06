#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

namespace dwe::game::evolution::messages {

using core::formatMessage;

// The error messages of the evolution module, as std::format templates for core::formatMessage.

// What is being read.
inline constexpr std::string_view SectionText = "the evolution section";
inline constexpr std::string_view CheckHoursText = "check_hours";
inline constexpr std::string_view RequirementsText = "requirements";
inline constexpr std::string_view FallbackText = "fallback";
inline constexpr std::string_view SpecialsText = "specials";
inline constexpr std::string_view SpecialText = "a special rule";
inline constexpr std::string_view RequireText = "require";
inline constexpr std::string_view ComparisonText = "a comparison";
inline constexpr std::string_view TimerSpecialText = "praise_scold_on_timer";
inline constexpr std::string_view ItemsText = "items";
inline constexpr std::string_view StatGainsText = "stat_gains";
inline constexpr std::string_view MovesText = "moves";
inline constexpr std::string_view RebirthText = "rebirth";
inline constexpr std::string_view SceneText = "scene";
inline constexpr std::string_view SceneCameraText = "scene.camera";
inline constexpr std::string_view SceneClipsText = "scene.clips";
inline constexpr std::string_view FrameSpanText = "a span of frames";
inline constexpr std::string_view EmptyFrameSpan = "the span from frame {} to {} is empty";
inline constexpr std::string_view SceneOutOfOrder =
    "the scene's frames must come in order: dim_frames, camera_aim_end, camera_move_end, "
    "burst_frame, grow.to, swap_frame, end_frame";
inline constexpr std::string_view StateText = "the evolution state";

// Strategies.
inline constexpr std::string_view ScoringWhat = "evolution scoring";
inline constexpr std::string_view SelectionWhat = "evolution selection";

// Problems.
inline constexpr std::string_view NotMapping = "{} must be a mapping";
inline constexpr std::string_view NotPositive = "{} must be 1 or more";
inline constexpr std::string_view OneComparison =
    "a comparison must have exactly one of equals and at_least";
inline constexpr std::string_view MapByIdOnly =
    "map compares with equals and a map id of data/maps";
inline constexpr std::string_view UnknownSpecialField = "unknown requirement '{}'";
inline constexpr std::string_view UnknownForm = "{}: unknown form '{}'";
inline constexpr std::string_view UnknownItem = "{}: unknown item '{}'";
inline constexpr std::string_view UnknownMove = "{}: form {} has unknown move '{}'";
inline constexpr std::string_view UnknownReference = "{}: unknown {} '{}'";
inline constexpr std::string_view NoRulesetTable = "ruleset {} names no table for '{}'";

// Where the problem is, for the rules' form and item ids.
inline constexpr std::string_view BabyTargetsWhere = "baby_targets";
inline constexpr std::string_view FallbackWhere = "fallback.rookie_target";
inline constexpr std::string_view SpecialWhere = "specials";
inline constexpr std::string_view ItemsWhere = "items.fixed";
inline constexpr std::string_view ScaledWhere = "stat_gains.scaled_targets";
inline constexpr std::string_view RebirthWhere = "rebirth.babies";
inline constexpr std::string_view DigimonWhat = "digimon";
inline constexpr std::string_view ItemWhat = "item";

// The move classes of the rules (checkMoveClasses), and the table they name.
inline constexpr std::string_view MasteredMovesRule =
    "evolution requirements.mastered_moves_below, mastered_move_ids";
inline constexpr std::string_view FinishersRule =
    "evolution moves.finisher_first to finisher_end, finisher_ids";
inline constexpr std::string_view SpecialtyBlockRule =
    "evolution moves.specialty_blocks[{}], specialty_block_ids";
inline constexpr std::string_view MoveText = "move";
inline constexpr std::string_view FinisherEndNotAfterFirst =
    "finisher_end {} must be after finisher_first {}";
inline constexpr std::string_view SpecialtyWithoutBlock =
    "specialty_block_ids: specialty {} has no block";

} // namespace dwe::game::evolution::messages
