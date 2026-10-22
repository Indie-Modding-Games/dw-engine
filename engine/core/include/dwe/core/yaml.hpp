#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::core {

struct YamlEntry;

// A YAML document as a tree of scalars, sequences and mappings. Scalars are kept as text: what a
// value means (a number, a version, an asset id) is decided by whoever reads it. Mappings keep the
// order of their keys, so a document read and written again keeps its layout.
class YamlNode {
public:
  enum class Kind : std::uint8_t { Scalar, Sequence, Mapping };

  // Plain scalars are written without quotes when YAML allows it; quoted ones always in double
  // quotes, so other tools read them as strings and never as numbers or booleans.
  enum class ScalarStyle : std::uint8_t { Plain, Quoted };

  // Builds a scalar.
  static YamlNode scalar(std::string text, ScalarStyle style = ScalarStyle::Plain);

  // Builds an empty sequence.
  static YamlNode sequence();

  // Builds an empty mapping.
  static YamlNode mapping();

  [[nodiscard]] Kind kind() const { return kind_; }
  [[nodiscard]] bool isScalar() const { return kind_ == Kind::Scalar; }
  [[nodiscard]] bool isSequence() const { return kind_ == Kind::Sequence; }
  [[nodiscard]] bool isMapping() const { return kind_ == Kind::Mapping; }

  // The text of a scalar.
  [[nodiscard]] const std::string& text() const;

  // How a scalar is quoted.
  [[nodiscard]] ScalarStyle style() const { return style_; }

  // The items of a sequence.
  [[nodiscard]] std::span<const YamlNode> items() const;

  // The entries of a mapping, in the order of the document.
  [[nodiscard]] std::span<const YamlEntry> entries() const;

  // Returns the value of a mapping's key, or nullptr if the mapping does not have it.
  [[nodiscard]] const YamlNode* find(std::string_view key) const;

  // Adds an item at the end of a sequence.
  void append(YamlNode item);

  // Sets the value of a mapping's key: an existing key keeps its place, a new one goes at the end.
  void set(std::string key, YamlNode value);

  // Where the node starts in the parsed text, counted from 1; 0 for nodes built in code.
  [[nodiscard]] std::size_t line() const { return line_; }
  [[nodiscard]] std::size_t column() const { return column_; }

  // Compares kinds, texts and children; quoting and positions do not count.
  friend bool operator==(const YamlNode& lhs, const YamlNode& rhs);

private:
  // The parser appends keys it has already checked and records positions.
  friend class YamlTreeBuilder;

  // Out of line: the vector of entries can only be built once YamlEntry is complete.
  explicit YamlNode(Kind kind);

  Kind kind_;
  ScalarStyle style_ = ScalarStyle::Plain;
  std::string text_;
  std::vector<YamlNode> items_;
  std::vector<YamlEntry> entries_;
  std::size_t line_ = 0;
  std::size_t column_ = 0;
};

struct YamlEntry {
  std::string key;
  YamlNode value;
};

// What parseYaml accepts. The defaults are generous for hand-written data and keep hostile input
// bounded.
struct YamlLimits {
  std::size_t maxBytes = std::size_t{16} * 1024 * 1024;
  std::size_t maxDepth = 64;
};

// Parses a single YAML document. Anchors, aliases, tags, duplicate keys and keys that are not
// scalars are errors, as is anything past the limits. Errors name the line and column. An empty
// text is an empty plain scalar.
Result<YamlNode> parseYaml(std::string_view text, const YamlLimits& limits = {});

// Writes a node as YAML: UTF-8, LF line ends, two-space indentation, keys in the node's order and a
// final newline. The same tree always gives the same text. Fails only on text that is not valid
// UTF-8.
Result<std::string> emitYaml(const YamlNode& node);

} // namespace dwe::core
