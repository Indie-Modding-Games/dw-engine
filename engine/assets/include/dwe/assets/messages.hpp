#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

namespace dwe::assets::messages {

using core::formatMessage;

// What the readers of the base pack's game tables call each part in their errors ("a raise row: ...").
inline constexpr std::string_view DigimonTableText = "the digimon table";
inline constexpr std::string_view DigimonRowText = "a digimon row";
inline constexpr std::string_view RaiseTableText = "the raise table";
inline constexpr std::string_view RaiseRowText = "a raise row";
inline constexpr std::string_view ViewText = "a view offset";
inline constexpr std::string_view SpecialsText = "special";
inline constexpr std::string_view MovesText = "moves";
inline constexpr std::string_view HungerTimesText = "hunger_times";
inline constexpr std::string_view MoveTableText = "move table";
inline constexpr std::string_view EvolutionTableText = "evolution table";
inline constexpr std::string_view MoveText = "a move";
inline constexpr std::string_view LearnChancesText = "learn_chances";
inline constexpr std::string_view RequirementText = "an evolution requirement";
inline constexpr std::string_view StatsGainText = "an evolution stats gain";
inline constexpr std::string_view PathText = "an evolution path";
inline constexpr std::string_view ItemTargetText = "an evolution item target";

// The errors of the move and evolution readers, as std::format templates for core::formatMessage.
inline constexpr std::string_view TableParse = "{}: {}";
inline constexpr std::string_view RepeatedEntry = "{} '{}' is listed twice";
inline constexpr std::string_view RepeatedIndex = "{} '{}' repeats the index {}";

// Content ids (content_id.cpp).
inline constexpr std::string_view ContentIdEmpty = "a content id cannot be empty";
inline constexpr std::string_view ContentIdTooLong = "content id '{}' has a name longer than {} characters";
inline constexpr std::string_view ContentIdCharacters =
    "content id '{}' may only use a-z, 0-9 and '_', with one ':' after a pack's namespace";
inline constexpr std::string_view ContentIdNamespace = "content id '{}': {}";

// Tables composed entry by entry (table_composition.cpp). The first argument is the pack's file.
inline constexpr std::string_view CompositionNotMapping = "{}: a table must be a mapping of sections";
inline constexpr std::string_view CompositionNotEntry = "{}: every entry of '{}' must be a mapping with its '{}'";
inline constexpr std::string_view CompositionBadKey = "{}: '{}' of '{}': {}";
inline constexpr std::string_view CompositionRepeatedKey = "{}: '{}' lists '{}' twice";
inline constexpr std::string_view NewSpeciesWithoutContract =
    "the new Digimon '{}' has no model contract: a pack that adds a Digimon adds {}.yaml with it";
inline constexpr std::string_view CompositionNotList = "{}: '{}' must be a list of entries";
inline constexpr std::string_view CompositionIndexTaken = "{}: '{}' of '{}' has the index {}, which '{}' already has";
inline constexpr std::string_view CompositionBadIndex = "{}: '{}' of '{}' has the index '{}', which is no number";
inline constexpr std::string_view CompositionIndexChanged =
    "{}: '{}' of '{}' has the index {}, but it is {}: an entry that replaces another keeps its index";
inline constexpr std::string_view CompositionIndexOfNewEntry =
    "{}: '{}' is new in '{}' and gives an index: leave it out, the engine numbers new entries";
inline constexpr std::string_view CompositionOriginalAdded =
    "{}: '{}' is new in '{}': an entry that {} of the {} layer adds is named '{}:<name>'";
inline constexpr std::string_view CompositionForeignNamespace =
    "{}: '{}' is new in '{}', but its namespace is not the pack's id, '{}'";

} // namespace dwe::assets::messages
