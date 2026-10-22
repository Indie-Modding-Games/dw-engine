#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>

#include <doctest/doctest.h>

#include <charconv>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The edits the map editor makes to a document of the engine: it reads the YAML into a tree,
// changes one value, and writes the tree back with emitYaml
// (docs/architecture/herramientas-externas.md).
namespace dwe::dwx::testing {

// The steps from the root to a node: a key of a mapping or the index of a sequence's item.
using YamlPath = std::vector<std::string>;

namespace detail {

inline std::size_t itemIndex(std::string_view step) {
  std::size_t index = 0;
  const auto parsed = std::from_chars(step.data(), step.data() + step.size(), index);
  REQUIRE_MESSAGE(parsed.ec == std::errc{}, "not an item index: ", step);
  return index;
}

// The tree with the node at the end of the path replaced by what change makes of it.
template <typename Change>
core::YamlNode changedAt(const core::YamlNode& node, std::span<const std::string> path,
                         const Change& change) {
  if (path.empty()) {
    return change(node);
  }
  const std::string& step = path.front();
  if (node.isSequence()) {
    const std::size_t index = itemIndex(step);
    REQUIRE(index < node.items().size());
    core::YamlNode sequence = core::YamlNode::sequence();
    for (std::size_t item = 0; item < node.items().size(); ++item) {
      sequence.append(item == index ? changedAt(node.items()[item], path.subspan(1), change)
                                    : node.items()[item]);
    }
    return sequence;
  }
  const core::YamlNode* child = node.find(step);
  REQUIRE_MESSAGE(child != nullptr, "no key ", step);
  core::YamlNode mapping = node;
  mapping.set(step, changedAt(*child, path.subspan(1), change));
  return mapping;
}

} // namespace detail

// The node at a path, which must exist.
inline const core::YamlNode& nodeAt(const core::YamlNode& root, const YamlPath& path) {
  const core::YamlNode* node = &root;
  for (const std::string& step : path) {
    if (node->isSequence()) {
      const std::size_t index = detail::itemIndex(step);
      REQUIRE(index < node->items().size());
      node = &node->items()[index];
    } else {
      node = node->find(step);
      REQUIRE_MESSAGE(node != nullptr, "no key ", step);
    }
  }
  return *node;
}

// The tree with the scalar at a path set to text, quoted as it was.
inline core::YamlNode withScalar(const core::YamlNode& root, const YamlPath& path,
                                 const std::string& text) {
  return detail::changedAt(root, path, [&text](const core::YamlNode& old) {
    return core::YamlNode::scalar(text, old.style());
  });
}

// The tree with a key added at the end of the mapping at a path.
inline core::YamlNode withKey(const core::YamlNode& root, const YamlPath& path,
                              const std::string& key, const core::YamlNode& value) {
  return detail::changedAt(root, path, [&](const core::YamlNode& mapping) {
    core::YamlNode added = mapping;
    added.set(key, value);
    return added;
  });
}

// The tree without a key of the mapping at a path.
inline core::YamlNode withoutKey(const core::YamlNode& root, const YamlPath& path,
                                 std::string_view key) {
  return detail::changedAt(root, path, [key](const core::YamlNode& mapping) {
    core::YamlNode kept = core::YamlNode::mapping();
    for (const core::YamlEntry& entry : mapping.entries()) {
      if (entry.key != key) {
        kept.set(entry.key, entry.value);
      }
    }
    return kept;
  });
}

// The message of a failed result, or nothing. doctest builds a message even when the check passes,
// so it cannot call error() unguarded.
template <typename T>
std::string errorOf(const core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

// A tree written with emitYaml, which must succeed.
inline std::string emitted(const core::YamlNode& root) {
  const auto text = core::emitYaml(root);
  REQUIRE_MESSAGE(text.has_value(), errorOf(text));
  return text.value_or(std::string{});
}

// A text read into a tree, which must succeed.
inline core::YamlNode parsed(std::string_view text) {
  auto root = core::parseYaml(text);
  REQUIRE_MESSAGE(root.has_value(), errorOf(root));
  return root.value_or(core::YamlNode::mapping());
}

// What the editor writes for a document it opens and saves without changes.
inline std::string savedUnchanged(std::string_view text) {
  return emitted(parsed(text));
}

} // namespace dwe::dwx::testing
