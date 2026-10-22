#include <dwe/formats/script/guarded_walk.hpp>
#include <dwe/formats/script/routine_writes.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <compare>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

#include "listing_vocabulary.hpp"

namespace dwe::formats::script {

namespace {

constexpr std::size_t DeadlineCheckSteps = 1024;

constexpr std::string_view SetTriggerOp = "set_trigger";
constexpr std::string_view UnsetTriggerOp = "unset_trigger";
constexpr std::string_view TriggerOperand = "trigger";

// The instructions besides set_trigger and unset_trigger that run game code which sets or clears
// triggers: the routines of the game (routine_writes.hpp), give_item, whose result is in trigger 0
// (script_interp.c: scriptInstruction28to3F), and compare_date, whose result is in its trigger
// (script_value.c: scriptCompareDate).
constexpr std::string_view RoutineOperand = "routine";
constexpr std::string_view GiveItemOp = "give_item";
constexpr std::uint16_t GiveItemResultTrigger = 0; // clear when the item was given
constexpr std::string_view CompareDateOp = "compare_date";

// Triggers the game writes between frames, while a section runs (main.c: gameLoop runs
// tournament.c: updateTournamentRegistration every frame; script_interp.c: scriptPauseGame clears
// 44): a path never knows them. Trigger 40 is not among those: only dget.c: initTournamentSchedule
// writes it, while routine 0x12 runs (script_interp.c: tickScript). Keeping it here is
// conservative: no path knows it, so both arms of a test of it are walked.
constexpr std::array<std::uint16_t, 5> TriggersOfTheFrame = {37, 38, 39, 40, 44};

// What a path knows on its way.
struct PathState {
  PathGuard guard;
  // trigger -> set, or nothing for a trigger the path does not know even with `othersClear`
  std::map<std::uint16_t, std::optional<bool>> known;
  bool othersClear = false; // every trigger not in `known` is clear
  std::vector<TriggerEffect> effects;

  bool operator==(const PathState&) const = default;

  [[nodiscard]] std::optional<bool> trigger(std::uint16_t number) const {
    if (std::ranges::contains(TriggersOfTheFrame, number)) {
      return std::nullopt;
    }
    if (const auto found = known.find(number); found != known.end()) {
      return found->second;
    }
    return othersClear ? std::optional{false} : std::nullopt;
  }
};

using PathStates = std::vector<PathState>;

// Where the paths go when a statement ends.
struct Outcome {
  PathStates next;     // on to the statement after it
  PathStates returned; // left the subroutine by jump_return
  PathStates broke;    // left the innermost loop

  void absorb(Outcome other) {
    std::ranges::move(other.next, std::back_inserter(next));
    std::ranges::move(other.returned, std::back_inserter(returned));
    std::ranges::move(other.broke, std::back_inserter(broke));
  }
};

void appendUnique(PathStates& states, PathState state) {
  if (!std::ranges::contains(states, state)) {
    states.push_back(std::move(state));
  }
}

PathStates unique(PathStates states) {
  PathStates result;
  for (PathState& state : states) {
    appendUnique(result, std::move(state));
  }
  return result;
}

// The triggers an instruction makes the path forget: nothing when it writes none the path could
// know (set_trigger and unset_trigger write what the path then knows), an empty list when it may
// write any.
std::optional<std::vector<std::uint16_t>> triggersWrittenBy(const ListingInstruction& instruction) {
  std::vector<std::uint16_t> written;
  if (instruction.op == CallRoutineOp || instruction.op == CallDigimonRoutineOp) {
    const auto routine = instruction.argument(RoutineOperand);
    const RoutineWrites* writes =
        routine ? routineWrites(instruction.op, static_cast<std::uint8_t>(*routine)) : nullptr;
    if (writes == nullptr) {
      return std::nullopt;
    }
    if (writes->set == WriteSet::Any) {
      return written;
    }
    written = writes->triggers;
  } else if (instruction.op == GiveItemOp) {
    written = {GiveItemResultTrigger};
  } else if (instruction.op == CompareDateOp) {
    const auto number = instruction.argument(TriggerOperand);
    if (!number) {
      return written;
    }
    written = {static_cast<std::uint16_t>(*number)};
  } else {
    return std::nullopt;
  }
  std::erase_if(written, [](std::uint16_t trigger) {
    return std::ranges::contains(TriggersOfTheFrame, trigger);
  });
  if (written.empty()) {
    return std::nullopt;
  }
  return written;
}

// The place of the decision a term is about; nothing for a recursion or a forget, which are no
// decision of a statement.
const ScriptPlace* placeOf(const GuardTerm& term) {
  if (const auto* branch = std::get_if<BranchTerm>(&term.decision)) {
    return &branch->place;
  }
  if (const auto* choice = std::get_if<ChoiceTerm>(&term.decision)) {
    return &choice->place;
  }
  return nullptr;
}

// Whether the term at `position` of a path's guard is about the decision at `place`.
bool decidedAt(const PathState& state, std::size_t position, const ScriptPlace& place) {
  if (state.guard.terms.size() <= position) {
    return false;
  }
  const ScriptPlace* at = placeOf(state.guard.terms[position]);
  return at != nullptr && *at == place;
}

// Which arm of the decision at `position` a path took: `holds` of an IF, the arm of a choice.
std::size_t armOf(const GuardTerm& term) {
  if (const auto* branch = std::get_if<BranchTerm>(&term.decision)) {
    return branch->holds ? 1 : 0;
  }
  if (const auto* choice = std::get_if<ChoiceTerm>(&term.decision)) {
    return choice->arm;
  }
  return 0;
}

// The paths after a decision at `place` with `arms` arms, whose term is at `position` of the paths'
// guards: paths that are the same but for that term, and that took every arm, become one path
// without it.
PathStates joinArms(PathStates states, const ScriptPlace& place, std::size_t position,
                    std::size_t arms) {
  std::vector<std::pair<PathState, std::vector<bool>>> groups;
  PathStates result;
  for (PathState& state : states) {
    if (!decidedAt(state, position, place)) {
      appendUnique(result, std::move(state));
      continue;
    }
    const std::size_t arm = armOf(state.guard.terms[position]);
    PathState without = state;
    without.guard.terms.erase(without.guard.terms.begin() + static_cast<std::ptrdiff_t>(position));
    auto group =
        std::ranges::find(groups, without, &std::pair<PathState, std::vector<bool>>::first);
    if (group == groups.end()) {
      groups.emplace_back(std::move(without), std::vector<bool>(arms, false));
      group = groups.end() - 1;
    }
    if (arm < arms) {
      group->second[arm] = true;
    }
    appendUnique(result, std::move(state));
  }
  for (auto& [without, taken] : groups) {
    if (std::ranges::all_of(taken, std::identity{})) {
      std::erase_if(result, [&](const PathState& state) {
        if (!decidedAt(state, position, place)) {
          return false;
        }
        PathState stripped = state;
        stripped.guard.terms.erase(stripped.guard.terms.begin() +
                                   static_cast<std::ptrdiff_t>(position));
        return stripped == without;
      });
      appendUnique(result, std::move(without));
    }
  }
  return result;
}

// The path `caller` followed by `inside`, a path walked from what `caller` knows but with no
// decisions and no effects: the decisions and effects of both, and what `inside` knows at its end.
PathState composed(const PathState& caller, const PathState& inside) {
  PathState result = caller;
  std::ranges::copy(inside.guard.terms, std::back_inserter(result.guard.terms));
  std::ranges::copy(inside.effects, std::back_inserter(result.effects));
  result.known = inside.known;
  result.othersClear = inside.othersClear;
  result.guard.unstructured = caller.guard.unstructured || inside.guard.unstructured;
  return result;
}

// Joins the arms of a decision taken by paths whose guards had the given lengths before it.
void joinOutcome(Outcome& outcome, const ScriptPlace& place, const std::set<std::size_t>& positions,
                 std::size_t arms) {
  for (const std::size_t position : positions) {
    outcome.next = joinArms(std::move(outcome.next), place, position, arms);
    outcome.returned = joinArms(std::move(outcome.returned), place, position, arms);
    outcome.broke = joinArms(std::move(outcome.broke), place, position, arms);
  }
}

// One path for many: unstructured, with the decisions and the effects all of them begin with and
// the triggers they all know alike.
PathState merged(const PathStates& states) {
  PathState result = states.front();
  std::set<std::uint16_t> named;
  for (const PathState& state : states) {
    for (const auto& entry : state.known) {
      named.insert(entry.first);
    }
  }
  for (const PathState& state : states) {
    result.guard.terms.erase(std::ranges::mismatch(result.guard.terms, state.guard.terms).in1,
                             result.guard.terms.end());
    result.effects.erase(std::ranges::mismatch(result.effects, state.effects).in1,
                         result.effects.end());
  }
  std::map<std::uint16_t, std::optional<bool>> known;
  for (const std::uint16_t trigger : named) {
    const std::optional<bool> value = result.trigger(trigger);
    const bool agreed = std::ranges::all_of(
        states, [&](const PathState& state) { return state.trigger(trigger) == value; });
    known[trigger] = agreed ? value : std::nullopt;
  }
  result.known = std::move(known);
  result.othersClear = std::ranges::all_of(states, &PathState::othersClear);
  result.guard.unstructured = true;
  return result;
}

// The tests of an IF's jump entry: the entries before it that are not jumps.
std::vector<Condition> testsBefore(const ListingInstruction& instruction, std::size_t entry) {
  std::vector<Condition> tests;
  for (std::size_t index = 0; index < entry && index < instruction.conditions.size(); ++index) {
    const Condition& condition = instruction.conditions[index];
    if (condition.test != ConditionTest::GotoIfTrue &&
        condition.test != ConditionTest::GotoIfFalse) {
      tests.push_back(condition);
    }
  }
  return tests;
}

std::optional<std::int32_t> argumentOf(const Condition& condition, std::string_view name) {
  const auto found = std::ranges::find(condition.arguments, name, &Argument::name);
  return found == condition.arguments.end() ? std::nullopt : std::optional{found->value};
}

// The result of tests that only read triggers the path knows; nothing if one reads anything else.
std::optional<bool> knownResult(const std::vector<Condition>& tests, const PathState& state) {
  std::optional<bool> result;
  for (const Condition& test : tests) {
    const auto number = argumentOf(test, TriggerOperand);
    if (test.test != ConditionTest::Trigger || !number) {
      return std::nullopt;
    }
    const auto set = state.trigger(static_cast<std::uint16_t>(*number));
    if (!set) {
      return std::nullopt;
    }
    const bool testsClear = (test.code & vocabulary::LowBits) != 0;
    const bool value = *set != testsClear;
    switch (test.combine) {
    case Combine::Replace:
      result = value;
      break;
    case Combine::And:
      result = result.value_or(false) && value;
      break;
    case Combine::Or:
      result = result.value_or(false) || value;
      break;
    }
  }
  return result;
}

class Walker {
  // An instruction the filter took on a summarized routine's walk, and the path there.
  struct Reported {
    ScriptPlace place;
    PathState state;

    bool operator==(const Reported&) const = default;
  };

  using RoutineId =
      std::pair<std::uint16_t, std::size_t>; // a routine's script and first instruction

  struct Summary {
    Outcome outcome;
    std::vector<Reported> reported;
    std::set<RoutineId>
        entered; // the routines its walk tried to enter, with the kept walks it used
    std::set<RoutineId> enteredBeingWalked; // which of them were being walked when it was walked
  };

  // A routine and what the path into it knows.
  struct SummaryKey {
    std::uint16_t script = 0;
    std::size_t instruction = 0;
    std::map<std::uint16_t, std::optional<bool>> known;
    bool othersClear = false;

    auto operator<=>(const SummaryKey&) const = default;
  };

public:
  Walker(ScriptLookup lookup, InstructionFilter filter, WalkOptions options)
      : lookup_(std::move(lookup)), filter_(std::move(filter)), options_(std::move(options)) {}

  std::vector<GuardedInstruction> walk(std::uint16_t script, std::uint16_t section) {
    PathState start;
    for (const TriggerEffect& trigger : options_.knownTriggers) {
      start.known[trigger.trigger] = trigger.set;
    }
    start.othersClear = options_.othersClear;
    walkSection(script, section, {std::move(start)});
    return std::move(found_);
  }

private:
  struct Frame {
    std::uint16_t script = 0;
    WalkableScript code;
  };

  Outcome walkSection(std::uint16_t script, std::uint16_t section, PathStates states) {
    const auto code = lookup_(script);
    if (!code || code->listing == nullptr || code->structured == nullptr) {
      return {};
    }
    const auto entry = std::ranges::find(code->listing->sections, section, &ListingSection::id);
    const auto index = entry != code->listing->sections.end() ? code->listing->indexAt(entry->entry)
                                                              : std::nullopt;
    if (!index) {
      return {};
    }
    return walkRoutine({.script = script, .code = *code}, *index, std::move(states));
  }

  Outcome walkRoutine(const Frame& frame, std::size_t entry, PathStates states) {
    const auto& routines = frame.code.structured->routines;
    const auto routine = std::ranges::find(routines, entry, &StructuredRoutine::instruction);
    const ScriptPlace place{.script = frame.script, .instruction = entry};
    if (routine == routines.end() || states.empty()) {
      return {};
    }
    if (entered_ != nullptr) {
      entered_->insert({place.script, place.instruction});
    }
    if (std::ranges::contains(active_, place)) {
      return {.next = {}, .returned = cutShort(place, std::move(states)), .broke = {}};
    }
    if (++walks_[{place.script, place.instruction}] > options_.maximumRoutineWalks) {
      count(&WalkMonitor::summarizedCalls);
      return summarized(frame, *routine, place, states);
    }
    active_.push_back(place);
    Outcome outcome = walkList(frame, routine->body, std::move(states));
    active_.pop_back();
    return outcome;
  }

  Outcome walkList(const Frame& frame, const StatementList& statements, PathStates states) {
    Outcome total;
    for (const Statement& statement : statements) {
      if (states.empty() || stopped()) {
        break;
      }
      Outcome outcome = std::visit(
          [this, &frame, &states](const auto& node) {
            return this->walkStatement(frame, node, std::move(states));
          },
          statement.node);
      states = bounded(std::move(outcome.next));
      outcome.next.clear();
      total.absorb(std::move(outcome));
    }
    total.next = std::move(states);
    total.returned = bounded(unique(std::move(total.returned)));
    total.broke = bounded(unique(std::move(total.broke)));
    return total;
  }

  Outcome walkStatement(const Frame& frame, const RunStatement& run, PathStates states) {
    Outcome total;
    for (std::size_t index = run.first; index < run.first + run.count && !states.empty(); ++index) {
      Outcome step = walkInstruction(frame, index, std::move(states));
      states = bounded(unique(std::move(step.next)));
      step.next.clear();
      total.absorb(std::move(step));
    }
    total.next = std::move(states);
    return total;
  }

  // The paths whose tests give an arm go down it; the others go down both, each with the term of
  // the arm.
  Outcome walkStatement(const Frame& frame, const IfStatement& branch, PathStates states) {
    const ScriptPlace place{.script = frame.script, .instruction = branch.condition.instruction};
    const auto tests = testsBefore(frame.code.listing->instructions[branch.condition.instruction],
                                   branch.condition.entry);
    PathStates whenTrue;
    PathStates whenFalse;
    std::set<std::size_t> positions;
    for (PathState& state : states) {
      if (const auto known = knownResult(tests, state)) {
        (*known ? whenTrue : whenFalse).push_back(std::move(state));
        continue;
      }
      positions.insert(state.guard.terms.size());
      PathState otherwise = state;
      state.guard.terms.push_back({BranchTerm{.place = place, .tests = tests, .holds = true}});
      otherwise.guard.terms.push_back({BranchTerm{.place = place, .tests = tests, .holds = false}});
      whenTrue.push_back(std::move(state));
      whenFalse.push_back(std::move(otherwise));
    }
    Outcome outcome = walkList(frame, branch.whenTrue, std::move(whenTrue));
    outcome.absorb(walkList(frame, branch.whenFalse, std::move(whenFalse)));
    joinOutcome(outcome, place, positions, 2);
    return outcome;
  }

  Outcome walkStatement(const Frame& frame, const ChoiceStatement& choice,
                        const PathStates& states) {
    const ScriptPlace place{.script = frame.script, .instruction = choice.instruction};
    const bool selection =
        frame.code.listing->instructions[choice.instruction].role == FlowRole::Selection;
    std::set<std::size_t> positions;
    for (const PathState& state : states) {
      positions.insert(state.guard.terms.size());
    }
    Outcome outcome;
    for (std::size_t arm = 0; arm < choice.arms.size(); ++arm) {
      PathStates taken = states;
      for (PathState& state : taken) {
        state.guard.terms.push_back(
            {ChoiceTerm{.place = place, .arm = arm, .selection = selection}});
      }
      outcome.absorb(walkList(frame, choice.arms[arm], std::move(taken)));
    }
    joinOutcome(outcome, place, positions, choice.arms.size());
    return outcome;
  }

  // A loop's body is walked once, its paths unstructured. After it come the paths that broke out:
  // with the decisions they had before the loop, and unstructured if they were before it or took a
  // decision inside it. Paths with as many decisions before the loop, and alike unstructured, are
  // walked together.
  Outcome walkStatement(const Frame& frame, const LoopStatement& loop, const PathStates& states) {
    std::map<std::pair<std::size_t, bool>, PathStates> byDecisions;
    for (const PathState& state : states) {
      PathState inside = state;
      inside.guard.unstructured = true;
      byDecisions[{state.guard.terms.size(), state.guard.unstructured}].push_back(
          std::move(inside));
    }
    Outcome total;
    for (auto& [group, inside] : byDecisions) {
      const auto [decisions, wasUnstructured] = group;
      Outcome body = walkList(frame, loop.body, std::move(inside));
      std::ranges::move(body.returned, std::back_inserter(total.returned));
      for (PathState& left : body.broke) {
        const bool decidedInside = left.guard.terms.size() > decisions;
        left.guard.terms.resize(decisions);
        left.guard.unstructured = wasUnstructured || decidedInside;
        appendUnique(total.next, std::move(left));
      }
    }
    return total;
  }

  static Outcome walkStatement(const Frame& /*frame*/, const BreakStatement& /*leave*/,
                               PathStates states) {
    return {.next = {}, .returned = {}, .broke = std::move(states)};
  }

  Outcome walkStatement(const Frame& frame, const TailCallStatement& call, PathStates states) {
    return walkRoutine(frame, call.instruction, std::move(states));
  }

  // Every state runs once, unstructured; a state's end chooses the next one, so no path leaves but
  // by an exit.
  Outcome walkStatement(const Frame& frame, const StateMachineStatement& machine,
                        PathStates states) {
    for (PathState& state : states) {
      state.guard.unstructured = true;
    }
    Outcome total;
    for (const MachineState& machineState : machine.states) {
      Outcome outcome = walkList(frame, machineState.body, states);
      std::ranges::move(outcome.returned, std::back_inserter(total.returned));
    }
    return total;
  }

  static Outcome walkStatement(const Frame& /*frame*/, const SetStateStatement& /*next*/,
                               const PathStates& /*states*/) {
    return {};
  }

  // Runs one instruction on every path: a call walks its routine once for all of them.
  Outcome walkInstruction(const Frame& frame, std::size_t index, PathStates states) {
    const ListingInstruction& instruction = frame.code.listing->instructions[index];
    if (filter_(instruction)) {
      for (const PathState& state : states) {
        report({.script = frame.script, .instruction = index}, state);
      }
    }
    switch (instruction.role) {
    case FlowRole::Call: {
      const auto target = instruction.targets.empty()
                              ? std::nullopt
                              : frame.code.listing->indexAt(instruction.targets.front());
      if (!target) {
        return {};
      }
      Outcome called = walkRoutine(frame, *target, std::move(states));
      std::ranges::move(called.returned, std::back_inserter(called.next));
      return {.next = std::move(called.next), .returned = {}, .broke = {}};
    }
    case FlowRole::FileCall: {
      if (!instruction.reference) {
        return {};
      }
      Outcome called = walkSection(instruction.reference->script, instruction.reference->section,
                                   std::move(states));
      std::ranges::move(called.returned, std::back_inserter(called.next));
      return {.next = std::move(called.next), .returned = {}, .broke = {}};
    }
    case FlowRole::FileJump: {
      if (!instruction.reference) {
        return {};
      }
      Outcome jumped = walkSection(instruction.reference->script, instruction.reference->section,
                                   std::move(states));
      return {.next = {}, .returned = std::move(jumped.returned), .broke = {}};
    }
    case FlowRole::Return:
      return {.next = {}, .returned = std::move(states), .broke = {}};
    case FlowRole::EndSection:
    case FlowRole::EndScript:
    case FlowRole::MapChange:
    case FlowRole::Data:
      return {};
    default:
      break;
    }
    if (const auto written = triggersWrittenBy(instruction)) {
      const ScriptPlace place{.script = frame.script, .instruction = index};
      return {.next = forgotten(place, *written, std::move(states)), .returned = {}, .broke = {}};
    }
    if (instruction.op == SetTriggerOp || instruction.op == UnsetTriggerOp) {
      if (const auto number = instruction.argument(TriggerOperand)) {
        const auto trigger = static_cast<std::uint16_t>(*number);
        const bool set = instruction.op == SetTriggerOp;
        for (PathState& state : states) {
          state.known[trigger] = set;
          state.effects.push_back({.trigger = trigger, .set = set});
        }
      }
    }
    return {.next = std::move(states), .returned = {}, .broke = {}};
  }

  // A routine walked too often. Each path goes through the walk of the routine from what it knows,
  // which is done once for all the paths that know the same and put after each of them: what a full
  // walk of that path gives, since the walk of a routine depends on its path only by what the path
  // knows and by which routines are being walked, which the recursion check leaves out. So a walk
  // is kept with the routines it tried to enter, and used again only where the same ones of them
  // are being walked: a routine that calls itself, as a loop does, is cut the same way on every
  // use.
  Outcome summarized(const Frame& frame, const StructuredRoutine& routine, const ScriptPlace& place,
                     const PathStates& states) {
    Outcome total;
    for (const PathState& caller : states) {
      const SummaryKey key{.script = place.script,
                           .instruction = place.instruction,
                           .known = caller.known,
                           .othersClear = caller.othersClear};
      const Summary& used = summaryFor(frame, routine, place, key);
      if (entered_ != nullptr) {
        entered_->insert(used.entered.begin(), used.entered.end());
      }
      for (const Reported& inside : used.reported) {
        report(inside.place, composed(caller, inside.state));
      }
      const auto after = [&caller](const PathStates& inside, PathStates& into) {
        for (const PathState& state : inside) {
          appendUnique(into, composed(caller, state));
        }
      };
      after(used.outcome.next, total.next);
      after(used.outcome.returned, total.returned);
      after(used.outcome.broke, total.broke);
    }
    return total;
  }

  // The kept walk of a routine from what a path knows, for the routines being walked now; walked if
  // there is none.
  const Summary& summaryFor(const Frame& frame, const StructuredRoutine& routine,
                            const ScriptPlace& place, const SummaryKey& key) {
    const std::vector<Summary>& kept = summaries_[key];
    const auto fits = std::ranges::find_if(kept, [this](const Summary& summary) {
      return beingWalked(summary.entered) == summary.enteredBeingWalked;
    });
    if (fits != kept.end()) {
      return *fits;
    }
    PathState start;
    start.known = key.known;
    start.othersClear = key.othersClear;
    std::vector<Reported> reported;
    std::set<RoutineId> entered;
    std::vector<Reported>* outerCapture = std::exchange(capture_, &reported);
    std::set<RoutineId>* outerEntered = std::exchange(entered_, &entered);
    count(&WalkMonitor::summaryWalks);
    active_.push_back(place);
    Outcome outcome = walkList(frame, routine.body, {std::move(start)});
    active_.pop_back();
    capture_ = outerCapture;
    entered_ = outerEntered;
    std::set<RoutineId> enteredBeingWalked = beingWalked(entered);
    // The walk may have kept other routines' walks, so the vector is looked up again.
    std::vector<Summary>& into = summaries_[key];
    into.push_back({.outcome = std::move(outcome),
                    .reported = std::move(reported),
                    .entered = std::move(entered),
                    .enteredBeingWalked = std::move(enteredBeingWalked)});
    return into.back();
  }

  // Which of these routines are being walked.
  [[nodiscard]] std::set<RoutineId> beingWalked(const std::set<RoutineId>& routines) const {
    std::set<RoutineId> walked;
    for (const ScriptPlace& place : active_) {
      if (routines.contains({place.script, place.instruction})) {
        walked.insert({place.script, place.instruction});
      }
    }
    return walked;
  }

  // The paths that call a routine the walk is in: they go on as if it returned, with a term that
  // says so and knowing no trigger, since the recursion may set any. Dropping them would leave the
  // decision before the call with one arm, a decision the original does not take.
  [[nodiscard]] PathStates cutShort(const ScriptPlace& routine, PathStates states) const {
    count(&WalkMonitor::recursionCuts);
    for (PathState& state : states) {
      state.guard.terms.push_back({RecursionTerm{.routine = routine}});
      state.known.clear();
      state.othersClear = false;
    }
    return unique(std::move(states));
  }

  // The paths after an instruction that runs game code which may write `triggers` (any, if empty):
  // they go on with a term that says so and without knowing them.
  static PathStates forgotten(const ScriptPlace& place, const std::vector<std::uint16_t>& triggers,
                              PathStates states) {
    for (PathState& state : states) {
      state.guard.terms.push_back({ForgetTerm{.place = place, .triggers = triggers}});
      if (triggers.empty()) {
        state.known.clear();
        state.othersClear = false;
        continue;
      }
      for (const std::uint16_t trigger : triggers) {
        state.known[trigger] = std::nullopt;
      }
    }
    return unique(std::move(states));
  }

  // The paths, or one path for them all if there are too many.
  [[nodiscard]] PathStates bounded(PathStates states) const {
    if (states.size() <= options_.maximumPaths) {
      return states;
    }
    count(&WalkMonitor::merges);
    return {merged(states)};
  }

  void report(const ScriptPlace& place, const PathState& state) {
    if (capture_ != nullptr) {
      Reported reported{.place = place, .state = state};
      if (!std::ranges::contains(*capture_, reported)) {
        capture_->push_back(std::move(reported));
      }
      return;
    }
    GuardedInstruction found{
        .place = place, .guard = state.guard, .effects = state.effects, .state = {}};
    for (const auto& [trigger, set] : state.known) {
      if (set) {
        found.state.push_back({.trigger = trigger, .set = *set});
      }
    }
    if (!std::ranges::contains(found_, found)) {
      found_.push_back(std::move(found));
    }
  }

  ScriptLookup lookup_;
  InstructionFilter filter_;
  WalkOptions options_;
  void count(std::size_t WalkMonitor::* counter) const {
    if (options_.monitor != nullptr) {
      ++(options_.monitor->*counter);
    }
  }

  // Whether the monitor's deadline has passed; the clock is read once every DeadlineCheckSteps
  // calls.
  bool stopped() {
    WalkMonitor* monitor = options_.monitor;
    if (monitor == nullptr || !monitor->deadline) {
      return false;
    }
    if (!monitor->stopped && ++steps_ % DeadlineCheckSteps == 0) {
      monitor->stopped = std::chrono::steady_clock::now() > *monitor->deadline;
    }
    return monitor->stopped;
  }

  std::vector<ScriptPlace> active_;        // the routines being walked, against recursion
  std::size_t steps_ = 0;                  // statement lists walked, for the deadline
  std::map<RoutineId, std::size_t> walks_; // routine -> times walked
  std::map<SummaryKey, std::vector<Summary>> summaries_;
  std::set<RoutineId>* entered_ =
      nullptr; // where a summarized routine's walk notes the routines it enters
  std::vector<Reported>* capture_ = nullptr; // where a summarized routine's walk reports
  std::vector<GuardedInstruction> found_;
};

} // namespace

std::vector<GuardedInstruction> walkSection(std::uint16_t script, std::uint16_t section,
                                            const ScriptLookup& lookup,
                                            const InstructionFilter& filter,
                                            const WalkOptions& options) {
  return Walker{lookup, filter, options}.walk(script, section);
}

} // namespace dwe::formats::script
