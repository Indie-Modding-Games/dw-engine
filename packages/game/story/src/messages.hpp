#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

namespace dwe::game::story::messages {

using core::formatMessage;

// The error and log messages of the story runtime, as std::format templates for
// core::formatMessage.

// A trigger or a variable outside the state (the number, the last valid one).
inline constexpr std::string_view TriggerOutOfRange = "story: trigger {} is outside 0-{}";
inline constexpr std::string_view VariableOutOfRange = "story: variable {} is outside 0-{}";

// The saved story state.
inline constexpr std::string_view StoryStateText = "the story state";
// The triggers of a save are not their hex digits (the count wanted).
inline constexpr std::string_view TriggerDigitsWrong = "triggers must be {} hex digits";
// A saved variable outside a byte (the value).
inline constexpr std::string_view VariableNotAByte = "a variable must be 0-255, not {}";

// The File City screen a finished game continues on: no choice matched the triggers.
inline constexpr std::string_view NoClearedCityMap =
    "no File City screen matches the city's triggers";

// The orders.
inline constexpr std::string_view NoSuchTrigger = "trigger {} is outside the story's {} triggers";
inline constexpr std::string_view NoSuchVariable =
    "variable {} is outside the story's {} variables";
inline constexpr std::string_view NoSuchPlace = "map {} exit {} is not a place a map change can go";
inline constexpr std::string_view NoSuchSection = "section {} is not a section a script can have";

// The runner.
inline constexpr std::string_view NoTicksPerFrame =
    "a frame of the original must last at least one tick";
inline constexpr std::string_view RequestDropped =
    "story: section {:#x} of script {} dropped: script {} is running";
inline constexpr std::string_view SectionFailedLog = "story: script {} section {:#x} failed: {}";
inline constexpr std::string_view SectionMissing = "story: script {} has no section {:#x}: {}";
// script_interp.c: callScriptSection for a section the script lacks (an NPC with none of its own).
inline constexpr std::string_view PortTalkWithoutSection =
    "[port:story] callScriptSection (script_interp.c:2244): script {} has no section {:#x}; the "
    "talk or step ends at once";
inline constexpr std::string_view TooDeep =
    "story: script {} section {:#x} jumps from {} subroutine calls deep, past the guard of {}";
inline constexpr std::string_view ReturnWithNothing =
    "story: script {} section {:#x} returns with nothing to return to";
inline constexpr std::string_view NoSuchScript = "script {} is not a script a map can have";
inline constexpr std::string_view NoMapHeadSection = "story: map {} has no map-head section";
inline constexpr std::string_view MapNotEntered =
    "story: set_script names map {}, but map {} is the one entered";
inline constexpr std::string_view NoMapScript = "story: no map script is set to go on in";
inline constexpr std::string_view NoEntrySection = "story: script {} has no map-entry section: {}";
inline constexpr std::string_view FlowStateFailed = "story: the script flow's state: {}";
inline constexpr std::string_view NeverWaits =
    "story: script {} section {:#x} resumed {} times in one frame";

// Port logs: debug lines with a fixed prefix, one per ported order,
// branch and continuation, so a play log shows the flow; phase 5 thins them out by the prefix.
inline constexpr std::string_view PortBeginScript =
    "[port:story] callScriptSection (script_interp.c:2244): script {} section {:#x} event={} "
    "var-at-start={}";
inline constexpr std::string_view PortEnterMap =
    "[port:story] changeMap (map.c:5331): map {} scripted-change={}";
inline constexpr std::string_view PortMapChangeDone =
    "[port:story] scriptTickChangeMap (map.c:5277): map in, script running={} arrival-now={}";
inline constexpr std::string_view PortSetScript =
    "[port:story] set_script (script_interp.c:2844): script {} map {}";
inline constexpr std::string_view PortSectionOnExit =
    "[port:story] section_on_exit (script_interp.c:2151): section {}";
inline constexpr std::string_view PortWarpTo =
    "[port:story] warp_to (script_interp.c:1402): map {} exit {} arrival {}";
inline constexpr std::string_view PortTeleport =
    "[port:story] teleport (script_interp.c:1534): variables {}/{} give map {} exit {}";
inline constexpr std::string_view PortMapChangeWait =
    "[port:story] map change (script_interp.c:625): map {} exit {} arrival {} show-name={}";
inline constexpr std::string_view PortShowName =
    "[port:story] consumeMapChangeShowName (script_interp.c:860): choice {} event={} -> {}";
inline constexpr std::string_view PortFreeze =
    "[port:story] scriptPauseGame (script_interp.c:2726): first-freeze={}, trigger {} cleared";
inline constexpr std::string_view PortFinish =
    "[port:story] tickScript end (script_interp.c:634): var {} back to {}, release={}";
inline constexpr std::string_view PortPopMapLoaded =
    "[port:story] returnFromScriptFile (script_interp.c:2684): map loaded, entry section of "
    "script {} opened={}";
inline constexpr std::string_view PortPopRunSection =
    "[port:story] returnFromScriptFile (script_interp.c:2684): run section {} of the map script "
    "next";
inline constexpr std::string_view PortMapHeadEnd =
    "[port:story] setMapHeadActive (script_interp.c:2181): no arrival section, return again";
inline constexpr std::string_view PortSectionFailed =
    "[port:story] section failed, ends as end_section; {} continuations left";

inline constexpr std::string_view PortVariableAtStart =
    "[port:story] routine 25 (script_interp.c:1775): var 0 goes back to {} at the script's end";

// story/names (story_names.cpp).
inline constexpr std::string_view NameNumberOutside = "{}: '{}' is not a number from 0 to {}";
inline constexpr std::string_view NameNotAnId =
    "'{}' is not a name id: use lower-case letters, digits and '_'";
inline constexpr std::string_view NameTwice = "{}: the name '{}' is given twice";

} // namespace dwe::game::story::messages
