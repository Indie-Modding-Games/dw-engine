#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/script/control_flow.hpp>
#include <dwe/formats/script/script_listing.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

// Structured control flow for each routine of a script, without jumps: blocks of instructions, if/else, loops with
// break, multi-way choices and calls to shared tails. It is what the Lua converter writes out, so every routine
// structures; the few that do not reduce to nested statements become a state machine (a loop that dispatches on a
// state variable), never a jump.
namespace dwe::formats::script {

// The test of an IF jump entry: the result of the IF's entries before `entry` (script_interp.c:
// scriptIfInstruction evaluates them all, left to right).
struct BranchCondition {
    std::size_t instruction = 0; // listing index of the IF
    std::size_t entry = 0;

    bool operator==(const BranchCondition&) const = default;
};

struct Statement;
using StatementList = std::vector<Statement>;

// Instructions that run in order: listing indices [first, first + count). An exit instruction (end_section,
// end_script, jump_return, jump_to_file, warp_to, teleport) is always the last statement of its path. For the
// emitter: end_section and end_script end the whole script even inside a jump_and_link subroutine (they unwind the
// whole call stack), so they cannot become a plain return from the subroutine's function.
struct RunStatement {
    std::size_t first = 0;
    std::size_t count = 0;
};

struct IfStatement {
    BranchCondition condition;
    StatementList whenTrue;
    StatementList whenFalse;
};

// Repeats its body; leaves by a BreakStatement or an exit instruction.
struct LoopStatement {
    StatementList body;
};

// Leaves the innermost loop.
struct BreakStatement {};

// A SWITCH or SET_SELECTION: arm i runs for case or option i. A SWITCH value past the last case runs the last arm; a
// SET_SELECTION has one more arm, for a cancel.
struct ChoiceStatement {
    std::size_t instruction = 0; // listing index
    std::vector<StatementList> arms;
};

// Runs the routine that starts at an instruction (another section, a subroutine or a shared tail) to its end;
// nothing follows it. For the emitter: it becomes `return f()`, a proper tail call, so the call stack does not grow.
struct TailCallStatement {
    std::size_t instruction = 0; // listing index of the routine's entry
};

// In a state machine: the state that runs next.
struct SetStateStatement {
    std::size_t state = 0;
};

struct MachineState {
    std::size_t state = 0;
    StatementList body; // ends with a SetStateStatement, a choice or branch between them, or an exit
};

// A loop that runs the state `initial` and then whichever state the last one set, until an exit instruction.
struct StateMachineStatement {
    std::size_t initial = 0;
    std::vector<MachineState> states;
};

struct Statement {
    std::variant<RunStatement, IfStatement, LoopStatement, BreakStatement, ChoiceStatement, TailCallStatement,
                 StateMachineStatement, SetStateStatement>
        node;
};

// Why a routine became a state machine.
enum class FallbackReason : std::uint8_t {
    Irreducible, // a loop with more than one way in
    NestedExit,  // a jump out of two loops at once, or back to the head of an outer loop
    Duplication, // copying the shared code would grow the routine past StructuringOptions::duplicationLimit
};

struct StructuredRoutine {
    RoutineKind kind = RoutineKind::Section;
    std::uint16_t section = 0;   // Section: its id
    std::size_t instruction = 0; // listing index of the entry
    StatementList body;
    std::optional<FallbackReason> fallback; // set when the body is a state machine
};

struct StructuredScript {
    // The sections in listing order, then the subroutines, then the shared tails in the order they were found.
    std::vector<StructuredRoutine> routines;
};

struct StructuringOptions {
    // Code that several paths share is copied into each when it is shorter than this, in instructions; otherwise
    // it becomes a shared tail (when it is outside every loop).
    std::size_t sharedTailMinimum = 3;
    // A routine may emit up to this many times its own size before it becomes a state machine. Each block costs its
    // instructions and at least 1, and so does each break or repeat of a loop.
    std::size_t duplicationLimit = 4;
};

// Structures every routine of a script. Fails only when the flow does not build (buildScriptFlow).
core::Result<StructuredScript> structureScript(const ScriptListing& listing, const StructuringOptions& options = {});

} // namespace dwe::formats::script
