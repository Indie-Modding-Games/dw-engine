#include "routine_graph.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <utility>

namespace dwe::formats::script {

RoutineGraph::RoutineGraph(const ControlFlowGraph& graph, BlockId entry, const std::set<BlockId>& boundaries) {
    collectBlocks(graph, entry, boundaries);
    dominators_ = immediateDominators(flow_, 0);
    findLoops();
    nestLoops();
    findPostDominators();
    findFollows();
}

void RoutineGraph::collectBlocks(const ControlFlowGraph& graph, BlockId entry, const std::set<BlockId>& boundaries) {
    const auto nodeFor = [&](BlockId block) {
        const auto [found, inserted] = nodeOf_.try_emplace(block, blocks_.size());
        if (inserted) {
            blocks_.push_back(block);
        }
        return found->second;
    };
    nodeFor(entry);
    for (std::size_t node = 0; node < blocks_.size(); ++node) {
        const bool boundary = node != 0 && boundaries.contains(blocks_[node]);
        boundary_.push_back(boundary);
        std::vector<std::size_t> successors;
        for (const BlockId successor : boundary ? std::vector<BlockId>{} : graph.blocks[blocks_[node]].successors) {
            successors.push_back(nodeFor(successor));
        }
        flow_.successors.push_back(std::move(successors));
    }
}

// Finds the back edges with a depth-first walk: an edge to a node on the walk's stack closes a loop. If that node
// dominates the edge's source it heads a natural loop; otherwise the loop has another way in and is irreducible.
// Every other edge goes to acyclic_. Returns the sources of the back edges by the header they go to.
std::map<std::size_t, std::vector<std::size_t>> RoutineGraph::findBackEdges() {
    struct Visit {
        std::size_t node;
        std::size_t next;
    };
    std::vector<bool> seen(size(), false);
    std::vector<bool> onStack(size(), false);
    std::map<std::size_t, std::vector<std::size_t>> latchesByHeader;
    acyclic_.successors.assign(size(), {});
    std::vector<Visit> stack{{.node = 0, .next = 0}};
    seen[0] = onStack[0] = true;
    while (!stack.empty()) {
        Visit& visit = stack.back();
        if (visit.next == flow_.successors[visit.node].size()) {
            onStack[visit.node] = false;
            stack.pop_back();
            continue;
        }
        const std::size_t source = visit.node;
        const std::size_t target = flow_.successors[source][visit.next++];
        if (onStack[target]) {
            if (dominates(dominators_, target, source)) {
                latchesByHeader[target].push_back(source);
            } else {
                irreducible_ = true; // a second way into a loop; leaving the edge out keeps acyclic_ free of cycles
            }
            continue;
        }
        acyclic_.successors[source].push_back(target);
        if (!seen[target]) {
            seen[target] = onStack[target] = true;
            stack.push_back({.node = target, .next = 0});
        }
    }
    return latchesByHeader;
}

// The body of a natural loop: its header and every node that reaches a latch without passing the header.
NaturalLoop RoutineGraph::collectBody(std::size_t header, const std::vector<std::size_t>& latches,
                                      const Digraph& predecessors) const {
    NaturalLoop loop{
        .header = header, .body = std::vector<bool>(size(), false), .size = 0, .parent = NoLoop, .follow = NoNode};
    loop.body[header] = true;
    std::vector<std::size_t> pending = latches;
    while (!pending.empty()) {
        const std::size_t node = pending.back();
        pending.pop_back();
        if (loop.body[node]) {
            continue;
        }
        loop.body[node] = true;
        std::ranges::copy(predecessors.successors[node], std::back_inserter(pending));
    }
    loop.size = static_cast<std::size_t>(std::ranges::count(loop.body, true));
    return loop;
}

void RoutineGraph::findLoops() {
    const auto latchesByHeader = findBackEdges();
    const Digraph predecessors = reverseGraph(flow_);
    for (const auto& [header, latches] : latchesByHeader) {
        loops_.push_back(collectBody(header, latches, predecessors));
    }
}

// Natural loops with different headers are nested or apart, so the innermost loop of a node is the smallest that
// holds it.
void RoutineGraph::nestLoops() {
    const auto smallestHolding = [&](std::size_t node, std::size_t excluded) {
        std::size_t best = NoLoop;
        for (std::size_t loop = 0; loop < loops_.size(); ++loop) {
            if (loop != excluded && loops_[loop].body[node] &&
                (best == NoLoop || loops_[loop].size < loops_[best].size)) {
                best = loop;
            }
        }
        return best;
    };
    innermostLoop_.assign(size(), NoLoop);
    headedLoop_.assign(size(), NoLoop);
    for (std::size_t node = 0; node < size(); ++node) {
        innermostLoop_[node] = smallestHolding(node, NoLoop);
    }
    for (std::size_t loop = 0; loop < loops_.size(); ++loop) {
        headedLoop_[loops_[loop].header] = loop;
        loops_[loop].parent = smallestHolding(loops_[loop].header, loop);
    }
    forwardPredecessors_.assign(size(), 0);
    for (const auto& successors : acyclic_.successors) {
        for (const std::size_t successor : successors) {
            ++forwardPredecessors_[successor];
        }
    }
}

// Post-dominators of the acyclic graph with a virtual exit. Ends and edges back to a loop's head lead to the exit:
// Lua has no continue, so code after a branch that may start the loop over cannot be shared by both arms.
void RoutineGraph::findPostDominators() {
    Digraph withExit = acyclic_;
    withExit.successors.emplace_back();
    for (std::size_t node = 0; node < size(); ++node) {
        if (withExit.successors[node].size() < flow_.successors[node].size() || withExit.successors[node].empty()) {
            withExit.successors[node].push_back(size());
        }
    }
    postDominators_ = immediateDominators(reverseGraph(withExit), size());
}

std::size_t RoutineGraph::levelOf(std::size_t node) const {
    const std::size_t headed = headedLoop_[node];
    return headed != NoLoop ? loops_[headed].parent : innermostLoop_[node];
}

// Whether a node outside a loop is at the level around it: in the loop around it (a sibling loop's head included),
// or that loop's head, where a break that ends the outer body lands.
bool RoutineGraph::isAround(const NaturalLoop& loop, std::size_t node) const {
    return levelOf(node) == loop.parent || (loop.parent != NoLoop && loops_[loop.parent].header == node);
}

std::size_t RoutineGraph::joinAfter(std::size_t node) const {
    const std::size_t join = postDominators_[node];
    return join == size() ? NoNode : join;
}

// A loop's follow is where its exits at the level around it meet again; failing that, the exit most edges take.
// Exits to other levels leave it from inside, as copies that end the routine or as a fallback.
void RoutineGraph::findFollows() {
    for (NaturalLoop& loop : loops_) {
        std::map<std::size_t, std::size_t> exitEdges;
        for (std::size_t node = 0; node < size(); ++node) {
            if (!loop.body[node]) {
                continue;
            }
            for (const std::size_t successor : flow_.successors[node]) {
                if (!loop.body[successor] && isAround(loop, successor)) {
                    ++exitEdges[successor];
                }
            }
        }
        if (exitEdges.empty()) {
            continue;
        }
        std::size_t join = exitEdges.begin()->first;
        for (const auto& [exit, edges] : exitEdges) {
            join = join == NoNode ? NoNode : nearestCommonDominator(postDominators_, join, exit);
        }
        if (join != size() && join != NoNode && isAround(loop, join)) {
            loop.follow = join;
            continue;
        }
        loop.follow = std::ranges::max_element(exitEdges, {}, [](const auto& entry) { return entry.second; })->first;
    }
}

} // namespace dwe::formats::script
