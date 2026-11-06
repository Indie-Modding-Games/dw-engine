#pragma once

#include <cstddef>
#include <limits>
#include <vector>

// Dominator trees of small directed graphs, for the structuring pass.
namespace dwe::formats::script {

// A graph over nodes 0..n-1 as successor lists.
struct Digraph {
  std::vector<std::vector<std::size_t>> successors;

  [[nodiscard]] std::size_t size() const { return successors.size(); }
};

inline constexpr std::size_t NoNode = std::numeric_limits<std::size_t>::max();

// The same graph with every edge reversed.
Digraph reverseGraph(const Digraph& graph);

// The immediate dominator of each node, seen from root (Cooper, Harvey and Kennedy's iterative
// algorithm). The root is its own; nodes the root does not reach have NoNode.
std::vector<std::size_t> immediateDominators(const Digraph& graph, std::size_t root);

// Whether `dominator` dominates `node` in a tree given by immediateDominators.
bool dominates(const std::vector<std::size_t>& dominators, std::size_t dominator, std::size_t node);

// The nearest common ancestor of two nodes in a tree given by immediateDominators.
std::size_t nearestCommonDominator(const std::vector<std::size_t>& dominators, std::size_t first,
                                   std::size_t second);

} // namespace dwe::formats::script
