#include <dwe/formats/script/structuring.hpp>

#include <algorithm>
#include <expected>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <utility>

#include "routine_graph.hpp"

namespace dwe::formats::script {

namespace {

using Statements = std::expected<StatementList, FallbackReason>;

// How the walk comes to a node.
enum class Arrival : std::uint8_t {
    Jump,      // along an edge of the graph
    Join,      // after a statement it just closed, where that statement's paths meet
    LoopEntry, // at the head of a loop it is opening
};

// The entry instructions of the script's routines, and the shared tails that a structured routine asks for.
class RoutineEntries {
public:
    explicit RoutineEntries(const std::vector<Routine>& routines) {
        for (const Routine& routine : routines) {
            entries_.emplace(routine.instruction, routine.entry);
        }
    }

    [[nodiscard]] bool contains(std::size_t instruction) const { return entries_.contains(instruction); }

    // The blocks the entries start.
    [[nodiscard]] std::set<BlockId> blocks() const {
        std::set<BlockId> result;
        for (const auto& [instruction, block] : entries_) {
            result.insert(block);
        }
        return result;
    }

    // Adds new shared tails; returns those that were not routines yet, in order.
    std::vector<BlockStart> add(const std::vector<BlockStart>& tails) {
        std::vector<BlockStart> added;
        for (const BlockStart& tail : tails) {
            if (entries_.emplace(tail.instruction, tail.block).second) {
                added.push_back(tail);
            }
        }
        return added;
    }

private:
    std::map<std::size_t, BlockId> entries_; // entry instruction to the block it starts
};

// Whether the code reachable from a block holds at least `count` instructions.
bool reachesAtLeast(const ControlFlowGraph& graph, BlockId start, std::size_t count) {
    std::vector<bool> seen(graph.blocks.size(), false);
    std::vector<BlockId> pending{start};
    std::size_t instructions = 0;
    while (!pending.empty() && instructions < count) {
        const BlockId block = pending.back();
        pending.pop_back();
        if (!seen[block]) {
            seen[block] = true;
            instructions += graph.blocks[block].count;
            std::ranges::copy(graph.blocks[block].successors, std::back_inserter(pending));
        }
    }
    return instructions >= count;
}

// Turns one routine's graph into nested statements, walking it from the entry: a branch becomes an if/else whose
// arms meet again at the branch's post-dominator, a natural loop becomes a loop whose exits break to its follow,
// another routine's entry becomes a tail call, and code that several paths reach outside every loop becomes a shared
// tail when it is long enough (a copy otherwise). What this cannot express (an irreducible loop, a jump out of two
// loops at once, too many copies) fails, and the routine becomes a state machine instead.
class RoutineStructurer {
public:
    RoutineStructurer(const ControlFlowGraph& graph, const Routine& routine, const RoutineEntries& entries,
                      const StructuringOptions& options)
        : graph_(&graph), entries_(&entries), options_(&options), shape_(graph, routine.entry, entries.blocks()) {
        std::size_t cost = 0;
        for (std::size_t node = 0; node < shape_.size(); ++node) {
            cost += costOf(node);
        }
        copyLimit_ = options_->duplicationLimit * std::max(cost, options_->sharedTailMinimum);
    }

    Statements structure() {
        if (shape_.irreducible()) {
            return std::unexpected{FallbackReason::Irreducible};
        }
        return walk(0, NoNode, Arrival::Join);
    }

    [[nodiscard]] const std::vector<BlockStart>& sharedTails() const { return newTails_; }

    // Every block as a state of a dispatch loop: the form that holds for any graph.
    [[nodiscard]] StatementList stateMachine() const {
        StateMachineStatement machine{.initial = 0, .states = {}};
        for (std::size_t node = 0; node < shape_.size(); ++node) {
            StatementList body;
            if (shape_.isBoundary(node)) {
                body.push_back({TailCallStatement{.instruction = blockOf(node).first}});
            } else {
                body = runOf(node);
                append(body, transitionOf(node));
            }
            machine.states.push_back({.state = node, .body = std::move(body)});
        }
        StatementList body;
        body.push_back({std::move(machine)});
        return body;
    }

private:
    static void append(StatementList& out, StatementList more) { std::ranges::move(more, std::back_inserter(out)); }

    [[nodiscard]] static StatementList single(Statement&& statement) {
        StatementList list;
        list.push_back(std::move(statement));
        return list;
    }

    [[nodiscard]] const BasicBlock& blockOf(std::size_t node) const { return graph_->blocks[shape_.block(node)]; }

    // What emitting a node costs the copy budget: its instructions, and at least 1 so that blocks made only of an
    // IF's test still count.
    [[nodiscard]] std::size_t costOf(std::size_t node) const { return std::max<std::size_t>(blockOf(node).count, 1); }

    // The block's instructions. A Branch block's run leaves out its IF: the IF's entries only test (they have no
    // effect), so the if statement that the branch becomes stands for it.
    [[nodiscard]] StatementList runOf(std::size_t node) const {
        const BasicBlock& block = blockOf(node);
        if (block.count == 0) {
            return {};
        }
        return single({RunStatement{.first = block.first, .count = block.count}});
    }

    [[nodiscard]] static BranchCondition conditionOf(const BasicBlock& block) {
        return {.instruction = block.terminator.instruction, .entry = block.terminator.entry};
    }

    // The successors of a Branch as {when the condition holds, when it does not}.
    [[nodiscard]] std::pair<std::size_t, std::size_t> branchArms(std::size_t node) const {
        const auto& successors = shape_.successors(node);
        return blockOf(node).terminator.jumpsWhenTrue ? std::pair{successors[0], successors[1]}
                                                      : std::pair{successors[1], successors[0]};
    }

    // How a state of the state machine picks the next one.
    [[nodiscard]] StatementList transitionOf(std::size_t node) const {
        const BasicBlock& block = blockOf(node);
        const auto setState = [](std::size_t state) { return single({SetStateStatement{.state = state}}); };
        switch (block.terminator.kind) {
        case TerminatorKind::Exit:
            return {};
        case TerminatorKind::FallThrough:
        case TerminatorKind::Jump:
            return setState(shape_.successors(node).front());
        case TerminatorKind::Branch: {
            const auto [whenTrue, whenFalse] = branchArms(node);
            return single({IfStatement{
                .condition = conditionOf(block), .whenTrue = setState(whenTrue), .whenFalse = setState(whenFalse)}});
        }
        case TerminatorKind::Choice: {
            ChoiceStatement choice{.instruction = block.terminator.instruction, .arms = {}};
            for (const std::size_t successor : shape_.successors(node)) {
                choice.arms.push_back(setState(successor));
            }
            return single({std::move(choice)});
        }
        }
        return {};
    }

    // The code from `node` until `stop` (or the path's end).
    Statements walk(std::size_t node, std::size_t stop, Arrival arrival) {
        StatementList out;
        while (node != stop) {
            if (arrival != Arrival::LoopEntry) {
                if (auto left = leave(node, arrival)) {
                    if (!*left) {
                        return *left;
                    }
                    append(out, std::move(**left));
                    return out;
                }
            }
            if (shape_.isBoundary(node)) {
                out.push_back({TailCallStatement{.instruction = blockOf(node).first}});
                return out;
            }
            arrival = Arrival::Jump;
            const std::size_t loop = shape_.loopHeadedBy(node);
            if (loop != NoLoop && !isInside(loop)) {
                auto looped = emitLoop(loop);
                if (!looped) {
                    return looped;
                }
                append(out, std::move(*looped));
                node = shape_.loops()[loop].follow;
                if (node == NoNode) {
                    return out;
                }
                arrival = Arrival::Join;
                continue;
            }
            auto run = emitRun(node);
            if (!run) {
                return run;
            }
            append(out, std::move(*run));
            const BasicBlock& block = blockOf(node);
            switch (block.terminator.kind) {
            case TerminatorKind::Exit:
                return out;
            case TerminatorKind::FallThrough:
            case TerminatorKind::Jump:
                node = shape_.successors(node).front();
                continue;
            case TerminatorKind::Branch:
            case TerminatorKind::Choice:
                break;
            }
            const std::size_t join = joinOf(node);
            const std::size_t armStop = join == NoNode ? stop : join;
            auto branched =
                block.terminator.kind == TerminatorKind::Branch ? emitIf(node, armStop) : emitChoice(node, armStop);
            if (!branched) {
                return branched;
            }
            append(out, std::move(*branched));
            if (join == NoNode) {
                return out;
            }
            node = join;
            arrival = Arrival::Join;
        }
        return out;
    }

    // The statements that end the current list at a node: none at the head of the loop the walk is in (the end of
    // the body, so the loop repeats), a break at the loop's follow, a tail call to a routine or shared tail that a
    // jump reaches, or a fallback for a way out of more than one loop. Nothing when the node is code to emit here.
    std::optional<Statements> leave(std::size_t node, Arrival arrival) {
        if (!frames_.empty()) {
            const NaturalLoop& inner = shape_.loops()[frames_.back()];
            if (node == inner.header) {
                return chargeTransfer(StatementList{});
            }
            if (node == inner.follow) {
                return chargeTransfer(single({BreakStatement{}}));
            }
            const auto outerFrames = std::span{frames_}.first(frames_.size() - 1);
            const bool headsOuterLoop = std::ranges::any_of(
                outerFrames, [&](std::size_t frame) { return shape_.loops()[frame].header == node; });
            const std::size_t level = shape_.levelOf(node);
            if (headsOuterLoop || (!inner.body[node] && level != inner.parent && level != NoLoop)) {
                return std::unexpected{FallbackReason::NestedExit};
            }
        }
        if (arrival == Arrival::Jump && shape_.loopOf(node) == NoLoop) {
            return callTail(node);
        }
        return std::nullopt;
    }

    // A break or a loop's repeat costs the copy budget 1 too: arms that only do that still multiply when copied.
    Statements chargeTransfer(StatementList&& statements) {
        if (++copied_ > copyLimit_) {
            return std::unexpected{FallbackReason::Duplication};
        }
        return std::move(statements);
    }

    std::optional<Statements> callTail(std::size_t node) {
        const BasicBlock& block = blockOf(node);
        if (graph_->blockAt(block.first) != shape_.block(node) || node == 0) {
            return std::nullopt;
        }
        const bool known =
            entries_->contains(block.first) || std::ranges::contains(newTails_, block.first, &BlockStart::instruction);
        const bool shared = shape_.forwardPredecessors(node) >= 2 &&
                            reachesAtLeast(*graph_, shape_.block(node), options_->sharedTailMinimum);
        if (!known && !shared) {
            return std::nullopt;
        }
        if (!known) {
            newTails_.push_back({.instruction = block.first, .block = shape_.block(node)});
        }
        return single({TailCallStatement{.instruction = block.first}});
    }

    // Where the arms of a branch meet, if that is inside the loop the walk is in. Arms that meet only past the
    // loop each end on their own (with a break, or at the loop's head), so no code follows a jump back to the head.
    [[nodiscard]] std::size_t joinOf(std::size_t node) const {
        const std::size_t join = shape_.joinAfter(node);
        if (join == NoNode || frames_.empty()) {
            return join;
        }
        return shape_.loops()[frames_.back()].body[join] ? join : NoNode;
    }

    [[nodiscard]] bool isInside(std::size_t loop) const { return std::ranges::contains(frames_, loop); }

    Statements emitRun(std::size_t node) {
        copied_ += costOf(node);
        if (copied_ > copyLimit_) {
            return std::unexpected{FallbackReason::Duplication};
        }
        return runOf(node);
    }

    Statements emitLoop(std::size_t loop) {
        frames_.push_back(loop);
        auto body = walk(shape_.loops()[loop].header, NoNode, Arrival::LoopEntry);
        frames_.pop_back();
        if (!body) {
            return body;
        }
        return single({LoopStatement{.body = std::move(*body)}});
    }

    Statements emitIf(std::size_t node, std::size_t join) {
        const auto [whenTrue, whenFalse] = branchArms(node);
        auto trueArm = walk(whenTrue, join, Arrival::Jump);
        if (!trueArm) {
            return trueArm;
        }
        auto falseArm = walk(whenFalse, join, Arrival::Jump);
        if (!falseArm) {
            return falseArm;
        }
        return single({IfStatement{.condition = conditionOf(blockOf(node)),
                                   .whenTrue = std::move(*trueArm),
                                   .whenFalse = std::move(*falseArm)}});
    }

    Statements emitChoice(std::size_t node, std::size_t join) {
        ChoiceStatement statement{.instruction = blockOf(node).terminator.instruction, .arms = {}};
        for (const std::size_t successor : shape_.successors(node)) {
            auto arm = walk(successor, join, Arrival::Jump);
            if (!arm) {
                return arm;
            }
            statement.arms.push_back(std::move(*arm));
        }
        return single({std::move(statement)});
    }

    const ControlFlowGraph* graph_;
    const RoutineEntries* entries_;
    const StructuringOptions* options_;
    RoutineGraph shape_;
    std::vector<std::size_t> frames_; // the loops the walk is inside, outermost first
    std::vector<BlockStart> newTails_;
    std::size_t copied_ = 0;
    std::size_t copyLimit_ = 0;
};

// The blocks each routine reaches, stopping at none.
std::vector<std::size_t> routinesReaching(const ControlFlowGraph& graph, const std::vector<Routine>& routines) {
    std::vector<std::size_t> reaching(graph.blocks.size(), 0);
    for (const Routine& routine : routines) {
        std::vector<bool> seen(graph.blocks.size(), false);
        std::vector<BlockId> pending{routine.entry};
        while (!pending.empty()) {
            const BlockId block = pending.back();
            pending.pop_back();
            if (seen[block]) {
                continue;
            }
            seen[block] = true;
            ++reaching[block];
            std::ranges::copy(graph.blocks[block].successors, std::back_inserter(pending));
        }
    }
    return reaching;
}

// Code that several routines jump to (a common ending, a loop several sections enter) becomes a shared tail up
// front, so that each routine calls it instead of holding a copy, and a loop entered from several routines is a
// loop with one way in.
std::vector<BlockStart> sharedJumpTargets(const ControlFlowGraph& graph, const std::vector<Routine>& routines,
                                          const RoutineEntries& entries, const StructuringOptions& options) {
    const std::vector<std::size_t> reaching = routinesReaching(graph, routines);
    std::map<std::size_t, BlockId> targets; // by instruction, so the tails come in listing order
    for (const BasicBlock& block : graph.blocks) {
        if (block.terminator.kind != TerminatorKind::Jump) {
            continue;
        }
        const BlockId target = block.successors.front();
        const std::size_t instruction = graph.blocks[target].first;
        if (reaching[target] >= 2 && !entries.contains(instruction) &&
            reachesAtLeast(graph, target, options.sharedTailMinimum)) {
            targets.emplace(instruction, target);
        }
    }
    std::vector<BlockStart> tails;
    tails.reserve(targets.size());
    for (const auto& [instruction, target] : targets) {
        tails.push_back({.instruction = instruction, .block = target});
    }
    return tails;
}

} // namespace

core::Result<StructuredScript> structureScript(const ScriptListing& listing, const StructuringOptions& options) {
    auto flow = buildScriptFlow(listing);
    if (!flow) {
        return std::unexpected{flow.error()};
    }
    std::vector<Routine> routines = flow->routines;
    RoutineEntries entries{routines};
    for (const BlockStart& tail : entries.add(sharedJumpTargets(flow->graph, routines, entries, options))) {
        routines.push_back({.kind = RoutineKind::SharedTail, .instruction = tail.instruction, .entry = tail.block});
    }
    StructuredScript script;
    for (std::size_t index = 0; index < routines.size(); ++index) {
        const Routine routine = routines[index];
        RoutineStructurer structurer{flow->graph, routine, entries, options};
        StructuredRoutine structured{.kind = routine.kind,
                                     .section = routine.section,
                                     .instruction = routine.instruction,
                                     .body = {},
                                     .fallback = std::nullopt};
        if (auto body = structurer.structure()) {
            structured.body = std::move(*body);
            for (const BlockStart& tail : entries.add(structurer.sharedTails())) {
                routines.push_back(
                    {.kind = RoutineKind::SharedTail, .instruction = tail.instruction, .entry = tail.block});
            }
        } else {
            structured.fallback = body.error();
            structured.body = structurer.stateMachine();
        }
        script.routines.push_back(std::move(structured));
    }
    return script;
}

} // namespace dwe::formats::script
