#pragma once

#include <string_view>

namespace dwe::game::field::messages {

// field/rules: a frame count or a shade of `transition` below 1 (the key's name).
inline constexpr std::string_view TransitionCountTooLow = "transition: {} must be at least 1";
// field/rules: a shade of `transition` beyond what a color channel holds (the limit).
inline constexpr std::string_view TransitionShadeTooHigh =
    "transition: shade_max and shade_full must be at most {}";

// field/triggers: a band whose first value is past its last (the band's name).
inline constexpr std::string_view TriggerBandBackwards = "{}: first is past last";
// field/triggers: the blocked bit is not the map format's (the format's bit).
inline constexpr std::string_view TriggerBlockedBitFixed =
    "blocked_bit is fixed by the map format: it must be {}";
// field/triggers: a band with values that are no trigger cells (the band, the highest trigger).
inline constexpr std::string_view TriggerBandOutside = "{}: the values must be 1 to {}";
// field/triggers: two bands share values (their names).
inline constexpr std::string_view TriggerBandsOverlap = "{} and {} share values";
// field/triggers: more exit values than a map has exits (the most).
inline constexpr std::string_view TriggerTooManyExits = "exits: a map has at most {} exits";

// field/script_warps: a cell of a map whose scripts change the map only on paths the field cannot
// decide yet (a test of something else than a trigger, a choice, a loop) or to a map in a variable
// (the map, the cell's value).
inline constexpr std::string_view ScriptWarpUndecided =
    "field: the scripts of {} change the map from cell {} only on paths that depend on what the field does not track "
    "yet; not taken";

// field/script_warps: what the vanilla policy (UnknownPolicy::Allow) lets a warp through, named in
// its log: a recursion the extraction's walk cut (its site), a trigger the new game's paths do not
// agree on (its number).
inline constexpr std::string_view RecursionAllowed =
    "a recursion the extraction did not follow ({})";
inline constexpr std::string_view UnknownTriggerAllowed =
    "trigger {}, which the new game's paths do not agree on";
// A trigger the path's recursion or game code may have written (the trigger, what: one of the sites
// below).
inline constexpr std::string_view ForgottenTriggerAllowed = "trigger {}, which {} may have written";
inline constexpr std::string_view RecursionSite = "the recursion at {}";
inline constexpr std::string_view GameCodeSite = "the game code at {}";

// field/script_warps: a warp taken under the vanilla policy, once per warp (the map, the cell's
// value, the warp's source, what the policy let through).
inline constexpr std::string_view ScriptWarpAllowed =
    "field: the scripts of {} change the map from cell {} ({}) although the field does not know {}; taken, as the "
    "original would not lose the exit";

// field/script_warps: a cell whose warps the vanilla policy takes go to more than one place, and
// none of them is taken without the policy, once per cell (the map, the cell's value, the places,
// the source of the warp taken: the first).
inline constexpr std::string_view ScriptWarpAmbiguous =
    "field: the scripts of {} change the map from cell {} to {} on paths the field does not tell apart; taken the "
    "first ({})";

// Free exploration (ScriptWarpSetup::explore): a map change the original's conditions would not
// take now, taken (the map, the cell's value, the place's map and exit, the warp's source).
inline constexpr std::string_view ExplorationOpened =
    "field: free exploration opens the way from {} cell {} to {} exit {} ({}), which the story keeps closed now";
// A cell whose scripts go to more than one place: the player chooses (the map, the cell's value,
// how many places).
inline constexpr std::string_view ExplorationChoice =
    "field: free exploration offers {} cell {}'s {} places";
inline constexpr std::string_view ExplorationOn = "field: free exploration on";
inline constexpr std::string_view ExplorationOff =
    "field: free exploration off; the story's conditions decide again";

} // namespace dwe::game::field::messages
