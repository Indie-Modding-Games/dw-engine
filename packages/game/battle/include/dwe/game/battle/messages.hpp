#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

namespace dwe::game::battle::messages {

using core::formatMessage;

// The error messages of the battle module, as std::format templates for core::formatMessage.

// The section.
inline constexpr std::string_view SectionText = "the battle section";
inline constexpr std::string_view NotNegative = "{} cannot be negative";
inline constexpr std::string_view NotPositive = "{} must be more than zero";
inline constexpr std::string_view NotPercent = "{} must be a percentage from 0 to 100";
inline constexpr std::string_view NotOrdered = "{}: {} must not be more than {}";
inline constexpr std::string_view NotAffinityKey = "'{}' is not an affinity";
inline constexpr std::string_view NotSpeciesMap = "'{}' is not a map of species to species";
inline constexpr std::string_view UnknownMenuEntry = "unknown menu entry '{}'";
inline constexpr std::string_view MenusNotSorted =
    "the menus must go from brain 0 up, without repeating a brain";
inline constexpr std::string_view DiscountsNotSorted =
    "the MP discounts must go from the highest brain down";
inline constexpr std::string_view NoEnemyFactors =
    "rewards.enemy_factors needs one factor per enemy, up to limits.max_enemies";
inline constexpr std::string_view HitAfterAttack =
    "timing.hit_frame must come before timing.attack_frames ends";
inline constexpr std::string_view ShotAfterAttack =
    "pacing.flattened_shot_release_frame must come before pacing.flattened_shot_frames ends";

// The tables and the setup.
inline constexpr std::string_view UnknownMove = "the move '{}' is not in the moves table";
inline constexpr std::string_view UnknownSpecies = "the Digimon '{}' is not in the Digimon table";
inline constexpr std::string_view RuleMoveMissing =
    "the battle section names the move '{}', which the moves table lacks";
inline constexpr std::string_view NoSpeciesMove =
    "loadBattleData (btl.c:466): {} has no move {} in its list, the slot equipped as {} is empty";
inline constexpr std::string_view NoEnemies = "a battle needs from 1 to {} enemies, not {}";
inline constexpr std::string_view NotFinisherSlot =
    "{}: the fourth slot is for a finisher, not '{}'";
inline constexpr std::string_view FinisherInTechniqueSlot =
    "{}: '{}' is a finisher and can only go in the fourth slot";
inline constexpr std::string_view NoMaxHp = "{} needs a maximum HP of at least 1";

// The commands.
inline constexpr std::string_view CommandNotInMenu = "the command is not in the menu now";
inline constexpr std::string_view BattleOver = "the battle is over";
inline constexpr std::string_view NoCommands = "the partner takes no commands in this battle";
inline constexpr std::string_view FinisherNotReady = "the finisher gauge is not full";
inline constexpr std::string_view VersusShape =
    "a versus match needs exactly one enemy, a time limit and the versus kind";
inline constexpr std::string_view NotCommanded = "that fighter takes no commands in this battle";
inline constexpr std::string_view BagOpen = "the tamer's bag is open";
inline constexpr std::string_view Fleeing =
    "the partner is running away and takes no more commands";
inline constexpr std::string_view BagUnavailable =
    "the tamer's bag only opens while the partner is not running away and no thrown item is on its way";
inline constexpr std::string_view NotRevivable =
    "the partner can only be revived after it falls and before the countdown ends";

} // namespace dwe::game::battle::messages
