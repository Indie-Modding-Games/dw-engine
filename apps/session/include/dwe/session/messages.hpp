#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

// The error and log messages of the session, as std::format templates for core::formatMessage.
namespace dwe::session::messages {

using core::formatMessage;

// The session's ruleset sections.
inline constexpr std::string_view NewGameSectionText = "the new_game section";
inline constexpr std::string_view TamerSectionText = "the tamer section";
inline constexpr std::string_view StatsText = "stats";
inline constexpr std::string_view NegativeValue = "{} cannot be negative";
inline constexpr std::string_view InFile = "{}: {}";
inline constexpr std::string_view LevelLimitsReversed = "level_min ({}) is above level_max ({})";
inline constexpr std::string_view StartLevelOutside = "tamer_level {} is outside the tamer section's {} to {}";
inline constexpr std::string_view ItemUseSectionText = "the item_use section";
inline constexpr std::string_view ItemRangeText = "an item range";
inline constexpr std::string_view FixedLevelText = "a fixed-level evolution item";
inline constexpr std::string_view ItemRangeReversed = "the range starts at {} and ends before it, at {}";
inline constexpr std::string_view PartnerMovesSectionText = "the partner_moves section";
inline constexpr std::string_view MoveSlotOutside = "move position {} is outside the species' list of {}";

// Setting up a session.
inline constexpr std::string_view NoFiles = "a session needs the packs";
inline constexpr std::string_view RulesetFailed = "ruleset '{}': {}";
inline constexpr std::string_view SpeciesFailed = "the species tables: {}";
inline constexpr std::string_view SaveSettingsFailed = "save/settings: {}";
inline constexpr std::string_view NewGameStatsFailed = "data/new_game_stats: {}";
inline constexpr std::string_view UnknownStartSpecies = "new_game: partner '{}' has no raise row";

// Saving and loading.
inline constexpr std::string_view NoStorage = "this session has no save storage";
inline constexpr std::string_view NoPartner = "no game is running: start a new game or load one";
inline constexpr std::string_view LoadFailed = "loading {}: {}";
inline constexpr std::string_view SaveFailed = "saving {}: {}";
inline constexpr std::string_view AutosaveFailed = "autosave: {}";
inline constexpr std::string_view Autosaved = "session: autosaved to {}";
inline constexpr std::string_view RulesChanged = "session: ruleset revision {} read";
inline constexpr std::string_view RulesRefused =
    "session: the ruleset's new values cannot be used, the old ones stay: {}";
inline constexpr std::string_view RebirthFailed = "session: the partner could not be reborn: {}";
inline constexpr std::string_view NotSleepy = "the partner is not sleepy";

// The session's save sections.
inline constexpr std::string_view ClockStateText = "the clock state";
inline constexpr std::string_view TamerStateText = "the tamer state";
inline constexpr std::string_view FieldStateText = "the field state";
inline constexpr std::string_view RandomStateText = "the random state";
inline constexpr std::string_view NotAFraction = "'{}' is not a fraction of a frame";
inline constexpr std::string_view NotARandomState = "'{}' is not a random state";
inline constexpr std::string_view MissingRandomKey = "the random state has no '{}'";

// The evolution adapter.
inline constexpr std::string_view EvolutionNeedsTables = "the evolution adapter needs the packs or its tables";
inline constexpr std::string_view EvolutionNotRead = "the evolution rules have not been read";
inline constexpr std::string_view EvolutionNotApplied = "evolution: the partner could not become '{}': {}";
inline constexpr std::string_view UnknownEvolutionForm = "evolution: no form '{}' in the tables";

// The inventory adapter.
inline constexpr std::string_view InventoryNeedsTables = "the inventory adapter needs the packs or its tables";
inline constexpr std::string_view InventoryNotRead = "the inventory rules have not been read";
inline constexpr std::string_view UnknownStartingItem = "bag.new_game_items: item {} is not in data/items";
inline constexpr std::string_view UnknownItem = "item {} is not in data/items";

// The battle adapter and the partner's moves.
inline constexpr std::string_view PartnerMovesStateText = "the partner's moves";
inline constexpr std::string_view EquippedCount = "the partner's moves have {} equipped positions, not {}";
inline constexpr std::string_view BattleNeedsTables = "the battle adapter needs the packs or its tables";
inline constexpr std::string_view BattleNotRead = "the battle rules have not been read";
inline constexpr std::string_view BattleWithoutBag = "the battle adapter has no inventory to throw items from";
inline constexpr std::string_view NoMoveTable = "the ruleset names no {} table";
inline constexpr std::string_view UnknownPartnerSpecies = "battle: the partner's species '{}' is not in data/digimon";
inline constexpr std::string_view MovePlaceOutside = "battle: place {} is not one of the {} equipped places";
inline constexpr std::string_view MoveNotLearned = "battle: the partner has not learned '{}'";
inline constexpr std::string_view NoMoveAtSlot = "battle: '{}' has no move at position {}";

} // namespace dwe::session::messages
