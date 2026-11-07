#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

namespace dwe::assets::messages {

using core::formatMessage;

// What the readers of the base pack's game tables call each part in their errors ("a raise row:
// ...").
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
inline constexpr std::string_view ContentIdTooLong =
    "content id '{}' has a name longer than {} characters";
inline constexpr std::string_view ContentIdCharacters =
    "content id '{}' may only use a-z, 0-9 and '_', with one ':' after a pack's namespace";
inline constexpr std::string_view ContentIdNamespace = "content id '{}': {}";

// Tables composed entry by entry (table_composition.cpp). The first argument is the pack's file.
inline constexpr std::string_view CompositionNotMapping =
    "{}: a table must be a mapping of sections";
inline constexpr std::string_view CompositionNotEntry =
    "{}: every entry of '{}' must be a mapping with its '{}'";
inline constexpr std::string_view CompositionBadKey = "{}: '{}' of '{}': {}";
inline constexpr std::string_view CompositionRepeatedKey = "{}: '{}' lists '{}' twice";
inline constexpr std::string_view NewSpeciesWithoutContract =
    "the new Digimon '{}' has no model contract: a pack that adds a Digimon adds {}.yaml with it";
inline constexpr std::string_view CompositionNotList = "{}: '{}' must be a list of entries";
inline constexpr std::string_view CompositionIndexTaken =
    "{}: '{}' of '{}' has the index {}, which '{}' already has";
inline constexpr std::string_view CompositionBadIndex =
    "{}: '{}' of '{}' has the index '{}', which is no number";
inline constexpr std::string_view CompositionIndexChanged =
    "{}: '{}' of '{}' has the index {}, but it is {}: an entry that replaces another keeps its index";
inline constexpr std::string_view CompositionIndexOfNewEntry =
    "{}: '{}' is new in '{}' and gives an index: leave it out, the engine numbers new entries";
inline constexpr std::string_view CompositionOriginalAdded =
    "{}: '{}' is new in '{}': an entry that {} of the {} layer adds is named '{}:<name>'";
inline constexpr std::string_view CompositionForeignNamespace =
    "{}: '{}' is new in '{}', but its namespace is not the pack's id, '{}'";

// Classes of entries of the rulesets (entry_class.hpp).
inline constexpr std::string_view EntryRangeText = "a range or an id";
inline constexpr std::string_view EntryRangeReversed =
    "the range starts at {} and ends before it, at {}";
inline constexpr std::string_view EntryRangeNegative = "a range counts numbers from 0, not {}";
inline constexpr std::string_view EntryClassNumberUnknown =
    "{}: number {} is no {} of the original (a range covers only the original game; a pack's entry joins by id)";
inline constexpr std::string_view EntryClassIdUnknown = "{}: '{}' is no {} of the packs mounted";

// The problems of a map's warps (map_warps.hpp), with the map and the exit.
inline constexpr std::string_view WarpDead =
    "map '{}', exit {}: the warp leads nowhere: its trigger is in the collision but target_map_id is empty and "
    "target_map ({}) is no map of the original; an editor may have dropped target_map_id, so write the target map's "
    "id there";
inline constexpr std::string_view WarpDisagree =
    "map '{}', exit {}: target_map_id and target_map name different maps ({}); write the same target in both";
// The targets of WarpDisagree: the id, the index and the original's map of that index, or none.
inline constexpr std::string_view WarpDisagreeTargets = "'{}' and {} ('{}')";
inline constexpr std::string_view WarpDisagreeNoMap = "'{}' and {}, no map of the original";
inline constexpr std::string_view WarpUnknownIdByIndex =
    "map '{}', exit {}: target_map_id '{}' is no map of data/maps, so the warp leads to the original's map of "
    "target_map; write a known id there, or leave it out";
inline constexpr std::string_view WarpUnknownTarget =
    "map '{}', exit {}: target '{}' is no map of data/maps, by id or by the original's index";
inline constexpr std::string_view WarpBaseNeedsBoth =
    "map '{}', exit {}: a base pack names a warp's target by both target_map and target_map_id ({})";

// The problems of a map's patch (map_patch.hpp). What a list's entries are named by: the place of
// an NPC slot, a light or an object, an exit's number.
inline constexpr std::string_view MapPatchFailed = "patch {}: {}";
inline constexpr std::string_view MapPatchContext = "map patch";
inline constexpr std::string_view MapPatchEntry =
    "every change of '{}' must be a mapping with its '{}'";
inline constexpr std::string_view MapPatchNegative = "'{}' counts from 0, not {}";
inline constexpr std::string_view MapPatchTwice = "'{}' {} of '{}' is changed twice";
inline constexpr std::string_view MapPatchNoEntry = "the map's data has no '{}' with '{}' {}";
inline constexpr std::string_view MapPatchNoObjects = "the map's data has no objects to change";
inline constexpr std::string_view MapPatchCellSize =
    "a rectangle of cells is at least 1 x 1, not {} x {}";
inline constexpr std::string_view MapPatchCellsOutside =
    "the cells from column {}, row {} ({} x {}) leave the {} x {} collision grid";
inline constexpr std::string_view MapPatchWithoutData =
    "a patch of map '{}', which has no data in any pack";
inline constexpr std::string_view AddedMapWithoutEntry =
    "map '{}' has data but no entry in data/maps, so the game never enters it";
inline constexpr std::string_view AddedMapWithoutData =
    "data/maps adds map '{}', which has no data in any pack";
inline constexpr std::string_view MapWithoutBackground =
    "map '{}' has no background (map/{}/background_<variant>)";

} // namespace dwe::assets::messages
