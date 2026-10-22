#pragma once

#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/structuring.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

// The paths through a section of an event script, each with what it takes to follow it: the decisions of its IFs,
// SWITCHes and SET_SELECTIONs, and the triggers it sets on the way. The walk follows the structured routines
// (structuring.hpp), so a decision whose arms meet again with the same state leaves no trace on the paths after it.
// It is what the extractors that need conditions (the map changes) and the Lua converter build on.
namespace dwe::formats::script {

// Where an instruction is: a script and an index of its listing.
struct ScriptPlace {
    std::uint16_t script = 0;
    std::size_t instruction = 0;

    bool operator==(const ScriptPlace&) const = default;
};

// A decision at an IF: its entries before the jump entry, folded left to right as the interpreter does
// (script_interp.c: scriptIfInstruction), must give `holds`.
struct BranchTerm {
    ScriptPlace place; // the IF
    std::vector<Condition> tests;
    bool holds = true;

    bool operator==(const BranchTerm&) const = default;
};

// A decision at a SWITCH (the variable selects an arm) or a SET_SELECTION (the player picks one; the last arm is a
// cancel).
struct ChoiceTerm {
    ScriptPlace place;
    std::size_t arm = 0;

    bool operator==(const ChoiceTerm&) const = default;
};

struct GuardTerm {
    std::variant<BranchTerm, ChoiceTerm> decision;

    bool operator==(const GuardTerm&) const = default;
};

// What a path takes, in the order it meets the decisions.
struct PathGuard {
    std::vector<GuardTerm> terms;
    // The path goes round a loop or through a state machine: the terms are those of one way through, not all.
    bool unstructured = false;

    bool operator==(const PathGuard&) const = default;
};

// A SET_TRIGGER (set) or UNSET_TRIGGER on the path.
struct TriggerEffect {
    std::uint16_t trigger = 0;
    bool set = true;

    bool operator==(const TriggerEffect&) const = default;
};

// An instruction the walk was asked for, with a path that reaches it.
struct GuardedInstruction {
    ScriptPlace place;
    PathGuard guard;
    std::vector<TriggerEffect> effects; // in the order the path runs them
    std::vector<TriggerEffect> state;   // the triggers the path knows when it gets there, by number

    bool operator==(const GuardedInstruction&) const = default;
};

// A script as the walk reads it.
struct WalkableScript {
    const ScriptListing* listing = nullptr;
    const StructuredScript* structured = nullptr;
};

// Finds a script by id, for jump_to_file and jump_to_file_and_link; nothing for one that does not exist.
using ScriptLookup = std::function<std::optional<WalkableScript>(std::uint16_t script)>;

// Which instructions the walk reports.
using InstructionFilter = std::function<bool(const ListingInstruction& instruction)>;

struct WalkOptions {
    // The triggers known when the section starts, by number. With `othersClear`, every other trigger is known to be
    // clear (a new game: initializeScripts clears them all).
    std::vector<TriggerEffect> knownTriggers;
    bool othersClear = false;
    // A statement that splits the paths into more than this many is followed by one path, unstructured, with the
    // decisions before it and nothing known about the triggers.
    std::size_t maximumPaths = 64;
};

// Walks the section of a script from its entry and returns every instruction the filter takes, once per distinct
// path that reaches it. IFs whose tests are all of triggers the path knows take the arm those give and leave no
// term. Calls run their routine and go on after it; jump_to_file goes on in the other script. Nothing if the script
// or the section does not exist.
std::vector<GuardedInstruction> walkSection(std::uint16_t script, std::uint16_t section, const ScriptLookup& lookup,
                                            const InstructionFilter& filter, const WalkOptions& options = {});

} // namespace dwe::formats::script
