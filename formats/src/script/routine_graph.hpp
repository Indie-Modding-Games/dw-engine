#pragma once

#include <dwe/formats/script/control_flow.hpp>

#include <cstddef>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "dominance.hpp"

// The analysis of one routine for the structuring pass: its blocks, dominators, loops and post-dominators.
namespace dwe::formats::script {

inline constexpr std::size_t NoLoop = NoNode;

struct NaturalLoop {
    std::size_t header = 0;
    std::vector<bool> body;      // by node
    std::size_t size = 0;        // nodes in the body
    std::size_t parent = NoLoop; // the innermost loop around this one
    std::size_t follow = NoNode; // where a break goes: the exit the loop's paths join at, if any
};

class RoutineGraph {
public:
    // `boundaries` are the entries of other routines: the graph stops there, as the routine calls them as tails.
    RoutineGraph(const ControlFlowGraph& graph, BlockId entry, const std::set<BlockId>& boundaries);

    // Nodes are the routine's blocks, numbered from 0 (the entry).
    [[nodiscard]] std::size_t size() const { return blocks_.size(); }
    [[nodiscard]] BlockId block(std::size_t node) const { return blocks_[node]; }
    [[nodiscard]] const std::vector<std::size_t>& successors(std::size_t node) const { return flow_.successors[node]; }

    // Whether a node is the entry of another routine, which this one calls as its tail.
    [[nodiscard]] bool isBoundary(std::size_t node) const { return boundary_[node]; }

    // Whether some loop of the routine has more than one way in.
    [[nodiscard]] bool irreducible() const { return irreducible_; }

    [[nodiscard]] const std::vector<NaturalLoop>& loops() const { return loops_; }
    // The innermost loop whose body holds a node, or NoLoop.
    [[nodiscard]] std::size_t loopOf(std::size_t node) const { return innermostLoop_[node]; }
    // The loop a node heads, or NoLoop.
    [[nodiscard]] std::size_t loopHeadedBy(std::size_t node) const { return headedLoop_[node]; }
    // The loop a node's statement sits in: for a loop's head, the loop around that loop; else loopOf.
    [[nodiscard]] std::size_t levelOf(std::size_t node) const;

    // Where the paths from a node meet again once the loops' back edges are taken out, or NoNode if they only end.
    [[nodiscard]] std::size_t joinAfter(std::size_t node) const;
    // How many edges other than back edges come into a node.
    [[nodiscard]] std::size_t forwardPredecessors(std::size_t node) const { return forwardPredecessors_[node]; }

private:
    void collectBlocks(const ControlFlowGraph& graph, BlockId entry, const std::set<BlockId>& boundaries);
    std::map<std::size_t, std::vector<std::size_t>> findBackEdges();
    [[nodiscard]] NaturalLoop collectBody(std::size_t header, const std::vector<std::size_t>& latches,
                                          const Digraph& predecessors) const;
    void findLoops();
    void nestLoops();
    void findPostDominators();
    [[nodiscard]] bool isAround(const NaturalLoop& loop, std::size_t node) const;
    void findFollows();

    std::vector<BlockId> blocks_;
    std::vector<bool> boundary_;
    std::unordered_map<BlockId, std::size_t> nodeOf_;
    Digraph flow_;
    Digraph acyclic_; // flow_ without the back edges
    std::vector<std::size_t> dominators_;
    std::vector<std::size_t> postDominators_; // over acyclic_ plus a virtual exit (node size())
    std::vector<NaturalLoop> loops_;
    std::vector<std::size_t> innermostLoop_;
    std::vector<std::size_t> headedLoop_;
    std::vector<std::size_t> forwardPredecessors_;
    bool irreducible_ = false;
};

} // namespace dwe::formats::script
