#include <dwe/formats/script/guarded_walk.hpp>

#include <algorithm>
#include <map>
#include <string_view>
#include <utility>

#include "listing_vocabulary.hpp"

namespace dwe::formats::script {

namespace {

constexpr std::string_view SetTriggerOp = "set_trigger";
constexpr std::string_view UnsetTriggerOp = "unset_trigger";
constexpr std::string_view TriggerOperand = "trigger";

// What a path knows on its way.
struct PathState {
    PathGuard guard;
    std::map<std::uint16_t, bool> known; // trigger -> set
    bool othersClear = false;            // every trigger not in `known` is clear
    std::vector<TriggerEffect> effects;

    bool operator==(const PathState&) const = default;

    [[nodiscard]] std::optional<bool> trigger(std::uint16_t number) const {
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

const ScriptPlace* placeOf(const GuardTerm& term) {
    if (const auto* branch = std::get_if<BranchTerm>(&term.decision)) {
        return &branch->place;
    }
    return &std::get<ChoiceTerm>(term.decision).place;
}

// Which arm of the decision at `position` a path took: `holds` of an IF, the arm of a choice.
std::size_t armOf(const GuardTerm& term) {
    if (const auto* branch = std::get_if<BranchTerm>(&term.decision)) {
        return branch->holds ? 1 : 0;
    }
    return std::get<ChoiceTerm>(term.decision).arm;
}

// The paths after a decision at `place` with `arms` arms, whose term is at `position` of each path's guard: paths
// that are the same but for that term, and that took every arm, become one path without it.
PathStates joinArms(PathStates states, const ScriptPlace& place, std::size_t position, std::size_t arms) {
    std::vector<std::pair<PathState, std::vector<bool>>> groups;
    PathStates result;
    for (PathState& state : states) {
        const bool decided = state.guard.terms.size() > position && *placeOf(state.guard.terms[position]) == place;
        if (!decided) {
            appendUnique(result, std::move(state));
            continue;
        }
        const std::size_t arm = armOf(state.guard.terms[position]);
        PathState without = state;
        without.guard.terms.erase(without.guard.terms.begin() + static_cast<std::ptrdiff_t>(position));
        auto group = std::ranges::find(groups, without, &std::pair<PathState, std::vector<bool>>::first);
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
                if (state.guard.terms.size() <= position || !(*placeOf(state.guard.terms[position]) == place)) {
                    return false;
                }
                PathState stripped = state;
                stripped.guard.terms.erase(stripped.guard.terms.begin() + static_cast<std::ptrdiff_t>(position));
                return stripped == without;
            });
            appendUnique(result, std::move(without));
        }
    }
    return result;
}

void joinOutcome(Outcome& outcome, const ScriptPlace& place, std::size_t position, std::size_t arms) {
    outcome.next = joinArms(std::move(outcome.next), place, position, arms);
    outcome.returned = joinArms(std::move(outcome.returned), place, position, arms);
    outcome.broke = joinArms(std::move(outcome.broke), place, position, arms);
}

// The tests of an IF's jump entry: the entries before it that are not jumps.
std::vector<Condition> testsBefore(const ListingInstruction& instruction, std::size_t entry) {
    std::vector<Condition> tests;
    for (std::size_t index = 0; index < entry && index < instruction.conditions.size(); ++index) {
        const Condition& condition = instruction.conditions[index];
        if (condition.test != ConditionTest::GotoIfTrue && condition.test != ConditionTest::GotoIfFalse) {
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
        const auto index = entry != code->listing->sections.end() ? code->listing->indexAt(entry->entry) : std::nullopt;
        if (!index) {
            return {};
        }
        return walkRoutine({.script = script, .code = *code}, *index, std::move(states));
    }

    Outcome walkRoutine(const Frame& frame, std::size_t entry, PathStates states) {
        const auto& routines = frame.code.structured->routines;
        const auto routine = std::ranges::find(routines, entry, &StructuredRoutine::instruction);
        const ScriptPlace place{.script = frame.script, .instruction = entry};
        if (routine == routines.end() || std::ranges::contains(active_, place)) {
            return {};
        }
        active_.push_back(place);
        Outcome outcome = walkList(frame, routine->body, std::move(states));
        active_.pop_back();
        return outcome;
    }

    Outcome walkList(const Frame& frame, const StatementList& statements, PathStates states) {
        Outcome total;
        for (const Statement& statement : statements) {
            if (states.empty()) {
                break;
            }
            Outcome outcome =
                std::visit([this, &frame,
                            &states](const auto& node) { return this->walkStatement(frame, node, std::move(states)); },
                           statement.node);
            states = std::move(outcome.next);
            outcome.next.clear();
            total.absorb(std::move(outcome));
        }
        total.next = std::move(states);
        return total;
    }

    Outcome walkStatement(const Frame& frame, const RunStatement& run, PathStates states) {
        Outcome total;
        for (std::size_t index = run.first; index < run.first + run.count && !states.empty(); ++index) {
            Outcome step;
            for (PathState& state : states) {
                step.absorb(walkInstruction(frame, index, std::move(state)));
            }
            states = unique(std::move(step.next));
            step.next.clear();
            total.absorb(std::move(step));
        }
        total.next = std::move(states);
        return total;
    }

    Outcome walkStatement(const Frame& frame, const IfStatement& branch, PathStates states) {
        const ScriptPlace place{.script = frame.script, .instruction = branch.condition.instruction};
        const auto tests =
            testsBefore(frame.code.listing->instructions[branch.condition.instruction], branch.condition.entry);
        Outcome total;
        for (PathState& state : states) {
            if (const auto known = knownResult(tests, state)) {
                total.absorb(walkList(frame, *known ? branch.whenTrue : branch.whenFalse, {std::move(state)}));
                continue;
            }
            const std::size_t position = state.guard.terms.size();
            Outcome outcome;
            for (const bool holds : {true, false}) {
                PathState taken = state;
                taken.guard.terms.push_back({BranchTerm{.place = place, .tests = tests, .holds = holds}});
                outcome.absorb(walkList(frame, holds ? branch.whenTrue : branch.whenFalse, {std::move(taken)}));
            }
            joinOutcome(outcome, place, position, 2);
            total.absorb(bounded(std::move(outcome), state));
        }
        return total;
    }

    Outcome walkStatement(const Frame& frame, const ChoiceStatement& choice, const PathStates& states) {
        const ScriptPlace place{.script = frame.script, .instruction = choice.instruction};
        Outcome total;
        for (const PathState& state : states) {
            const std::size_t position = state.guard.terms.size();
            Outcome outcome;
            for (std::size_t arm = 0; arm < choice.arms.size(); ++arm) {
                PathState taken = state;
                taken.guard.terms.push_back({ChoiceTerm{.place = place, .arm = arm}});
                outcome.absorb(walkList(frame, choice.arms[arm], {std::move(taken)}));
            }
            joinOutcome(outcome, place, position, choice.arms.size());
            total.absorb(bounded(std::move(outcome), state));
        }
        return total;
    }

    // A loop's body is walked once, its paths unstructured. After it come the paths that broke out: with the
    // decisions they had before the loop, and unstructured if they took any inside it.
    Outcome walkStatement(const Frame& frame, const LoopStatement& loop, const PathStates& states) {
        Outcome total;
        for (const PathState& state : states) {
            PathState inside = state;
            inside.guard.unstructured = true;
            Outcome body = walkList(frame, loop.body, {std::move(inside)});
            std::ranges::move(body.returned, std::back_inserter(total.returned));
            for (PathState& left : body.broke) {
                const bool decidedInside = left.guard.terms.size() > state.guard.terms.size();
                left.guard.terms.resize(state.guard.terms.size());
                left.guard.unstructured = state.guard.unstructured || decidedInside;
                appendUnique(total.next, std::move(left));
            }
        }
        return total;
    }

    static Outcome walkStatement(const Frame& /*frame*/, const BreakStatement& /*leave*/, PathStates states) {
        return {.next = {}, .returned = {}, .broke = std::move(states)};
    }

    Outcome walkStatement(const Frame& frame, const TailCallStatement& call, PathStates states) {
        return walkRoutine(frame, call.instruction, std::move(states));
    }

    // Every state runs once, unstructured; a state's end chooses the next one, so no path leaves but by an exit.
    Outcome walkStatement(const Frame& frame, const StateMachineStatement& machine, PathStates states) {
        Outcome total;
        for (PathState& state : states) {
            state.guard.unstructured = true;
            for (const MachineState& machineState : machine.states) {
                Outcome outcome = walkList(frame, machineState.body, {state});
                std::ranges::move(outcome.returned, std::back_inserter(total.returned));
            }
        }
        return total;
    }

    static Outcome walkStatement(const Frame& /*frame*/, const SetStateStatement& /*next*/,
                                 const PathStates& /*states*/) {
        return {};
    }

    Outcome walkInstruction(const Frame& frame, std::size_t index, PathState state) {
        const ListingInstruction& instruction = frame.code.listing->instructions[index];
        if (filter_(instruction)) {
            report({.script = frame.script, .instruction = index}, state);
        }
        switch (instruction.role) {
        case FlowRole::Call: {
            const auto target =
                instruction.targets.empty() ? std::nullopt : frame.code.listing->indexAt(instruction.targets.front());
            if (!target) {
                return {};
            }
            Outcome called = walkRoutine(frame, *target, {std::move(state)});
            std::ranges::move(called.returned, std::back_inserter(called.next));
            return {.next = std::move(called.next), .returned = {}, .broke = {}};
        }
        case FlowRole::FileCall: {
            if (!instruction.reference) {
                return {};
            }
            Outcome called = walkSection(instruction.reference->script, instruction.reference->section, {state});
            std::ranges::move(called.returned, std::back_inserter(called.next));
            return {.next = std::move(called.next), .returned = {}, .broke = {}};
        }
        case FlowRole::FileJump: {
            if (!instruction.reference) {
                return {};
            }
            Outcome jumped = walkSection(instruction.reference->script, instruction.reference->section, {state});
            return {.next = {}, .returned = std::move(jumped.returned), .broke = {}};
        }
        case FlowRole::Return:
            return {.next = {}, .returned = {std::move(state)}, .broke = {}};
        case FlowRole::EndSection:
        case FlowRole::EndScript:
        case FlowRole::MapChange:
        case FlowRole::Data:
            return {};
        default:
            break;
        }
        if (instruction.op == SetTriggerOp || instruction.op == UnsetTriggerOp) {
            if (const auto number = instruction.argument(TriggerOperand)) {
                const auto trigger = static_cast<std::uint16_t>(*number);
                const bool set = instruction.op == SetTriggerOp;
                state.known[trigger] = set;
                state.effects.push_back({.trigger = trigger, .set = set});
            }
        }
        return {.next = {std::move(state)}, .returned = {}, .broke = {}};
    }

    // The outcome of a decision taken from `before`, or one unstructured path that knows nothing more if it split
    // into too many.
    [[nodiscard]] Outcome bounded(Outcome outcome, const PathState& before) const {
        const auto tooMany = [&](const PathStates& states) { return states.size() > options_.maximumPaths; };
        if (!tooMany(outcome.next) && !tooMany(outcome.returned) && !tooMany(outcome.broke)) {
            return outcome;
        }
        PathState widened = before;
        widened.guard.unstructured = true;
        widened.known.clear();
        widened.othersClear = false;
        const auto widen = [&](PathStates& states) {
            if (!states.empty()) {
                states = {widened};
            }
        };
        widen(outcome.next);
        widen(outcome.returned);
        widen(outcome.broke);
        return outcome;
    }

    void report(const ScriptPlace& place, const PathState& state) {
        GuardedInstruction found{.place = place, .guard = state.guard, .effects = state.effects, .state = {}};
        for (const auto& [trigger, set] : state.known) {
            found.state.push_back({.trigger = trigger, .set = set});
        }
        if (!std::ranges::contains(found_, found)) {
            found_.push_back(std::move(found));
        }
    }

    ScriptLookup lookup_;
    InstructionFilter filter_;
    WalkOptions options_;
    std::vector<ScriptPlace> active_; // the routines being walked, against recursion
    std::vector<GuardedInstruction> found_;
};

} // namespace

std::vector<GuardedInstruction> walkSection(std::uint16_t script, std::uint16_t section, const ScriptLookup& lookup,
                                            const InstructionFilter& filter, const WalkOptions& options) {
    return Walker{lookup, filter, options}.walk(script, section);
}

} // namespace dwe::formats::script
