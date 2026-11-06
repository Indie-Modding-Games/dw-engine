#pragma once

#include <dwe/core/error.hpp>

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::core {

struct PatternNode;

// The pattern keyword of a JSON Schema: the subset of ECMAScript patterns that JsonSchema
// documents. It runs as a Pike virtual machine, where every possible match advances in step over
// the text, so the time is linear in the text and nothing recurses.
class SchemaPattern {
public:
  // Compiles a pattern; anything outside the subset is an error.
  static Result<SchemaPattern> compile(std::string_view source);

  [[nodiscard]] const std::string& source() const { return source_; }

  // Whether the pattern matches anywhere in the text, as JSON Schema's pattern does.
  [[nodiscard]] bool search(std::string_view text) const;

private:
  enum class Op : std::uint8_t { Byte, Split, Jump, Start, End, Match };

  struct Instruction {
    Op op = Op::Match;
    std::bitset<128> set; // the ASCII bytes a Byte instruction takes
    std::size_t x = 0;
    std::size_t y = 0;
  };

  std::size_t push(Op op);
  bool emit(const PatternNode& node);
  bool emitAlternation(std::span<const PatternNode> choices);
  bool emitRepeat(const PatternNode& child, std::size_t min, std::size_t max);

  std::string source_;
  std::vector<Instruction> program_;
};

} // namespace dwe::core
