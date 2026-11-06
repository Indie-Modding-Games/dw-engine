#include <dwe/formats/script/structure_statistics.hpp>

#include <algorithm>
#include <set>
#include <type_traits>
#include <variant>

namespace dwe::formats::script {

namespace {

class StatementCounter {
public:
  StatementCounter(StructureStatistics& statistics, std::set<std::size_t>& instructions)
      : statistics_(&statistics), instructions_(&instructions) {}

  void count(const StatementList& list, std::size_t depth) {
    statistics_->maxDepth = std::max(statistics_->maxDepth, depth);
    for (const Statement& statement : list) {
      std::visit([&](const auto& node) { countNode(node, depth); }, statement.node);
    }
  }

private:
  void countNode(const RunStatement& run, std::size_t /*depth*/) {
    statistics_->emittedInstructions += run.count;
    for (std::size_t instruction = run.first; instruction < run.first + run.count; ++instruction) {
      instructions_->insert(instruction);
    }
  }
  // An if that is all of an else arm reads as an elseif, at the depth of the first if.
  void countNode(const IfStatement& statement, std::size_t depth) {
    ++statistics_->ifs;
    count(statement.whenTrue, depth + 1);
    const bool elseIf = statement.whenFalse.size() == 1 &&
                        std::holds_alternative<IfStatement>(statement.whenFalse.front().node);
    if (elseIf) {
      countNode(std::get<IfStatement>(statement.whenFalse.front().node), depth);
    } else {
      count(statement.whenFalse, depth + 1);
    }
  }
  void countNode(const LoopStatement& statement, std::size_t depth) {
    ++statistics_->loops;
    count(statement.body, depth + 1);
  }
  void countNode(const BreakStatement& /*statement*/, std::size_t /*depth*/) {
    ++statistics_->breaks;
  }
  void countNode(const ChoiceStatement& statement, std::size_t depth) {
    ++statistics_->choices;
    for (const StatementList& arm : statement.arms) {
      count(arm, depth + 1);
    }
  }
  void countNode(const TailCallStatement& /*statement*/, std::size_t /*depth*/) {
    ++statistics_->tailCalls;
  }
  void countNode(const StateMachineStatement& statement, std::size_t depth) {
    for (const MachineState& state : statement.states) {
      count(state.body, depth + 1);
    }
  }
  void countNode(const SetStateStatement& /*statement*/, std::size_t /*depth*/) {}

  StructureStatistics* statistics_;
  std::set<std::size_t>* instructions_;
};

void countRoutine(const StructuredRoutine& routine, StructureStatistics& statistics) {
  switch (routine.kind) {
  case RoutineKind::Section:
    ++statistics.sections;
    break;
  case RoutineKind::Subroutine:
    ++statistics.subroutines;
    break;
  case RoutineKind::SharedTail:
    ++statistics.sharedTails;
    break;
  }
  if (!routine.fallback) {
    return;
  }
  ++statistics.stateMachines;
  switch (*routine.fallback) {
  case FallbackReason::Irreducible:
    ++statistics.irreducible;
    break;
  case FallbackReason::NestedExit:
    ++statistics.nestedExits;
    break;
  case FallbackReason::Duplication:
    ++statistics.duplications;
    break;
  }
}

} // namespace

StructureStatistics& StructureStatistics::operator+=(const StructureStatistics& other) {
  sections += other.sections;
  subroutines += other.subroutines;
  sharedTails += other.sharedTails;
  stateMachines += other.stateMachines;
  irreducible += other.irreducible;
  nestedExits += other.nestedExits;
  duplications += other.duplications;
  copiedRoutines += other.copiedRoutines;
  ifs += other.ifs;
  loops += other.loops;
  breaks += other.breaks;
  choices += other.choices;
  tailCalls += other.tailCalls;
  maxDepth = std::max(maxDepth, other.maxDepth);
  emittedInstructions += other.emittedInstructions;
  distinctInstructions += other.distinctInstructions;
  return *this;
}

StructureStatistics measureStructure(const StructuredScript& script) {
  StructureStatistics statistics;
  std::set<std::size_t> instructions;
  for (const StructuredRoutine& routine : script.routines) {
    countRoutine(routine, statistics);
    std::set<std::size_t> routineInstructions;
    const std::size_t emittedBefore = statistics.emittedInstructions;
    StatementCounter{statistics, routineInstructions}.count(routine.body, 0);
    statistics.copiedRoutines +=
        statistics.emittedInstructions - emittedBefore > routineInstructions.size() ? 1 : 0;
    instructions.insert(routineInstructions.begin(), routineInstructions.end());
  }
  statistics.distinctInstructions = instructions.size();
  return statistics;
}

} // namespace dwe::formats::script
