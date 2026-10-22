#include "dominance.hpp"

#include <algorithm>

namespace dwe::formats::script {

namespace {

// Nodes in reverse postorder from root.
std::vector<std::size_t> reversePostorder(const Digraph& graph, std::size_t root) {
  std::vector<std::size_t> order;
  std::vector<bool> seen(graph.size(), false);
  struct Visit {
    std::size_t node;
    std::size_t next;
  };
  std::vector<Visit> stack{{.node = root, .next = 0}};
  seen[root] = true;
  while (!stack.empty()) {
    Visit& visit = stack.back();
    if (visit.next < graph.successors[visit.node].size()) {
      const std::size_t successor = graph.successors[visit.node][visit.next++];
      if (!seen[successor]) {
        seen[successor] = true;
        stack.push_back({.node = successor, .next = 0});
      }
      continue;
    }
    order.push_back(visit.node);
    stack.pop_back();
  }
  std::ranges::reverse(order);
  return order;
}

std::size_t intersect(const std::vector<std::size_t>& dominators,
                      const std::vector<std::size_t>& rank, std::size_t first, std::size_t second) {
  while (first != second) {
    while (rank[first] > rank[second]) {
      first = dominators[first];
    }
    while (rank[second] > rank[first]) {
      second = dominators[second];
    }
  }
  return first;
}

} // namespace

Digraph reverseGraph(const Digraph& graph) {
  Digraph reversed{.successors = std::vector<std::vector<std::size_t>>(graph.size())};
  for (std::size_t node = 0; node < graph.size(); ++node) {
    for (const std::size_t successor : graph.successors[node]) {
      reversed.successors[successor].push_back(node);
    }
  }
  return reversed;
}

std::vector<std::size_t> immediateDominators(const Digraph& graph, std::size_t root) {
  const std::vector<std::size_t> order = reversePostorder(graph, root);
  std::vector<std::size_t> rank(graph.size(), NoNode);
  for (std::size_t position = 0; position < order.size(); ++position) {
    rank[order[position]] = position;
  }
  const Digraph predecessors = reverseGraph(graph);
  std::vector<std::size_t> dominators(graph.size(), NoNode);
  dominators[root] = root;
  bool changed = true;
  while (changed) {
    changed = false;
    for (const std::size_t node : order) {
      if (node == root) {
        continue;
      }
      std::size_t candidate = NoNode;
      for (const std::size_t predecessor : predecessors.successors[node]) {
        if (dominators[predecessor] == NoNode) {
          continue;
        }
        candidate =
            candidate == NoNode ? predecessor : intersect(dominators, rank, candidate, predecessor);
      }
      if (candidate != dominators[node]) {
        dominators[node] = candidate;
        changed = true;
      }
    }
  }
  return dominators;
}

bool dominates(const std::vector<std::size_t>& dominators, std::size_t dominator,
               std::size_t node) {
  while (node != NoNode) {
    if (node == dominator) {
      return true;
    }
    if (dominators[node] == node) {
      return false;
    }
    node = dominators[node];
  }
  return false;
}

std::size_t nearestCommonDominator(const std::vector<std::size_t>& dominators, std::size_t first,
                                   std::size_t second) {
  while (first != NoNode && !dominates(dominators, first, second) && dominators[first] != first) {
    first = dominators[first];
  }
  return first == NoNode || !dominates(dominators, first, second) ? NoNode : first;
}

} // namespace dwe::formats::script
