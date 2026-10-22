#include <dwe/formats/script/control_flow.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "../messages.hpp"

namespace dwe::formats::script {

namespace {

bool runsOn(FlowRole role) {
    return role == FlowRole::Plain || role == FlowRole::Call || role == FlowRole::FileCall ||
           role == FlowRole::LoadScript;
}

bool endsFlow(FlowRole role) {
    return role == FlowRole::Return || role == FlowRole::FileJump || role == FlowRole::EndSection ||
           role == FlowRole::EndScript || role == FlowRole::Data;
}

// The listing indices where a block must start: the entry points, every destination, and the instruction after
// one that does not simply run on. An offset with no instruction is skipped here; the builder reports it.
std::set<std::size_t> findLeaders(const ScriptListing& listing) {
    std::set<std::size_t> leaders;
    const auto addOffset = [&](std::uint32_t offset) {
        if (const auto index = listing.indexAt(offset)) {
            leaders.insert(*index);
        }
    };
    for (const ListingSection& section : listing.sections) {
        addOffset(section.entry);
    }
    for (std::size_t index = 0; index < listing.instructions.size(); ++index) {
        const ListingInstruction& instruction = listing.instructions[index];
        std::ranges::for_each(instruction.targets, addOffset);
        for (const ConditionalJump& jump : instruction.conditionalJumps) {
            addOffset(jump.target);
        }
        if (!runsOn(instruction.role) && index + 1 < listing.instructions.size()) {
            leaders.insert(index + 1);
        }
    }
    return leaders;
}

// Builds the blocks on demand, from the entry points along every edge.
class FlowBuilder {
public:
    explicit FlowBuilder(const ScriptListing& listing) : listing_(&listing), leaders_(findLeaders(listing)) {}

    BlockId blockStartingAt(std::size_t instruction) {
        const auto [found, inserted] = starts_.try_emplace(instruction, graph_.blocks.size());
        if (inserted) {
            graph_.blocks.push_back({.first = instruction, .count = 0, .terminator = {}, .successors = {}});
            pending_.push_back(found->second);
        }
        return found->second;
    }

    core::Result<ControlFlowGraph> build() && {
        while (!pending_.empty()) {
            const BlockId block = pending_.back();
            pending_.pop_back();
            if (auto filled = fill(block); !filled) {
                return std::unexpected{filled.error()};
            }
        }
        for (const auto& [instruction, block] : starts_) {
            graph_.starts.push_back({.instruction = instruction, .block = block});
        }
        return std::move(graph_);
    }

private:
    core::Result<BlockId> blockAfter(std::size_t instruction) {
        if (instruction + 1 >= listing_->instructions.size()) {
            return core::fail(
                messages::formatMessage(messages::FlowRunsOffEnd, listing_->instructions[instruction].offset));
        }
        return blockStartingAt(instruction + 1);
    }

    core::Result<BlockId> blockAtOffset(std::uint32_t offset) {
        const auto index = listing_->indexAt(offset);
        if (!index) {
            return core::fail(messages::formatMessage(messages::ListingNoInstruction, "target", offset));
        }
        return blockStartingAt(*index);
    }

    core::Result<void> fill(BlockId block) {
        std::size_t index = graph_.blocks[block].first;
        while (runsOn(listing_->instructions[index].role)) {
            ++graph_.blocks[block].count;
            if (index + 1 >= listing_->instructions.size() || leaders_.contains(index + 1)) {
                graph_.blocks[block].terminator = {.kind = TerminatorKind::FallThrough, .instruction = index};
                return link(block, blockAfter(index));
            }
            ++index;
        }
        return terminate(block, index);
    }

    core::Result<void> link(BlockId block, const core::Result<BlockId>& successor) {
        if (!successor) {
            return std::unexpected{successor.error()};
        }
        graph_.blocks[block].successors.push_back(*successor);
        return {};
    }

    // An exit instruction ends its block's run. Any other terminator stays out of the run: the statement it becomes
    // stands for it (an IF's entries only test and change nothing, so the if statement is all there is of it).
    core::Result<void> terminate(BlockId block, std::size_t index) {
        const ListingInstruction& instruction = listing_->instructions[index];
        if (endsFlow(instruction.role)) {
            ++graph_.blocks[block].count;
            graph_.blocks[block].terminator = {.kind = TerminatorKind::Exit, .instruction = index};
            return {};
        }
        switch (instruction.role) {
        case FlowRole::Jump:
            graph_.blocks[block].terminator = {.kind = TerminatorKind::Jump, .instruction = index};
            return link(block, blockAtOffset(instruction.targets.front()));
        case FlowRole::If:
            return branch(block, index);
        case FlowRole::Switch:
            return choose(block, index, false);
        case FlowRole::Selection:
            return choose(block, index, true);
        default:
            return {};
        }
    }

    // An IF without jump entries only falls through; each jump entry is a Branch, chained on its not-taken side.
    core::Result<void> branch(BlockId block, std::size_t index) {
        const ListingInstruction& instruction = listing_->instructions[index];
        if (instruction.conditionalJumps.empty()) {
            graph_.blocks[block].terminator = {.kind = TerminatorKind::FallThrough, .instruction = index};
            return link(block, blockAfter(index));
        }
        BlockId current = block;
        for (std::size_t jump = 0; jump < instruction.conditionalJumps.size(); ++jump) {
            const ConditionalJump& entry = instruction.conditionalJumps[jump];
            graph_.blocks[current].terminator = {.kind = TerminatorKind::Branch,
                                                 .instruction = index,
                                                 .entry = entry.entry,
                                                 .jumpsWhenTrue = entry.whenTrue};
            if (auto linked = link(current, blockAtOffset(entry.target)); !linked) {
                return linked;
            }
            if (jump + 1 == instruction.conditionalJumps.size()) {
                return link(current, blockAfter(index));
            }
            const BlockId chained = graph_.blocks.size();
            graph_.blocks.push_back({.first = index, .count = 0, .terminator = {}, .successors = {}});
            graph_.blocks[current].successors.push_back(chained);
            current = chained;
        }
        return {};
    }

    // SWITCH goes to its cases (a value past the last goes to the last one, script_interp.c: tickScript); a
    // SET_SELECTION to its options, and on a cancel to the instruction after it.
    core::Result<void> choose(BlockId block, std::size_t index, bool cancelGoesOn) {
        graph_.blocks[block].terminator = {.kind = TerminatorKind::Choice, .instruction = index};
        for (const std::uint32_t target : listing_->instructions[index].targets) {
            if (auto linked = link(block, blockAtOffset(target)); !linked) {
                return linked;
            }
        }
        return cancelGoesOn ? link(block, blockAfter(index)) : core::Result<void>{};
    }

    const ScriptListing* listing_;
    std::set<std::size_t> leaders_;
    ControlFlowGraph graph_;
    std::map<std::size_t, BlockId> starts_;
    std::vector<BlockId> pending_;
};

// The jump_and_link targets that are not a section's entry already.
std::vector<std::size_t> subroutineEntries(const ScriptListing& listing) {
    std::set<std::size_t> entries;
    for (const ListingInstruction& instruction : listing.instructions) {
        const auto target =
            instruction.role == FlowRole::Call ? listing.indexAt(instruction.targets.front()) : std::nullopt;
        if (target) {
            entries.insert(*target);
        }
    }
    for (const ListingSection& section : listing.sections) {
        if (const auto entry = listing.indexAt(section.entry)) {
            entries.erase(*entry);
        }
    }
    return {entries.begin(), entries.end()};
}

} // namespace

std::optional<BlockId> ControlFlowGraph::blockAt(std::size_t instruction) const {
    const auto found = std::ranges::lower_bound(starts, instruction, {}, &BlockStart::instruction);
    if (found == starts.end() || found->instruction != instruction) {
        return std::nullopt;
    }
    return found->block;
}

core::Result<ScriptFlow> buildScriptFlow(const ScriptListing& listing) {
    FlowBuilder builder{listing};
    std::vector<Routine> routines;
    for (const ListingSection& section : listing.sections) {
        const auto instruction = listing.indexAt(section.entry);
        if (!instruction) {
            return core::fail(messages::formatMessage(messages::ListingNoInstruction, "section", section.entry));
        }
        routines.push_back({.kind = RoutineKind::Section,
                            .section = section.id,
                            .instruction = *instruction,
                            .entry = builder.blockStartingAt(*instruction)});
    }
    for (const std::size_t instruction : subroutineEntries(listing)) {
        routines.push_back({.kind = RoutineKind::Subroutine,
                            .instruction = instruction,
                            .entry = builder.blockStartingAt(instruction)});
    }
    auto graph = std::move(builder).build();
    if (!graph) {
        return std::unexpected{graph.error()};
    }
    return ScriptFlow{.graph = std::move(*graph), .routines = std::move(routines)};
}

} // namespace dwe::formats::script
