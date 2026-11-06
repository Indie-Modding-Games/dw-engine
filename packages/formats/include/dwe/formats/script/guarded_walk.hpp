#pragma once

#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/structuring.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

// The paths through a section of an event script, each with what it takes to follow it: the
// decisions of its IFs, SWITCHes and SET_SELECTIONs, and the triggers it sets on the way. The walk
// follows the structured routines (structuring.hpp), so a decision whose arms meet again with the
// same state leaves no trace on the paths after it. It is what the extractors that need conditions
// (the map changes) and the Lua converter build on.
namespace dwe::formats::script {

// Where an instruction is: a script and an index of its listing.
struct ScriptPlace {
  std::uint16_t script = 0;
  std::size_t instruction = 0;

  bool operator==(const ScriptPlace&) const = default;
};

// A decision at an IF: its entries before the jump entry, folded left to right as the interpreter
// does (script_interp.c: scriptIfInstruction), must give `holds`.
struct BranchTerm {
  ScriptPlace place; // the IF
  std::vector<Condition> tests;
  bool holds = true;

  bool operator==(const BranchTerm&) const = default;
};

// A decision at a SWITCH (the variable selects an arm) or a SET_SELECTION (the player picks one;
// the last arm is a cancel).
struct ChoiceTerm {
  ScriptPlace place;
  std::size_t arm = 0;
  bool selection = false; // the player picks it (SET_SELECTION), not a variable

  bool operator==(const ChoiceTerm&) const = default;
};

// A call of a routine the walk is already in. The walk does not follow it again (it would never
// end), so the path goes on after the call knowing no trigger: the decisions the recursion would
// take are not known.
struct RecursionTerm {
  ScriptPlace routine; // the first instruction of the routine called again

  bool operator==(const RecursionTerm&) const = default;
};

// An instruction that runs game code which sets or clears triggers: a routine of the game that
// writes some (call_routine, call_digimon_routine; routine_writes.hpp), give_item (its result in
// trigger 0) or compare_date (its result in its trigger). The path goes on not knowing those
// triggers, so a test of one after it is not decided by what the path or the field knew before. It
// is no decision: it only says where that knowledge ends.
struct ForgetTerm {
  ScriptPlace place;                   // the instruction
  std::vector<std::uint16_t> triggers; // the triggers it may write, by number; empty: any

  bool operator==(const ForgetTerm&) const = default;
};

struct GuardTerm {
  std::variant<BranchTerm, ChoiceTerm, RecursionTerm, ForgetTerm> decision;

  bool operator==(const GuardTerm&) const = default;
};

// What a path takes, in the order it meets the decisions.
struct PathGuard {
  std::vector<GuardTerm> terms;
  // The path goes round a loop or through a state machine: the terms are those of one way through,
  // not all.
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
  // Whether the listing's offsets are the script's: false for a story module read back without a
  // line map that matches it (lua_reader.hpp), whose offsets mean nothing outside it.
  bool offsets = true;
};

// Finds a script by id, for jump_to_file and jump_to_file_and_link; nothing for one that does not
// exist.
using ScriptLookup = std::function<std::optional<WalkableScript>(std::uint16_t script)>;

// Which instructions the walk reports.
using InstructionFilter = std::function<bool(const ListingInstruction& instruction)>;

// How many paths go on through a statement before they are merged (WalkOptions::maximumPaths).
inline constexpr std::size_t DefaultMaximumPaths = 64;
// How many times a walk follows a routine in full (WalkOptions::maximumRoutineWalks).
inline constexpr std::size_t DefaultMaximumRoutineWalks = 16;

// What a walk did, and a deadline to stop it (for the tests that measure the walk).
struct WalkMonitor {
  std::optional<std::chrono::steady_clock::time_point> deadline; // the walk stops when it passes it
  bool stopped = false;            // it passed the deadline: what the walk gave is not all
  std::size_t merges = 0;          // times more than maximumPaths paths were merged into one
  std::size_t summarizedCalls = 0; // calls of a routine that went through its summary
  std::size_t summaryWalks = 0;    // walks of a routine made for a summary
  std::size_t recursionCuts =
      0; // calls of a routine the walk was already in, which it did not follow
};

struct WalkOptions {
  // The triggers known when the section starts, by number. With `othersClear`, every other trigger
  // is known to be clear (a new game: initializeScripts clears them all).
  std::vector<TriggerEffect> knownTriggers;
  bool othersClear = false;
  // When a statement leaves more paths than this, they go on as one path, unstructured, with the
  // decisions and the effects they all begin with and the triggers they all know alike. Each
  // instruction the paths reached they still reach (a trigger they do not all know alike is not
  // known, so both arms of a test of it are walked); a map change after the merge is never certain
  // (unstructured), but a decision all the paths took stays.
  std::size_t maximumPaths = DefaultMaximumPaths;
  // A routine walked this many times is summarized for every later call: each path that gets to it
  // goes through the walk of the routine from what that path knows, made once for all the paths
  // that know the same, put after the path's decisions and effects. That is what walking it in full
  // from the path gives, since the walk of a routine depends on its path only by what the path
  // knows; only the merges of maximumPaths can differ, as they depend on how many paths go through
  // together. Without it, chains of routines that call each other from several arms are walked once
  // per path through them, which grows without end on some scripts.
  std::size_t maximumRoutineWalks = DefaultMaximumRoutineWalks;
  WalkMonitor* monitor = nullptr;
};

// Walks the section of a script from its entry and returns every instruction the filter takes, once
// per distinct path that reaches it. IFs whose tests are all of triggers the path knows take the
// arm those give and leave no term. Calls run their routine and go on after it; jump_to_file goes
// on in the other script. Nothing if the script or the section does not exist.
std::vector<GuardedInstruction> walkSection(std::uint16_t script, std::uint16_t section,
                                            const ScriptLookup& lookup,
                                            const InstructionFilter& filter,
                                            const WalkOptions& options = {});

} // namespace dwe::formats::script
