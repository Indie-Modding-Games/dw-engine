#pragma once

#include <cstdint>

namespace dwe::ruleset {

// The random numbers of the game rules: seedable, so a test or a simulation with the same seed
// plays the same game on every platform (the standard distributions differ between standard
// libraries, so none is used). The rulesets keep the original's probabilities and distributions,
// not its generator: the design owner decided on 2026-10-20 that the rolls need not repeat the
// original's bit for bit. PCG32 (pcg-random.org, XSH RR).
// An oracle run can instead roll with the original's generator from the state the original had
// (original()), so that both sides take the same first decisions; never in a normal game.
class Random {
public:
  explicit Random(std::uint64_t seed) { reseed(seed); }

  // The original's generator (libc rand: the state times 0x41C64E6D plus 0x3039, 15 bits out) from
  // its 32-bit state; next() then gives 15 bits and below() rolls as math.c: randomLimit does.
  [[nodiscard]] static Random original(std::uint32_t state) {
    Random random{0};
    random.original_ = true;
    random.state_ = state;
    return random;
  }

  // Starts the sequence of a seed again.
  void reseed(std::uint64_t seed) {
    state_ = 0;
    next();
    state_ += seed;
    next();
  }

  // Where the sequence is, to save it with a game; restoreState goes on from there.
  [[nodiscard]] std::uint64_t state() const { return state_; }
  void restoreState(std::uint64_t state) { state_ = state; }

  // The next 32 random bits (15 with the original's generator).
  std::uint32_t next() {
    if (original_) {
      state_ = ((state_ * OriginalMultiplier) + OriginalIncrement) & OriginalStateMask;
      return static_cast<std::uint32_t>(state_ >> OriginalOutputShift) & OriginalOutputMask;
    }
    const std::uint64_t old = state_;
    state_ = (old * Multiplier) + Increment;
    const auto xorShifted = static_cast<std::uint32_t>(((old >> XorShift) ^ old) >> OutputShift);
    const auto rotation = static_cast<std::uint32_t>(old >> RotationShift);
    return (xorShifted >> rotation) | (xorShifted << ((~rotation + 1U) & RotationMask));
  }

  // A whole number from 0 to limit - 1, as the original's roll of a limit (math.c: randomLimit); 0
  // when the limit is 0 or less. Like the original, a multiply and a shift rather than a remainder.
  std::int32_t below(std::int32_t limit) {
    if (limit <= 0) {
      return 0;
    }
    if (original_) {
      return static_cast<std::int32_t>(
          (static_cast<std::int64_t>(limit) * static_cast<std::int64_t>(next())) >>
          OriginalOutputBits);
    }
    return static_cast<std::int32_t>(
        (static_cast<std::uint64_t>(next()) * static_cast<std::uint64_t>(limit)) >> ResultBits);
  }

  // Whether a roll of 100 falls under `percent`: true `percent` times in 100.
  bool chance(std::int32_t percent) { return below(PercentLimit) < percent; }

private:
  static constexpr std::uint64_t Multiplier = 6364136223846793005ULL;
  static constexpr std::uint64_t Increment = 1442695040888963407ULL;
  static constexpr std::uint32_t XorShift = 18;
  static constexpr std::uint32_t OutputShift = 27;
  static constexpr std::uint32_t RotationShift = 59;
  static constexpr std::uint32_t RotationMask = 31;
  static constexpr std::uint32_t ResultBits = 32;
  static constexpr std::int32_t PercentLimit = 100;
  static constexpr std::uint64_t OriginalMultiplier = 0x41C64E6DULL;
  static constexpr std::uint64_t OriginalIncrement = 0x3039ULL;
  static constexpr std::uint64_t OriginalStateMask = 0xFFFFFFFFULL;
  static constexpr std::uint32_t OriginalOutputShift = 16;
  static constexpr std::uint32_t OriginalOutputMask = 0x7FFFU;
  static constexpr std::uint32_t OriginalOutputBits = 15;

  std::uint64_t state_ = 0;
  bool original_ = false;
};

} // namespace dwe::ruleset
