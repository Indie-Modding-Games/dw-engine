#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::game::randomizer {

// The chance of learning a technique an enemy showed in battle: the move table's own, or a fixed
// one for every technique the partner can learn.
enum class LearnChance : std::uint8_t { Original, Quarter, Half, ThreeQuarters, Always };

inline constexpr std::array<LearnChance, 5> LearnChances{
    LearnChance::Original, LearnChance::Quarter, LearnChance::Half, LearnChance::ThreeQuarters,
    LearnChance::Always};

// The fixed chance in 100, or nothing for the move table's own.
[[nodiscard]] std::optional<std::int32_t> percentOf(LearnChance chance);
// "original", "25", "50", "75" or "100", as saves and the setup screen name them.
[[nodiscard]] std::string_view nameOf(LearnChance chance);
[[nodiscard]] std::optional<LearnChance> learnChanceNamed(std::string_view name);

// What a randomized run changes, fixed when it starts and kept in its save.
struct RunSettings {
  std::uint64_t seed = 0;
  bool evolutions = false;
  LearnChance learnChance = LearnChance::Original;
  bool starter = false;
  bool enemies = false;
  bool items = false;
  bool drops = false;

  // Whether the run changes anything: an inactive run plays as the original.
  [[nodiscard]] bool active() const;

  friend bool operator==(const RunSettings&, const RunSettings&) = default;
};

// Every option on, techniques always learned.
[[nodiscard]] RunSettings chaos(std::uint64_t seed);

// Seeds are 40 bits, written as 8 letters and digits (Crockford's base 32: no I, L, O or U) so
// players can read them out and type them.
inline constexpr std::size_t SeedLength = 8;
inline constexpr std::uint64_t SeedMask = (std::uint64_t{1} << 40U) - 1U;

[[nodiscard]] std::string seedText(std::uint64_t seed);

// The run code players share: the seed, a dash and two characters with the options
// ("K7QH2M9X-3Z").
[[nodiscard]] std::string runCode(const RunSettings& settings);

// A seed or a run code as typed or pasted: case, spaces and dashes do not matter, and I, L and O
// read as 1, 1 and 0. A seed alone keeps the options as they are.
struct ParsedCode {
  std::uint64_t seed = 0;
  std::optional<RunSettings> options;
};
[[nodiscard]] std::optional<ParsedCode> parseCode(std::string_view text);

// Whether a typed character can be part of a code.
[[nodiscard]] bool codeCharacter(char character);

} // namespace dwe::game::randomizer
