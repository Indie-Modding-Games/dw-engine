#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/script/script_listing.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// The control-flow graph of a script listing: basic blocks of instructions that run in order,
// joined by the jumps, IF gotos, SWITCH cases and SET_SELECTION options. Calls (jump_and_link,
// jump_to_file_and_link) are instructions inside a block: the code they run is a routine of its
// own.
namespace dwe::formats::script {

using BlockId = std::size_t;

// How a block ends.
enum class TerminatorKind : std::uint8_t {
  FallThrough, // into the next block, which starts at a label
  Jump,        // jump_to
  Branch,      // an IF jump entry: successors are {its target, the next block}
  Choice, // SWITCH or SET_SELECTION: successors are the cases or options, then the next instruction
  Exit,   // the last instruction of the run ends the flow: return, end, jump to another script, map
          // change, DATA
};

struct Terminator {
  TerminatorKind kind = TerminatorKind::Exit;
  std::size_t instruction =
      0; // the listing index of the jump, IF, SWITCH, SET_SELECTION or exit instruction
  std::size_t entry = 0;     // Branch: the jump entry's place in the IF's conditions
  bool jumpsWhenTrue = true; // Branch: goto_if_true, or goto_if_false
};

// A run of instructions that execute in order, then a terminator. An IF with several jump entries
// is a chain of blocks: the first holds the run, the others only a Branch.
struct BasicBlock {
  std::size_t first = 0; // listing index of the run's first instruction
  std::size_t count =
      0; // instructions in the run; an Exit block's last one is the exit instruction
  Terminator terminator;
  std::vector<BlockId> successors;
};

// Where a block starts: the listing index of its first instruction (the chained IF blocks have
// none).
struct BlockStart {
  std::size_t instruction = 0;
  BlockId block = 0;
};

struct ControlFlowGraph {
  std::vector<BasicBlock> blocks;
  std::vector<BlockStart> starts; // sorted by instruction

  // The block that starts at an instruction, if the flow reaches that instruction as the start of a
  // block.
  [[nodiscard]] std::optional<BlockId> blockAt(std::size_t instruction) const;
};

// What a routine is: a section the engine runs, a subroutine of jump_and_link, or a shared tail
// that the structuring pass makes a function of its own.
enum class RoutineKind : std::uint8_t { Section, Subroutine, SharedTail };

// An entry point and the code reachable from it.
struct Routine {
  RoutineKind kind = RoutineKind::Section;
  std::uint16_t section = 0; // Section: its id
  std::size_t instruction = 0;
  BlockId entry = 0;
};

struct ScriptFlow {
  ControlFlowGraph graph;
  std::vector<Routine>
      routines; // the sections in listing order, then each other jump_and_link target once
};

// Builds the blocks reachable from the sections and the jump_and_link targets. Fails if the flow
// runs past the last instruction.
core::Result<ScriptFlow> buildScriptFlow(const ScriptListing& listing);

} // namespace dwe::formats::script
