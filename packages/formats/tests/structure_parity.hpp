#pragma once

#include <dwe/formats/script/control_flow.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/structuring.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Checks that a structured routine runs the same instructions as the graph it came from: both are
// walked with the same stream of branch outcomes and choices, and must give the same sequence of
// instructions. A structure that drops, adds or reorders a path gives a different sequence for some
// stream.
namespace structure_parity {

using dwe::formats::script::BasicBlock;
using dwe::formats::script::BlockId;
using dwe::formats::script::BreakStatement;
using dwe::formats::script::ChoiceStatement;
using dwe::formats::script::ControlFlowGraph;
using dwe::formats::script::FlowRole;
using dwe::formats::script::IfStatement;
using dwe::formats::script::LoopStatement;
using dwe::formats::script::RunStatement;
using dwe::formats::script::ScriptListing;
using dwe::formats::script::SetStateStatement;
using dwe::formats::script::StateMachineStatement;
using dwe::formats::script::Statement;
using dwe::formats::script::StatementList;
using dwe::formats::script::StructuredRoutine;
using dwe::formats::script::StructuredScript;
using dwe::formats::script::TailCallStatement;
using dwe::formats::script::TerminatorKind;

// A deterministic, finite stream of branch outcomes and choices. Both walks stop before the outcome
// past the end, so the same paths give the same instructions up to there.
class OutcomeStream {
public:
  OutcomeStream(std::uint32_t seed, std::size_t length)
      : state_((seed * 2654435761U) + 1U), length_(length) {}

  bool nextCondition() { return (next() & 1U) != 0; }
  std::size_t nextChoice(std::size_t count) { return next() % count; }
  [[nodiscard]] bool exhausted() const { return used_ >= length_; }

private:
  std::uint32_t next() {
    ++used_;
    state_ = (state_ * 1664525U) + 1013904223U;
    return state_ >> 8U;
  }

  std::uint32_t state_;
  std::size_t length_;
  std::size_t used_ = 0;
};

// How many streams of outcomes a parity check tries.
constexpr std::uint32_t ParitySeeds = 32;
// How many instructions a walk runs at most.
constexpr std::size_t TraceLimit = 400;
// The stream holds one outcome for this many instructions of the limit, so most walks end on the
// stream.
constexpr std::size_t InstructionsPerDecision = 4;
// How many blocks, loop rounds or tail calls a walk may take for each instruction of its limit.
constexpr std::size_t StepsPerInstruction = 16;

// What a walk did: an instruction ran, or a condition or choice was decided.
struct Event {
  enum class Kind : std::uint8_t { Instruction, Condition, Choice };
  Kind kind = Kind::Instruction;
  std::size_t instruction = 0; // listing index
  std::size_t entry = 0;       // Condition: the jump entry
  std::size_t outcome = 0;     // Condition: 1 if it held; Choice: the arm

  bool operator==(const Event&) const = default;
};

struct Trace {
  std::vector<Event> events;
  std::size_t instructions = 0;
  bool fellOffEnd = false; // the structure ran out of statements before an exit instruction

  void run(std::size_t first, std::size_t count) {
    for (std::size_t index = first; index < first + count; ++index) {
      events.push_back(
          {.kind = Event::Kind::Instruction, .instruction = index, .entry = 0, .outcome = 0});
    }
    instructions += count;
  }

  void decide(Event::Kind kind, std::size_t instruction, std::size_t entry, std::size_t outcome) {
    events.push_back(
        {.kind = kind, .instruction = instruction, .entry = entry, .outcome = outcome});
  }

  // Drops what came after the `limit`th instruction: blocks and runs stop at different places past
  // it.
  void cutAt(std::size_t limit) {
    std::size_t seen = 0;
    const auto last = std::ranges::find_if(events, [&](const Event& event) {
      return event.kind == Event::Kind::Instruction && ++seen == limit;
    });
    if (last != events.end()) {
      events.erase(last + 1, events.end());
    }
  }
};

inline bool isExit(const ScriptListing& listing, std::size_t instruction) {
  const FlowRole role = listing.instructions[instruction].role;
  return role == FlowRole::Return || role == FlowRole::FileJump || role == FlowRole::EndSection ||
         role == FlowRole::EndScript || role == FlowRole::Data || role == FlowRole::MapChange;
}

// Walks the graph from a block until an exit, the end of the stream, `limit` instructions or
// StepsPerInstruction times `limit` blocks (a loop with no instructions and no decisions).
inline Trace runGraph(const ControlFlowGraph& graph, BlockId entry, OutcomeStream& stream,
                      std::size_t limit) {
  Trace trace;
  BlockId block = entry;
  for (std::size_t step = 0; step < limit * StepsPerInstruction && trace.instructions < limit;
       ++step) {
    const BasicBlock& current = graph.blocks[block];
    trace.run(current.first, current.count);
    switch (current.terminator.kind) {
    case TerminatorKind::Exit:
      return trace;
    case TerminatorKind::FallThrough:
    case TerminatorKind::Jump:
      block = current.successors.front();
      break;
    case TerminatorKind::Branch:
      if (stream.exhausted()) {
        return trace;
      }
      {
        const bool holds = stream.nextCondition();
        trace.decide(Event::Kind::Condition, current.terminator.instruction,
                     current.terminator.entry, holds ? 1 : 0);
        block = holds == current.terminator.jumpsWhenTrue ? current.successors[0]
                                                          : current.successors[1];
        break;
      }
    case TerminatorKind::Choice:
      if (stream.exhausted()) {
        return trace;
      }
      {
        const std::size_t arm = stream.nextChoice(current.successors.size());
        trace.decide(Event::Kind::Choice, current.terminator.instruction, 0, arm);
        block = current.successors[arm];
        break;
      }
    }
  }
  return trace;
}

// Runs the statements of a structured script.
class StructureRunner {
public:
  StructureRunner(const ScriptListing& listing, const StructuredScript& script,
                  OutcomeStream& stream, std::size_t limit)
      : listing_(&listing), script_(&script), stream_(&stream), limit_(limit) {}

  Trace run(const StructuredRoutine& routine) {
    if (runList(routine.body) == Signal::Normal) {
      trace_.fellOffEnd = true;
    }
    return trace_;
  }

private:
  enum class Signal : std::uint8_t { Normal, Break, End };

  [[nodiscard]] bool exhausted() const {
    return trace_.instructions >= limit_ || steps_ >= limit_ * StepsPerInstruction;
  }

  Signal runList(const StatementList& list) {
    for (const Statement& statement : list) {
      const Signal signal =
          std::visit([this](const auto& node) { return this->runNode(node); }, statement.node);
      if (signal != Signal::Normal) {
        return signal;
      }
    }
    return Signal::Normal;
  }

  Signal runNode(const RunStatement& run) {
    trace_.run(run.first, run.count);
    const bool exits = run.count > 0 && isExit(*listing_, run.first + run.count - 1);
    return exits || exhausted() ? Signal::End : Signal::Normal;
  }

  Signal runNode(const IfStatement& statement) {
    if (stream_->exhausted()) {
      return Signal::End;
    }
    const bool holds = stream_->nextCondition();
    trace_.decide(Event::Kind::Condition, statement.condition.instruction,
                  statement.condition.entry, holds ? 1 : 0);
    return runList(holds ? statement.whenTrue : statement.whenFalse);
  }

  Signal runNode(const LoopStatement& statement) {
    while (!exhausted()) {
      ++steps_;
      const Signal signal = runList(statement.body);
      if (signal == Signal::Break) {
        return Signal::Normal;
      }
      if (signal == Signal::End) {
        return signal;
      }
    }
    return Signal::End;
  }

  static Signal runNode(const BreakStatement& /*statement*/) { return Signal::Break; }

  Signal runNode(const ChoiceStatement& statement) {
    if (stream_->exhausted()) {
      return Signal::End;
    }
    const std::size_t arm = stream_->nextChoice(statement.arms.size());
    trace_.decide(Event::Kind::Choice, statement.instruction, 0, arm);
    return runList(statement.arms[arm]);
  }

  Signal runNode(const TailCallStatement& statement) {
    ++steps_;
    if (exhausted()) {
      return Signal::End;
    }
    for (const StructuredRoutine& routine : script_->routines) {
      if (routine.instruction == statement.instruction) {
        if (runList(routine.body) == Signal::Normal) {
          trace_.fellOffEnd = true;
        }
        return Signal::End;
      }
    }
    trace_.fellOffEnd = true;
    return Signal::End;
  }

  Signal runNode(const StateMachineStatement& machine) {
    std::size_t state = machine.initial;
    while (!exhausted()) {
      ++steps_;
      nextState_ = NoState;
      const Signal signal = runList(machine.states[state].body);
      if (signal != Signal::Normal) {
        return signal;
      }
      if (nextState_ == NoState) {
        trace_.fellOffEnd = true;
        return Signal::End;
      }
      state = nextState_;
    }
    return Signal::End;
  }

  Signal runNode(const SetStateStatement& statement) {
    nextState_ = statement.state;
    return Signal::Normal;
  }

  static constexpr std::size_t NoState = static_cast<std::size_t>(-1);

  const ScriptListing* listing_;
  const StructuredScript* script_;
  OutcomeStream* stream_;
  std::size_t limit_;
  std::size_t steps_ = 0;
  std::size_t nextState_ = NoState;
  Trace trace_;
};

// An outline of a statement list, one statement per line, for failure messages.
class Outline {
public:
  static std::string of(const StatementList& list) {
    Outline outline;
    outline.add(list, 0);
    return outline.text_;
  }

private:
  void add(const StatementList& list, std::size_t depth) {
    for (const Statement& statement : list) {
      std::visit([this, depth](const auto& node) { this->line(node, depth); }, statement.node);
    }
  }

  void put(std::size_t depth, std::string_view text) {
    text_ += std::string(depth * 2, ' ');
    text_ += text;
    text_ += '\n';
  }

  void line(const RunStatement& run, std::size_t depth) {
    put(depth, std::format("run [{}, {})", run.first, run.first + run.count));
  }
  void line(const IfStatement& statement, std::size_t depth) {
    put(depth, std::format("if {}#{}", statement.condition.instruction, statement.condition.entry));
    add(statement.whenTrue, depth + 1);
    put(depth, "else");
    add(statement.whenFalse, depth + 1);
  }
  void line(const LoopStatement& statement, std::size_t depth) {
    put(depth, "loop");
    add(statement.body, depth + 1);
  }
  void line(const BreakStatement& /*statement*/, std::size_t depth) { put(depth, "break"); }
  void line(const ChoiceStatement& statement, std::size_t depth) {
    put(depth, std::format("choose {}", statement.instruction));
    for (const StatementList& arm : statement.arms) {
      put(depth, "arm");
      add(arm, depth + 1);
    }
  }
  void line(const TailCallStatement& statement, std::size_t depth) {
    put(depth, std::format("tail {}", statement.instruction));
  }
  void line(const StateMachineStatement& machine, std::size_t depth) {
    put(depth, std::format("machine from {}", machine.initial));
    for (const auto& state : machine.states) {
      put(depth, std::format("state {}", state.state));
      add(state.body, depth + 1);
    }
  }
  void line(const SetStateStatement& statement, std::size_t depth) {
    put(depth, std::format("state = {}", statement.state));
  }

  std::string text_;
};

// Whether every transfer in a structured script is structured: a break sits inside a loop, a state
// change names a state of the machine around it, and a tail call names a routine of the script.
// Nothing jumps anywhere else.
class TransferCheck {
public:
  static bool goesNowhereElse(const StructuredScript& script) {
    TransferCheck check{script};
    return std::ranges::all_of(script.routines, [&](const StructuredRoutine& routine) {
      return check.list(routine.body);
    });
  }

private:
  explicit TransferCheck(const StructuredScript& script) : script_(&script) {}

  bool list(const StatementList& statements) {
    return std::ranges::all_of(statements, [&](const Statement& statement) {
      return std::visit([this](const auto& node) { return this->check(node); }, statement.node);
    });
  }

  static bool check(const RunStatement& /*run*/) { return true; }
  bool check(const IfStatement& statement) {
    return list(statement.whenTrue) && list(statement.whenFalse);
  }
  bool check(const LoopStatement& statement) {
    ++loops_;
    const bool fine = list(statement.body);
    --loops_;
    return fine;
  }
  [[nodiscard]] bool check(const BreakStatement& /*statement*/) const { return loops_ > 0; }
  bool check(const ChoiceStatement& statement) {
    return std::ranges::all_of(statement.arms, [&](const StatementList& arm) { return list(arm); });
  }
  [[nodiscard]] bool check(const TailCallStatement& statement) const {
    return std::ranges::any_of(script_->routines, [&](const StructuredRoutine& routine) {
      return routine.instruction == statement.instruction;
    });
  }
  bool check(const StateMachineStatement& machine) {
    machines_.push_back(machine.states.size());
    const std::size_t outerLoops = std::exchange(loops_, 0);
    const bool fine =
        machine.initial < machine.states.size() &&
        std::ranges::all_of(machine.states, [&](const auto& state) { return list(state.body); });
    loops_ = outerLoops;
    machines_.pop_back();
    return fine;
  }
  [[nodiscard]] bool check(const SetStateStatement& statement) const {
    return !machines_.empty() && statement.state < machines_.back();
  }

  const StructuredScript* script_;
  std::size_t loops_ = 0;
  std::vector<std::size_t> machines_; // the state counts of the machines around the statement
};

// Whether a routine and its graph run the same instructions for `seeds` streams of outcomes.
inline bool runsLikeTheGraph(const ScriptListing& listing, const ControlFlowGraph& graph,
                             const StructuredScript& script, const StructuredRoutine& routine,
                             std::uint32_t seeds, std::size_t limit = TraceLimit) {
  const auto entry = graph.blockAt(routine.instruction);
  if (!entry) {
    return false;
  }
  for (std::uint32_t seed = 1; seed <= seeds; ++seed) {
    OutcomeStream graphStream{seed, limit / InstructionsPerDecision};
    OutcomeStream structureStream{seed, limit / InstructionsPerDecision};
    Trace expected = runGraph(graph, *entry, graphStream, limit);
    Trace actual = StructureRunner{listing, script, structureStream, limit}.run(routine);
    expected.cutAt(limit);
    actual.cutAt(limit);
    if (actual.fellOffEnd || expected.events != actual.events) {
      return false;
    }
  }
  return true;
}

} // namespace structure_parity
